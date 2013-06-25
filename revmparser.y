%{
#include "revm.h"
#include <assert.h>

char *input;
int yylex();
void yyerror(const char *msg);
  
//Ast Types
char *typeNames[] = {
    "(NULL)",
    "Char",
    "Alt",
    "Concat",
    "Any",
    "Star",
    "Plus",
    "Quest",
    "Paren"
};

static ReAst *ast_new(int type, int c, ReAst *lhs, ReAst *rhs)
{
    ReAst *ast = pmalloc(sizeof(ReAst));
    ast->type = type;
    ast->c = c;
    ast->lhs = lhs;
    ast->rhs = rhs;
    return ast;
}

static ReAst *root = NULL;
%}

%union {
    int c;
    ReAst *ast;
}

%token EOL 
%token <c> CHAR

%type <ast> re alt concat term single

%%

re: alt EOL {
    $$ = $1;    
    root = $$;
    return 0;
  }
  ;

alt: concat
   | alt '|' concat {
    $$ = ast_new(Alt, 0, $1, $3);
   }
   ;

concat: term
      | concat term {
        $$ = ast_new(Concat, 0, $1, $2);
      }
      ;

term: single
    | single '*' {
        $$ = ast_new(Star, 0, $1, NULL);
    }
    | single '+' {
        $$ = ast_new(Plus, 0, $1, NULL);
    }
    | single '?' {
        $$ = ast_new(Quest, 0, $1, NULL);
    }
    ;

single: CHAR {
        $$ = ast_new(Char, $1, NULL, NULL);
      }
      | '.' {
        $$ = ast_new(Any, 0, NULL, NULL); 
      }
      | '(' alt ')' {
        $$ = ast_new(Paren, 0, $2, NULL);
      }
      | '(' '?' ':' alt ')' {
        $$ = $4;
      }
      ;

%% 

int yylex()
{
    if (input == NULL || *input == 0 || strchr("\n\r", *input)) {
        fprintf(stderr, "match EOL\n");
        return EOL;
    }

    int c = *input++;
    if (strchr("*+?:)(|.", c)) {
        return c;
    }

    yylval.c = c;
    return CHAR;
}

static void dumpast(ReAst *root)
{
    if (!root) {
        return;
    }
    fprintf(stderr, "%s %c(%x)\n", typeNames[root->type], root->c, root->c);
    dumpast(root->lhs);
    dumpast(root->rhs);
}

void yyerror(const char *msg)
{
    fprintf(stderr, "parse: %s at %c(%x)\n", msg, *input, *input);
}

int main(int argc, char **argv)
{
    input = NULL;
    if (argc < 3) {
        return -1;
    }

    input = strdup(argv[1]);
    int len = strlen(input);

    Re *re = malloc(sizeof(Re));
    re->insts = malloc(sizeof(Inst)*3*len);
    re->size = 0;

    yyparse();
    dumpast(root);

    Inst *i = re_compile(re, root); 
    re_addInst(re, IMatch, 0, NULL, NULL);

    assert(i == re->insts);
    dumpinsts(re);

    int matched = re_exec(re, argv[2]);
    if (matched) 
        printf("match\n");
    
    re_free(re);
    return matched;
}

