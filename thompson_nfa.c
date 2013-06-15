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

#include "nfa.h"

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

// a NFA state comprises of multiple States
typedef struct StateList_ {
    State **ss;
    int size;
} StateList;

typedef struct LinkList_ {
    void *payload;
    struct LinkList_ *next;
} LinkList;

typedef struct REprivate_ {
    char *fp; // frame pointer
    char *rep; // copy of regex literal

    int capacity; // NO. of States a NFA have
    StateList gstore1, gstore2; // temporary storage for NFA State
    int listid;

    // track temp resources for freeing
    LinkList *pspl; // StatePtrList, this can be freed before RE_match
    LinkList *pfrags; // Fragment, this can be freed before RE_match
    LinkList *pss;  // State
} REprivate;

static char metas[] = "*?+()|";

// check if it's primtive re
static inline int isprim(int c)
{
    return strchr(metas, c) == NULL;
}

static inline int peek(RE *re)
{
    return *(re->priv->fp);
}

static inline int tok(RE *re)
{
    return *(re->priv->fp)++;
}

static inline int eof(RE *re)
{
    return *(re->priv->fp) == 0;
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

State* state_new(RE *re, int c, State *out, State *out1)
{
    LinkList *old = re->priv->pss;

    LinkList *ll = malloc(sizeof(LinkList));
    State *s = (State*)malloc(sizeof(State));
    ll->payload = s;
    ll->next = old;
    re->priv->pss = ll;

    s->c = c;
    s->out = out;
    s->out1 = out1;
    s->lastlist = 0;
    return s;
}

Fragment* fragment_new(RE *re, State *start)
{
    LinkList *old = re->priv->pfrags;

    LinkList *ll = malloc(sizeof(LinkList));
    Fragment *f = (Fragment*)malloc(sizeof(Fragment));
    ll->payload = f;
    ll->next = old;
    re->priv->pfrags = ll;

    f->start = start;
    f->out = NULL;
    return f;
}

StatePtrList *list1(RE *re, State **outp)
{
    LinkList *old = re->priv->pspl;

    LinkList *ll = malloc(sizeof(LinkList));
    StatePtrList *spl = malloc(sizeof(StatePtrList));
    ll->payload = spl;
    ll->next = old;
    re->priv->pspl = ll;

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

static Fragment *match_re(RE *re, Fragment *lhs);
static Fragment *match_term(RE *re);

static Fragment *match_single(RE *re)
{
    int t = tok(re);
    if (!isprim(t)) {
        err_quit(EINVAL);
    }

    State *s = state_new(re, t, NULL, NULL);
    Fragment *f = fragment_new(re, s);
    f->out = list1(re, &(s->out));
    dump_frag("single", f);
    return f;
}

static Fragment *match_bracketed(RE *re)
{
    if (tok(re) != '(') {
        err_quit(EINVAL);
    }

    Fragment *e1 = match_re(re, NULL);

    if (tok(re) != ')') {
        err_quit(EINVAL);
    }

    return e1;
}

static Fragment *match_prim(RE *re)
{
    Fragment *e1 = NULL;
    int t = peek(re);
    if (t != '(') {
        e1 = match_single(re);
    } else {
        e1 = match_bracketed(re);
    }

    return e1;
}

static Fragment *match_uniform(RE *re, Fragment *e)
{
    Fragment *e1;
    int t = tok(re);
    switch(t) {
    case '*':
    {
        State *s = state_new(re, Split, e->start, NULL);
        patch(e->out, s);
        e1 = fragment_new(re, s);
        e1->out = list1(re, &(s->out1));
        break;
    }

    case '?':
    {
        State *s = state_new(re, Split, e->start, NULL);
        e1 = fragment_new(re, s);
        e1->out = append(e->out, list1(re, &(s->out1)));
        break;
    }

    case '+':
    {
        State *s = state_new(re, Split, e->start, NULL);
        patch(e->out, s);
        e1 = fragment_new(re, e->start);
        e1->out = list1(re, &(s->out1));
        break;
    }

    default:
        err_quit(EINVAL);
        break;
    }

    return e1;
}

static Fragment *match_alternate(RE *re, Fragment *e)
{
    if (tok(re) != '|') {
        err_quit(EINVAL);
    }

    Fragment *e1 = match_term(re);
    // alternating
    State *start = state_new(re, Split, e->start, e1->start);
    Fragment *f = fragment_new(re, start);
    f->out = append(e->out, e1->out);
    e1 = f;

    if (eof(re)) {
        return e1;
    }

    return match_re(re, e1);
}

static Fragment *match_term(RE *re)
{
    Fragment *e1 = match_prim(re);
    dump_frag("term", e1);
    switch(peek(re)) {
    case '*':
    case '?':
    case '+':
        return match_uniform(re, e1);

    default:
        return e1;
    }
}

static Fragment *match_re(RE *re, Fragment *lhs)
{
    Fragment *e1 = NULL, *e2 = NULL;
    if (eof(re))
        return lhs;

    if (peek(re) == ')') {
        return lhs;
    }

    debug("match_re: lhs %c\n", lhs ? lhs->start->c : 0);
    e1 = match_term(re);

    if (lhs) {
        debug("concate %c . %c\n", lhs->start->c, e1->start->c);
        patch(lhs->out, e1->start);
        e2 = fragment_new(re, lhs->start);
        e2->out = e1->out;
        e1 = e2;
    }

    if (peek(re) == '|') {
        e2 = match_alternate(re, e1);
    } else {
        e2 = match_re(re, e1);
    }

    return e2;
}

State matchstate = { Match };

// parsing
static State *compile(RE *re, const char *rep)
{
    Fragment *e = match_re(re, NULL);
    patch(e->out, &matchstate);
    return e->start;
}

static void addstate(RE *re, StateList *store, State *s)
{
    if (!s || s->lastlist == re->priv->listid)
        return;

    s->lastlist = re->priv->listid;

    if (s->c == Split) {
        addstate(re, store, s->out);
        addstate(re, store, s->out1);
        return; // so store only contains `core` states
    }

    store->ss[store->size++] = s;
}

static StateList *closure(RE *re, State *s, StateList *store)
{
    ++re->priv->listid;
    store->size = 0;
    addstate(re, store, s);
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

static void step(RE *re, StateList *sl, int c, StateList *next)
{
    ++re->priv->listid;
    next->size = 0;
    for (int i = 0; i < sl->size; ++i) {
        State *s = sl->ss[i];
        assert(s->c != Split);
        if (s->c == c) {
            addstate(re, next, s->out);
        }
    }
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

static void clean_tempdata(RE *re)
{
    LinkList **pll = &(re->priv->pfrags);
    while (*pll) {
        debug("free %lx\n", (unsigned long)(*pll));
        free((*pll)->payload);
        /* free(*pll); */
        pll = &((*pll)->next);
    }
    re->priv->pfrags = NULL;

    pll = &(re->priv->pspl);
    while (*pll) {
        debug("free spl %lx\n", (unsigned long)(*pll));
        free((*pll)->payload);
        free(*pll);
        pll = &((*pll)->next);
    }
    re->priv->pspl = NULL;
}

RE *RE_compile(const char *rep)
{
    RE *re = malloc(sizeof(RE));
    re->priv = malloc(sizeof(REprivate));
    bzero(re->priv, sizeof(REprivate));
    re->priv->rep = strdup(rep);
    re->priv->fp = re->priv->rep;

    re->start = compile(re, rep);
    return re;
}

int RE_match(RE *re, const char *s)
{
    clean_tempdata(re); // reduce memory usage

    REprivate *priv = re->priv;
    priv->capacity = 20;
    priv->gstore1.ss = (State**)malloc(sizeof(State*) * priv->capacity);
    priv->gstore2.ss = (State**)malloc(sizeof(State*) * priv->capacity);

    StateList *cl, *nl, *t;
    cl = closure(re, re->start, &(priv->gstore1));
    nl = &(priv->gstore2);
    while (*s) {
        step(re, cl, *s++, nl);
        t = nl, nl = cl, cl = t;
        if (ismatched(cl)) {
            return 1;
        }
    }

    return 0;
}

void RE_free(RE *re)
{
    free(re->priv->gstore2.ss);
    free(re->priv->gstore1.ss);
    free(re->priv->rep);

    clean_tempdata(re);

    LinkList **pll = &(re->priv->pss);
    while (*pll) {
        debug("free state %lx\n", (unsigned long)(*pll));
        free((*pll)->payload);
        free(*pll);
        pll = &((*pll)->next);
    }

    free(re->priv);
    free(re);
}

#ifdef STANDALONE
char *progname = NULL;
int main(int argc, char *argv[])
{
    State *nfa;
    progname = basename(argv[0]);

    if (argc == 3) {
        RE *re = RE_compile(argv[1]);

#ifdef DEBUG
        dump_nfa(re->start);
#else
        printf("match: %s\n", RE_match(re, argv[2]) ? "yes" : "no");

        RE_free(re);
#endif
    } else {
        fprintf(stderr, "%s re str", progname);
    }
    return 0;
}
#endif
