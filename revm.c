#include "revm.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

void dumpinst(Re *re, Inst *i)
{
    static char *instNames[] = {
        "(NULL)",
        "char",
        "any",
        "split",
        "jmp",
        "match"
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
    }

    buf[len] = '\0';
    fprintf(stderr, "%s\n", buf);

}

void dumpinsts(Re *re)
{
    for (int p = 0; p < re->size; p++) {
        Inst *i = &re->insts[p];
        dumpinst(re, i);
    }
}

void dumpthreads(const char *msg, Re *re, ThreadList *tl)
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

Inst *re_addInst(Re *re, int op, int c, Inst *br1, Inst *br2)
{
    Inst *i = &re->insts[re->size++];
    i->op = op;
    i->c = c;
    i->br1 = br1;
    i->br2 = br2;
    return i;
}

Inst *re_compile(Re *re, ReAst *ast)
{
    if (!ast) {
        return NULL;
    }

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
        return i;
    }

    case Plus: {
        Inst *i = re_compile(re, ast->lhs);
        Inst *i2 = re_addInst(re, ISplit, 0, i, NULL);
        i2->br2 = &re->insts[re->size];
        return i;
    }

    case Quest: {
        Inst *i = re_addInst(re, ISplit, 0, NULL, NULL);
        i->br1 = re_compile(re, ast->lhs);
        i->br2 = &re->insts[re->size];
        return i;
    }

    case Paren: {
        assert(0);
        return NULL;
    }

    default:
        assert(0);
        return NULL;
    };
}

#define swap_list(tl1, tl2) do {                \
        ThreadList *tmp = tl1;                  \
        tl1 = tl2;                              \
        tl2 = tmp;                              \
    } while(0)

int re_exec(Re *re, const char *s)
{
    re->capacity = re->size;
    re->tpool[0].threads = malloc(sizeof(Thread) * re->capacity);
    re->tpool[1].threads = malloc(sizeof(Thread) * re->capacity);

    ThreadList *cl = &re->tpool[0], *nl = &re->tpool[1];
    cl->n = 1;
    cl->threads[0].pc = &re->insts[0];

    while (*s) {
        nl->n = 0;
        for (int i = 0; i < cl->n; i++) {
            Thread t = cl->threads[i];
            switch(t.pc->op) {
            case IChar:
                if (t.pc->c != *s) {
                    continue;
                }
                nl->threads[nl->n++].pc = t.pc + 1;
                break;

            case IAny:
                nl->threads[nl->n++].pc = t.pc + 1;
                break;

            case ISplit:
                cl->threads[cl->n++].pc = t.pc->br1;
                cl->threads[cl->n++].pc = t.pc->br2;
                break;

            case IJmp:
                cl->threads[cl->n++].pc = t.pc->br1;
                break;

            case IMatch:
                return 1;
            }
        }
        dumpthreads("\ncl:\n", re, cl);
        dumpthreads("nl:\n", re, nl);
        swap_list(cl, nl);
        ++s;
    }
    return 0;
}

void re_free(Re *re)
{
    free(re->tpool[0].threads);
    free(re->tpool[1].threads);
    free(re->insts);
    free(re);
}
