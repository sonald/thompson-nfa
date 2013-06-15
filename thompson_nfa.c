// see: http://swtch.com/~rsc/regexp/regexp1.html
// I use a recursive descend parser for the following re grammar :
// R -> term
//   -> term R
//   -> term '|' R;
//
// term -> prim
//      -> prim '*'
//      -> prim '?'
//      -> prim '+';
//
// prim -> LITERAL
//      -> '(' R ')'


#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <assert.h>
#include <libgen.h>

/* #define DEBUG */

#define EINVAL "invalid re"

#define err_quit(msg) {                                          \
        fprintf(stderr, "%s:%d: %s\n", __func__, __LINE__, msg); \
        exit(1);                                                 \
    }

static inline void debug(const char *fmt, ...)
{
#ifdef DEBUG
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
#endif
}



enum {
    Split = 256,
    Match = 257
};

typedef struct State_ {
    int c;
    struct State_ *out;
    struct State_ *out1;
    int lastlist; // for optimization
} State;

typedef struct StatePtrList_ {
    State **s;
    struct StatePtrList_ *next;
} StatePtrList;

typedef struct Fragment_ {
    State *start;
    StatePtrList *out;
} Fragment;

static char metas[] = "*?+()|";

// check if it's primtive re
static inline int isprim(int c)
{
    return strchr(metas, c) == NULL;
}

static char *fp = NULL;

static inline int peek()
{
    return *fp;
}

static inline int tok()
{
    return *fp++;
}

static inline int eof()
{
    return *fp == 0;
}


static void dump_frag(const char *head, Fragment *f)
{
#ifdef DEBUG
    char msg[1024];
    int len = sprintf(msg, "[%s]: frag: start %c, out: ", head, f->start->c);
    StatePtrList *p = f->out;
    while (p) {
        char c[1] = { *(p->s) ? (*(p->s))->c : 'x'};
        strncat(msg, c, 1);
        strcat(msg, ", ");
        p = p->next;
    }
    strcat(msg, "\n");
    debug(msg);
#endif
}

static void dump_state(const char *head, State *s)
{
#ifdef DEBUG
    int c = s->c ? s->c: 0;
    c = c == Split ? '/' : (c == Match ? '#': c);
    fprintf(stderr, "[%s]: State %d: %c, out: %d, out1: %d\n", head, s->lastlist,
            c, s->out ? s->out->lastlist : 0, s->out1 ? s->out1->lastlist : 0);

#endif
}

State* state_new(int c, State *out, State *out1)
{
    State *s = (State*)malloc(sizeof(State));
    s->c = c;
    s->out = out;
    s->out1 = out1;
    s->lastlist = 0;
    return s;
}

Fragment* fragment_new(State *start)
{
    Fragment *f = (Fragment*)malloc(sizeof(Fragment));
    f->start = start;
    f->out = NULL;
    return f;
}

StatePtrList *list1(State **outp)
{
    StatePtrList *spl = malloc(sizeof(StatePtrList));
    spl->s = outp;
    spl->next = NULL;
    return spl;
}

StatePtrList *append(StatePtrList *l1, StatePtrList *l2)
{
    StatePtrList  *pp = l1;
    while (pp->next != NULL) {
        pp = pp->next;
    }

    pp->next = l2;
    return l1;
}

void patch(StatePtrList *l, State *s)
{
    StatePtrList *p = l;
    while (p) {
        *(p->s) = s;
        p = p->next;
    }
}

static Fragment *match_re();
static Fragment *match_term();

static Fragment *match_single()
{
    int t = tok();
    if (!isprim(t)) {
        err_quit(EINVAL);
    }

    State *s = state_new(t, NULL, NULL);
    Fragment *f = fragment_new(s);
    f->out = list1(&(s->out));
    dump_frag("single", f);
    return f;
}

static Fragment *match_bracketed()
{
    if (tok() != '(') {
        err_quit(EINVAL);
    }

    Fragment *e1 = match_re(NULL);

    if (tok() != ')') {
        err_quit(EINVAL);
    }

    return e1;
}

static Fragment *match_prim()
{
    Fragment *e1 = NULL;
    int t = peek();
    if (t != '(') {
        e1 = match_single();
    } else {
        e1 = match_bracketed();
    }

    return e1;
}

