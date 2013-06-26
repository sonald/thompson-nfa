%{
#include "revm.h"
#include <assert.h>

char *input;
int yylex();
void yyerror(Re *, const char *msg);

static int nparen = 0;
%}

%union {
    int c;
    ReAst *ast;
    int nparen;
}

%parse-param { Re *re }

%token EOL 
%token <c> CHAR
%type <nparen> count
%type <ast> re alt concat term single

%%

re: alt EOL {
    $$ = $1;    
    re->ast = $$;
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
	| single '*' '?' {
        $$ = ast_new(Star, 0, $1, NULL);
		$$->nongreedy = 1;		
	}
    | single '+' {
        $$ = ast_new(Plus, 0, $1, NULL);
    }
	| single '+' '?' {
        $$ = ast_new(Plus, 0, $1, NULL);
		$$->nongreedy = 1;				
	}
    | single '?' {
        $$ = ast_new(Quest, 0, $1, NULL);
    }
	| single '?' '?' {
		$$ = ast_new(Quest, 0, $1, NULL);
		$$->nongreedy = 1;
	}
    ;

count: {
        $$ = ++nparen;
     };

single: CHAR {
        $$ = ast_new(Char, $1, NULL, NULL);
      }
      | '.' {
        $$ = ast_new(Any, 0, NULL, NULL); 
      }
      | '(' count alt ')' {
        $$ = ast_new(Paren, $2, $3, NULL);
      }
      | '(' '?' ':' alt ')' {
        $$ = $4;
      }
      ;

%% 

int yylex()
{
    if (input == NULL || *input == 0 || strchr("\n\r", *input)) {
        return EOL;
    }

    int c = *input++;
    if (strchr("*+?:)(|.", c)) {
        return c;
    }

    yylval.c = c;
    return CHAR;
}

void yyerror(Re *re, const char *msg)
{
    fprintf(stderr, "parse: %s at %c(%x)\n", msg, *input, *input);
}

static void usage()
{
	fprintf(stderr, "igrepvm regex str\n");
}

int main(int argc, char **argv)
{
    input = NULL;
    if (argc < 3) {
		usage();
        return -1;
    }
	
	Re *re = re_new(argv[1], 0);
	
    int matched = re_exec(re, argv[2]);
    if (matched) 
        printf("matched\n");
    
    re_free(re);
    return matched;
}

