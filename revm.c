#include "revm.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>

static inline void debug(const char *fmt, ...)
{
#ifdef DEBUG
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
#endif
}

//Ast Types
static char *typeNames[] = {
    "(NULL)",
    "Char",
    "Alt",
    "Concat",
    "Any",
    "Star",
    "Plus",
    "Quest",
    "Paren",
    "NgStar",
    "NgPlus",
    "NgQuest"
};

static void dumpast(ReAst *root, int deep)
{
    if (!root) {
        return;
    }
    switch(root->type) {
    case Char:
    case Any:
        fprintf(stderr, "%*c%s(%c)\n", 2*deep, ' ', typeNames[root->type], root->c?root->c:'.');
        break;

    case Star:
    case Plus:
    case Quest:
        if (root->nongreedy == 1) {
            fprintf(stderr, "%*c%s\n", 2*deep, ' ', typeNames[root->type+Paren-Star+1]);
            break;
        }
        //fallthrough

    default:
        fprintf(stderr, "%*c%s\n", 2*deep, ' ', typeNames[root->type]);
        break;
    }

    dumpast(root->lhs, deep+1);
    dumpast(root->rhs, deep+1);
}


static void dumpinst(Re *re, Inst *i)
{
    static char *instNames[] = {
        "(NULL)",
        "char",
        "any",
        "split",
        "jmp",
        "match",
        "save"
    };

    char buf[128];
    int len = snprintf(buf, sizeof buf - 1, "%ld %s ", i - re->insts, instNames[i->op]);
    switch(i->op) {
    case IChar:
        len += sprintf(buf+len, "%c", i->c); break;

    case ISplit:
        len += sprintf(buf+len, "%ld, %ld", i->br1 - re->insts, i->br2 - re->insts); break;

    case IJmp:
        len += sprintf(buf+len, "%ld", i->br1 - re->insts); break;

    case ISave:
        len += sprintf(buf+len, "%d", i->c); break;
    }

    buf[len] = '\0';
    fprintf(stderr, "%s\n", buf);

}

static void dumpsub(Re *re, Sub sub[NPAREN])
{
    char buf[128];
    int len = 0;
    for (int i = 0; i < NPAREN; i++) {
        if (sub[i*2].sp && sub[i*2+1].sp) {
            len += sprintf(buf+len, "(%ld, %ld)", sub[i*2].sp - re->s,
                           sub[i*2+1].sp - re->s);
        } else {
            len += sprintf(buf+len, "(?, ?)");
        }
    }

    debug("%s\n", buf);
}

static void dumpinsts(Re *re)
{
    for (int p = 0; p < re->size; p++) {
        Inst *i = &re->insts[p];
        dumpinst(re, i);
    }
}

static void dumpthreads(const char *msg, Re *re, ThreadList *tl)
{
    fprintf(stderr, "%s", msg);
    for (int i = 0; i < tl->n; i++) {
        dumpinst(re, tl->threads[i].pc);
    }
}

void *pmalloc(size_t size)
{
    void *p = malloc(size);
    if (p == NULL) {
        perror("malloc");
        exit(errno);
    }

    return p;
}


ReAst *ast_new(int type, int c, ReAst *lhs, ReAst *rhs)
{
    ReAst *ast = pmalloc(sizeof(ReAst));
    ast->type = type;
    ast->c = c;
    ast->nongreedy = 0;
    ast->lhs = lhs;
    ast->rhs = rhs;
    return ast;
}

static Inst *re_addInst(Re *re, int op, int c, Inst *br1, Inst *br2)
{
    Inst *i = &re->insts[re->size++];
    i->op = op;
    i->c = c;
    i->gen = 0;
    i->br1 = br1;
    i->br2 = br2;
    return i;
}

extern void yyparse();

static Inst *re_compile(Re *re, ReAst *ast)
{
    if (!ast) {
        return NULL;
    }

    Inst *tmp;

    switch(ast->type) {
    case Alt: {
        Inst *i = re_addInst(re, ISplit, 0, NULL, NULL);
        i->br1 = re_compile(re, ast->lhs);
        Inst *i2 = re_addInst(re, IJmp, 0, NULL, NULL);
        i->br2 = re_compile(re, ast->rhs);
        i2->br1 = &re->insts[re->size];
        return i;
    }
    case Concat: {
        Inst *i = re_compile(re, ast->lhs);
        re_compile(re, ast->rhs);
        return i;
    }

    case Char: {
        return re_addInst(re, IChar, ast->c, NULL, NULL);
    }

    case Any: {
        return re_addInst(re, IAny, 0, NULL, NULL);
    }

    case Star: {
        Inst *i = re_addInst(re, ISplit, 0, NULL, NULL);
        Inst *i2 = re_compile(re, ast->lhs);
        re_addInst(re, IJmp, 0, i, NULL);
        i->br1 =i2;
        i->br2 = &re->insts[re->size];
        if (ast->nongreedy) {
            tmp = i->br1;
            i->br1 = i->br2;
            i->br2 = tmp;
        }
        return i;
    }

    case Plus: {
        Inst *i = re_compile(re, ast->lhs);
        Inst *i2 = re_addInst(re, ISplit, 0, i, NULL);
        i2->br2 = &re->insts[re->size];
        if (ast->nongreedy) {
            tmp = i->br1;
            i->br1 = i->br2;
            i->br2 = tmp;
        }
        return i;
    }

    case Quest: {
        Inst *i = re_addInst(re, ISplit, 0, NULL, NULL);
        i->br1 = re_compile(re, ast->lhs);
        i->br2 = &re->insts[re->size];
        if (ast->nongreedy) {
            tmp = i->br1;
            i->br1 = i->br2;
            i->br2 = tmp;
        }
        return i;
    }

    case Paren: {
        Inst *i = re_addInst(re, ISave, 2*ast->c, NULL, NULL);
        re_compile(re, ast->lhs);
        re_addInst(re, ISave, 2*ast->c + 1, NULL, NULL);
        return i;
    }

    default:
        assert(0);
        return NULL;
    };
}

