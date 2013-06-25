/**
 * VM for RE: http://swtch.com/~rsc/regexp/regexp2.html
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct ReAst_ ReAst;
struct ReAst_ {
    int type;
    int c;
    ReAst *lhs;
    ReAst *rhs;
};

//Ast Types
enum {
    Char = 1,
    Alt,
    Concat,
    Any,
    Star,
    Plus,
    Quest, //?
    Paren
};


// opcodes
enum {
    IChar = 1,
    IAny,
    ISplit,
    IJmp,
    IMatch
};

typedef struct Inst_ {
    int op;
    int c;
    struct Inst_ *br1;
    struct Inst_ *br2;
} Inst;

typedef struct Thread_ {
    Inst *pc;

} Thread;

typedef struct ThreadList_ {
    Thread *threads;
    int n;
} ThreadList;

typedef struct Re_ {
    Inst *insts;
    int size;

    //private, move it
    int capacity; // max threads
    ThreadList tpool[2];
} Re;

extern Inst *re_addInst(Re *re, int op, int c, Inst *br1, Inst *br2);
extern void *pmalloc(size_t size);
extern Inst *re_compile(Re *re, ReAst *ast);
extern int re_exec(Re *re, const char *s);
extern void re_free(Re *re);
extern void dumpinst(Re *re, Inst *i);
extern void dumpinsts(Re *re);