static Fragment *match_uniform(Fragment *e)
{
    Fragment *e1;
    int t = tok();
    switch(t) {
    case '*':
    {
        State *s = state_new(Split, e->start, NULL);
        patch(e->out, s);
        e1 = fragment_new(s);
        e1->out = list1(&(s->out1));
        break;
    }

    case '?':
    {
        State *s = state_new(Split, e->start, NULL);
        e1 = fragment_new(s);
        e1->out = append(e->out, list1(&(s->out1)));
        break;
    }

    case '+':
    {
        State *s = state_new(Split, e->start, NULL);
        patch(e->out, s);
        e1 = fragment_new(e->start);
        e1->out = list1(&(s->out1));
        break;
    }

    default:
        err_quit(EINVAL);
        break;
    }

    return e1;
}

static Fragment *match_alternate(Fragment *e)
{
    if (tok() != '|') {
        err_quit(EINVAL);
    }

    Fragment *e1 = match_term();
    // alternating
    State *start = state_new(Split, e->start, e1->start);
    Fragment *f = fragment_new(start);
    f->out = append(e->out, e1->out);
    e1 = f;

    if (eof()) {
        return e1;
    }

    return match_re(e1);
}

static Fragment *match_term()
{
    Fragment *e1 = match_prim();
    dump_frag("term", e1);
    switch(peek()) {
    case '*':
    case '?':
    case '+':
        return match_uniform(e1);

    default:
        return e1;
    }
}

static Fragment *match_re(Fragment *lhs)
{
    Fragment *e1 = NULL, *e2 = NULL;
    if (eof())
        return lhs;

    if (peek() == ')') {
        return lhs;
    }

    debug("match_re: lhs %c\n", lhs ? lhs->start->c : 0);
    e1 = match_term();

    if (lhs) {
        debug("concate %c . %c\n", lhs->start->c, e1->start->c);
        patch(lhs->out, e1->start);
        e2 = fragment_new(lhs->start);
        e2->out = e1->out;
        e1 = e2;
    }

    if (peek() == '|') {
        e2 = match_alternate(e1);
    } else {
        e2 = match_re(e1);
    }

    return e2;
}

State matchstate = { Match };

// parsing
State *compile(char *re)
{
    fp = re;
    Fragment *e = match_re(NULL);
    patch(e->out, &matchstate);
    return e->start;
}

typedef struct StateList_ {
    State **ss;
    int size;
} StateList;

static StateList gstore1, gstore2;
static int listid = 0;

static void addstate(StateList *store, State *s)
{
    if (!s || s->lastlist == listid)
        return;

    s->lastlist = listid;

    if (s->c == Split) {
        addstate(store, s->out);
        addstate(store, s->out1);
        return; // so store only contains `core` states
    }

    store->ss[store->size++] = s;
}

static StateList *closure(State *s, StateList *store)
{
    ++listid;
    store->size = 0;
    addstate(store, s);
    return store;
}

static int ismatched(StateList *sl)
{
    for (int i = 0; i < sl->size; ++i) {
        if (sl->ss[i]->c == Match) {
            return 1;
        }
    }

    return 0;
}

static void step(StateList *sl, int c, StateList *next)
{
    ++listid;
    next->size = 0;
    for (int i = 0; i < sl->size; ++i) {
        State *s = sl->ss[i];
        assert(s->c != Split);
        if (s->c == c) {
            addstate(next, s->out);
        }
    }
}

int match(State *re, const char *s)
{
    int storage = 100;
    gstore1.ss = (State**)malloc(sizeof(State*)*storage);
    gstore2.ss = (State**)malloc(sizeof(State*)*storage);

    StateList *cl, *nl, *t;
    cl = closure(re, &gstore1);
    nl = &gstore2;
    while (*s) {
        step(cl, *s++, nl);
        t = nl, nl = cl, cl = t;
        if (ismatched(cl)) {
            return 1;
        }
    }

    return 0;
}

static void annotate_nfa(State *s, int id)
{
    if (!s || s->lastlist > 0)
        return;

    s->lastlist = id;
    annotate_nfa(s->out, ++id);
    annotate_nfa(s->out1, ++id);
    dump_state("annotate", s);
}

void dump_nfa(State *start)
{
    matchstate.lastlist = 1000;
    annotate_nfa(start, 1);
}

char *progname = NULL;
int main(int argc, char *argv[])
{
    State *nfa;
    progname = basename(argv[0]);

    if (argc == 3) {
        nfa = compile(argv[1]);
#ifdef DEBUG
        dump_nfa(nfa);
#else
        printf("match: %s\n", match(nfa, argv[2]) ? "yes" : "no");
#endif
    } else {
        fprintf(stderr, "%s re str", progname);
    }


    return 0;
}