void re_setopt(Re *re, int opt)
{
    re->opts |= opt;
}

int re_getopt(Re *re, int opt)
{
    return re->opts & opt;
}

static int collect_insts(ReAst *ast)
{
    switch (ast->type) {
    case Alt:
    case Star:
    case Paren: return 2;

    case Concat: return 0;

    case Char:
    case Any:
    case Plus:
    case Quest: return 1;

    default:
        assert(0);
        return 0;
    }
}

static int visit_ast(ReAst *ast, int (*fn)(ReAst*))
{
    if (!ast) {
        return 0;
    }

    int val = visit_ast(ast->lhs, fn) + visit_ast(ast->rhs, fn);
    val += fn(ast);
    return val;
}

//FIXME: global var, no good
extern char *input;
Re *re_new(const char *rep, int opts)
{
    Re *re = malloc(sizeof(Re));
    bzero(re, sizeof re);

    input = (char *)rep;
    re_setopt(re, opts);

    yyparse(re);

    re->ast = ast_new(Paren, 0, re->ast, NULL);
    if (!re_getopt(re, RE_ANCHOR_HEAD)) {
        ReAst *ast = ast_new(Star, 0, ast_new(Any, 0, NULL, NULL), NULL);
        ast->nongreedy = 1;
        re->ast = ast_new(Concat, 0, ast, re->ast);
    }
    dumpast(re->ast, 0);

    int nr_insts = visit_ast(re->ast, collect_insts) + 1; // plus 1 for IMatch
    debug("insts size: %d\n", nr_insts);
    re->insts = malloc(sizeof(Inst) * nr_insts);

    re_compile(re, re->ast);
    re_addInst(re, IMatch, 0, NULL, NULL);

    dumpinsts(re);

    return re;
}

#define swap_list(tl1, tl2) do {                \
        ThreadList *tmp = tl1;                  \
        tl1 = tl2;                              \
        tl2 = tmp;                              \
    } while(0)

static void addthread(Re *re, ThreadList *tl, Inst *pc, Sub *sub, char *sp)
{
    //FIXME: need O(1) search
    if (pc->gen == re->gen) {
        /* debug("["); */
        /* dumpinst(re, pc); */
        /* debug("] already in thread\n"); */
        return;
    }
    pc->gen = re->gen;

    //recursive adding respects thread priority(greedy or not changes priority)
    switch(pc->op) {
    case ISplit:
        addthread(re, tl, pc->br1, sub, sp);
        addthread(re, tl, pc->br2, sub, sp);
        break;

    case IJmp:
        addthread(re, tl, pc->br1, sub, sp);
        break;

    case ISave: {
        //FIXME: mem leak
        Sub *newsub = malloc(sizeof re->sub);
        memcpy(newsub, sub, sizeof re->sub);
        newsub[pc->c].sp = sp;
        /* debug("saving: %ld at %d\n", sp - re->s, pc->c); */
        addthread(re, tl, pc+1, newsub, sp);
        break;
    }

    default:
        tl->threads[tl->n].pc = pc;
        memcpy(tl->threads[tl->n++].sub, sub, sizeof re->sub);
        break;
    }
}

int re_exec(Re *re, char *s)
{
    re->s = s;
    re->capacity = re->size;
    re->tpool[0].threads = malloc(sizeof(Thread) * re->capacity);
    re->tpool[1].threads = malloc(sizeof(Thread) * re->capacity);
    bzero(re->tpool[0].threads, sizeof re->tpool[0].threads);
    bzero(re->tpool[1].threads, sizeof re->tpool[1].threads);

    re->gen = 1;
    ThreadList *cl = &re->tpool[0], *nl = &re->tpool[1];
    addthread(re, cl, &re->insts[0], re->sub, (char *)s);

    for (;;s++) {
        /* debug("*s: %c\n", *s); */
        /* dumpthreads("cl:\n", re, cl); */
        re->gen++;
        nl->n = 0;
        for (int i = 0; i < cl->n; i++) {
            Thread t = cl->threads[i];
            Inst *pc = t.pc;
            switch(pc->op) {
            case IChar:
                if (pc->c != *s) {
                    continue;
                }
                addthread(re, nl, pc+1, t.sub, (char*)s+1);
                break;

            case IAny:
                if (*s == '\0') {
                    break;
                }

                addthread(re, nl, pc+1, t.sub, (char*)s+1);
                break;

            case IMatch:
                memcpy(re->sub, t.sub, sizeof re->sub);
                re->matched++;
                cl->n = i; // cut off threads with low priorities
                break;
            }
        }

        /* dumpthreads("nl:\n", re, nl); */
        swap_list(cl, nl);
        if (*s == '\0') {
            break;
        }
    }

    int done = re->matched > 0;
    dumpsub(re, re->sub);
    if (re_getopt(re, RE_ANCHOR_TAIL)) {
        done = done && (re->sub[1].sp == s);
    }

    return done;
}

static void free_ast(ReAst *ast)
{
    if (!ast) {
        return;
    }

    free_ast(ast->lhs);
    free_ast(ast->rhs);
    free(ast);
}

void re_free(Re *re)
{
    free(re->tpool[0].threads);
    free(re->tpool[1].threads);
    free(re->insts);
    free_ast(re->ast);
    free(re);
}
