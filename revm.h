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
    int nongreedy; // 0 is greedy, 1 is not
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
    IMatch,
    ISave
};

typedef struct Sub_ {
    char *sp;
} Sub;

#define NPAREN 10

typedef struct Thread_ Thread;
typedef struct Inst_ {
    int op;
    int c;
    int gen;
    struct Inst_ *br1;
    struct Inst_ *br2;
} Inst;

struct Thread_ {
    Inst *pc;
    Sub sub[2*NPAREN];
};

typedef struct ThreadList_ {
    Thread *threads;
    int n;
} ThreadList;

enum {
    RE_ANCHOR_HEAD = 0x01,
    RE_ANCHOR_TAIL = 0x02,
};

typedef struct Re_ {
    Inst *insts;
    int size;
    Sub sub[2*NPAREN];
    char *s;
    int matched;  // flag that some of threads match

    int opts;

    //private, move it
    int capacity; // max threads
    ThreadList tpool[2];
    ReAst *ast;
    int gen; // generation of threadlist
} Re;

extern ReAst *ast_new(int type, int c, ReAst *lhs, ReAst *rhs);
extern void *pmalloc(size_t size);
extern Re *re_new(const char *, int opts);
extern int re_exec(Re *re, char *s);
extern void re_free(Re *re);

extern void re_setopt(Re *re, int opt);
extern int re_getopt(Re *re, int opt);
