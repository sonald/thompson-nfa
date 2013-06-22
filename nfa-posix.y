/*
 * Regular expression implementation.
 * Supports traditional egrep syntax, plus non-greedy operators.
 * Tracks submatches a la POSIX.
 *
 * Seems to work (by running backward!) but very subtle.
 * Assumes repetitions are all individually parenthesized:
 * must say '(a?)b(c*)' not 'a?bc*'.
 *
 * Let m = length of regexp (number of states), and 
 * let p = number of capturing parentheses, and
 * let t = length of the text.  POSIX via running backward,
 * implemented here, requires O(m*p) storage during execution.
 * Can implement via running forward instead, but would
 * require O(m*p+m*m) storage and is not nearly so simple.
 *
 * yacc -v nfa-posix.y && gcc y.tab.c

These should be equivalent:

re='ab|cd|ef|a|bc|def|bcde|f'
a.out "(?:$re)(?:$re)($re)" abcdef
a.out "($re)*" abcdef

 (0,6)(3,6) => longest last guy (wrong)
 (0,6)(5,6) => shortest last guy (wrong)
 (0,6)(4,6) => posix last guy (right)

 * Copyright (c) 2007 Russ Cox.
 * Can be distributed under the MIT license, see bottom of file.
 */

%{
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

enum
{
	NSUB = 20,
	MPAREN = 9,
};

typedef struct Sub Sub;
struct Sub
{
	char *sp;
	char *ep;
};

enum
{
	Char = 1,
	Any = 2,
	Split = 3,
	LParen = 4,
	RParen = 5,
	Match = 6,
};
typedef struct State State;
typedef struct Thread Thread;
struct State
{
	int op;
	int data;
	State *out;
	State *out1;
	int id;
	int lastlist;
	Thread *lastthread;
};

struct Thread
{
	State *state;
	Sub match[NSUB];
};

typedef struct List List;
struct List
{
	Thread *t;
	int n;
};

int debug;

State matchstate = { Match };
int nstate;
int listid;
List l1, l2;

/* Allocate and initialize State */
State*
state(int op, int data, State *out, State *out1)
{
	State *s;
	
	nstate++;
	s = malloc(sizeof *s);
	s->lastlist = 0;
	s->op = op;
	s->data = data;
	s->out = out;
	s->out1 = out1;
	s->id = nstate;
	return s;
}

typedef struct Frag Frag;
typedef union Ptrlist Ptrlist;
struct Frag
{
	State *start;
	Ptrlist *out;
};

/* Initialize Frag struct. */
Frag
frag(State *start, Ptrlist *out)
{
	Frag n = { start, out };
	return n;
}

/*
 * Since the out pointers in the list are always 
 * uninitialized, we use the pointers themselves
 * as storage for the Ptrlists.
 */
union Ptrlist
{
	Ptrlist *next;
	State *s;
};

/* Create singleton list containing just outp. */
Ptrlist*
list1(State **outp)
{
	Ptrlist *l;
	
	l = (Ptrlist*)outp;
	l->next = NULL;
	return l;
}

/* Patch the list of states at out to point to start. */
void
patch(Ptrlist *l, State *s)
{
	Ptrlist *next;
	
	for(; l; l=next){
		next = l->next;
		l->s = s;
	}
}

/* Join the two lists l1 and l2, returning the combination. */
Ptrlist*
append(Ptrlist *l1, Ptrlist *l2)
{
	Ptrlist *oldl1;
	
	oldl1 = l1;
	while(l1->next)
		l1 = l1->next;
	l1->next = l2;
	return oldl1;
}

int nparen;
void yyerror(char*);
int yylex(void);
State *start;

Frag
paren(Frag f, int n)
{
	State *s1, *s2;

	if(n > MPAREN)
		return f;
	s1 = state(RParen, n, f.start, NULL);
	s2 = state(LParen, n, NULL, NULL);
	patch(f.out, s2);
	return frag(s1, list1(&s2->out));
}

%}

%union {
	Frag	frag;
	int	c;
	int nparen;
}

%token	<c>	CHAR
%token	EOL

%type	<frag>	alt concat repeat single line
%type	<nparen>	count

%%

line: alt EOL
	{
		State *s;

		$1 = paren($1, 0);
		s = state(Match, 0, NULL, NULL);
		patch($1.out, s);
		start = $1.start;
		return 0;
	}

alt:
	concat
|	alt '|' concat
	{
		State *s = state(Split, 0, $1.start, $3.start);
		$$ = frag(s, append($1.out, $3.out));
	}
;

concat:
	repeat
|	concat repeat
	{
		patch($2.out, $1.start);
		$$ = frag($2.start, $1.out);
	}
;

repeat:
	single
|	single '*'
	{
		State *s = state(Split, 0, $1.start, NULL);
		patch($1.out, s);
		$$ = frag(s, list1(&s->out1));
	}
|	single '+'
	{
		State *s = state(Split, 0, $1.start, NULL);
		patch($1.out, s);
		$$ = frag($1.start, list1(&s->out1));
	}
|	single '?'
	{
		State *s = state(Split, 0, $1.start, NULL);
		$$ = frag(s, append($1.out, list1(&s->out1)));
	}
;

count:	{ $$ = ++nparen; }

single:
	'(' count alt ')'
	{
		$$ = paren($3, $2);
	}
|	'(' '?' ':' alt ')'
	{
		$$ = $4;
	}
|	CHAR
	{
		State *s = state(Char, $1, NULL, NULL);
		$$ = frag(s, list1(&s->out));
	}
|	'.'
	{
		State *s = state(Any, 0, NULL, NULL);
		$$ = frag(s, list1(&s->out));
	}
;

%%

char *input;
char *text;
void dumplist(List*);

int
yylex(void)
{
	int c;

	if(input == NULL || *input == 0)
		return EOL;
	c = *input++;
	if(strchr("|*?():.", c))
		return c;
	yylval.c = c;
	return CHAR;
}

void
yyerror(char *s)
{
	fprintf(stderr, "parse error: %s\n", s);
	exit(1);
}

void
printmatch(Sub *m, int jump)
{
	int i;
	
	for(i=jump-1; i<2*nparen+2; i+=jump){
		if(m[i].sp && m[i].ep)
			printf("(%d,%d)", m[i].sp - text, m[i].ep - text);
		else if(m[i].sp)
			printf("(%d,?)", m[i].sp - text);
		else
			printf("(?,?)");
	}
}

void
dumplist(List *l)
{
	int i;
	Thread *t;

	for(i=0; i<l->n; i++){
		t = &l->t[i];
		if(t->state->op != Char && t->state->op != Any && t->state->op != Match)
			continue;
		printf("  ");
		printf("%d ", t->state->id);
		printmatch(t->match, 1);
		printf("\n");
	}
}

/*
 * Is match a better than match b?
 * If so, return 1; if not, 0.
 */
int
_better(Sub *a, Sub *b)
{
	int i;

	/* Leftmost longest */
	for(i=0; i<2*nparen+2; i++){
		if(a[i].sp != b[i].sp)
			return b[i].sp == NULL || a[i].sp < b[i].sp;
		if(a[i].ep != b[i].ep)
			return a[i].ep > b[i].ep;
	}
	return 0;
}

int
better(Sub *a, Sub *b)
{
	int r;
	
	r = _better(a, b);
	if(debug > 1){
		printf("better? ");
		printmatch(a, 1);
		printf(" vs ");
		printmatch(b, 1);
		printf(": %s\n", r ? "yes" : "no");
	}
	return r;
}

/*
 * Add s to l, following unlabeled arrows.
 * Next character to read is p.
 */
void
addstate(List *l, State *s, Sub *m, char *p)
{
	Sub save0, save1;

	if(s == NULL)
		return;

	if(s->lastlist == listid){
		if(!better(m, s->lastthread->match))
			return;
	}else{
		s->lastlist = listid;
		s->lastthread = &l->t[l->n++];
	}
	s->lastthread->state = s;
	memmove(s->lastthread->match, m, NSUB*sizeof m[0]);

	switch(s->op){
	case Split:
		/* follow unlabeled arrows */
		addstate(l, s->out, m, p);
		addstate(l, s->out1, m, p);
		break;
	
	case LParen:
		save0 = m[2*s->data];
		save1 = m[2*s->data+1];
		/* record left paren location and keep going */
		m[2*s->data].sp = p;
		if(save1.sp == NULL)
			m[2*s->data+1].sp = p;
		addstate(l, s->out, m, p);
		/* restore old information before returning. */
		m[2*s->data] = save0;
		m[2*s->data+1] = save1;
		break;
	
	case RParen:
		save0 = m[2*s->data];
		save1 = m[2*s->data+1];
		/* record right paren location and keep going */
		m[2*s->data].ep = p;
		m[2*s->data].sp = NULL;
		if(save1.ep == NULL)
			m[2*s->data+1].ep = p;
		addstate(l, s->out, m, p);
		/* restore old information before returning. */
		m[2*s->data] = save0;
		m[2*s->data+1] = save1;
		break;
	}
}

/*
 * Step the NFA from the states in clist
 * past the character c,
 * to create next NFA state set nlist.
 * Record best match so far in match.
 */
void
step(List *clist, int c, char *p, List *nlist, Sub *match)
{
	int i;
	Thread *t;
	static Sub m[NSUB];

	if(debug){
		dumplist(clist);
		printf("%c (%d)\n", c, c);
	}

	listid++;
	nlist->n = 0;

	for(i=0; i<clist->n; i++){
		t = &clist->t[i];
		switch(t->state->op){
		case Char:
			if(c == t->state->data)
				addstate(nlist, t->state->out, t->match, p);
			break;

		case Any:
			addstate(nlist, t->state->out, t->match, p);
			break;

		case Match:
			if(better(t->match, match))
				memmove(match, t->match, NSUB*sizeof match[0]);
			break;
		}
	}
	
	/* start a new thread */
	if(match == NULL) // || match[0].sp == NULL)
		addstate(nlist, start, m, p);
}

/* Compute initial thread list */
List*
startlist(State *start, char *p, List *l)
{
	List empty = {NULL, 0};
	step(&empty, 0, p, l, NULL);
	return l;
}	

int
match(State *start, char *p, Sub *m)
{
	int c;
	List *clist, *nlist, *t;
	char *q;
	
	q = p+strlen(p);
	clist = startlist(start, q, &l1);
	nlist = &l2;
	memset(m, 0, NSUB*sizeof m[0]);
	while(--q>=p){
		c = *q & 0xFF;
		step(clist, c, q, nlist, m);
		t = clist; clist = nlist; nlist = t;
	}
	step(clist, 0, p, nlist, m);
	return m[0].sp != NULL;
}

void
dump(State *s)
{
	if(s == NULL || s->lastlist == listid)
		return;
	s->lastlist = listid;
	printf("%d| ", s->id);
	switch(s->op){
	case Char:
		printf("'%c' -> %d\n", s->data, s->out->id);
		break;

	case Any:
		printf(". -> %d\n", s->out->id);
		break;

	case Split:
		printf("| -> %d, %d\n", s->out->id, s->out1->id);
		break;
	
	case LParen:
		printf("( %d -> %d\n", s->data, s->out->id);
		break;
	
	case RParen:
		printf(") %d -> %d\n", s->data, s->out->id);
		break;

	case Match:
		printf("match\n");
		break;

	default:
		printf("??? %d\n", s->op);
		break;
	}

	dump(s->out);
	dump(s->out1);
}

int
main(int argc, char **argv)
{
	int i;
	Sub m[NSUB];

	for(;;){
		if(argc > 1 && strcmp(argv[1], "-d") == 0){
			debug++;
			argv[1] = argv[0]; argc--; argv++;
		}
		else
			break;
	}

	if(argc < 3){
		fprintf(stderr, "usage: %s regexp string...\n", argv[0]);
		return 1;
	}
	
	input = argv[1];
	yyparse();
	if(nparen >= MPAREN)
		nparen = MPAREN;
	
	if(debug){
		++listid;
		dump(start);
	}
	
	l1.t = malloc(nstate*sizeof l1.t[0]);
	l2.t = malloc(nstate*sizeof l2.t[0]);
	for(i=2; i<argc; i++){
		text = argv[i];	/* used by printmatch */
		if(match(start, argv[i], m)){
			printf("%s: ", argv[i]);
			printmatch(m, 2);
			printf("\n");
		}
	}
	return 0;
}

/*
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the
 * Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute,
 * sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall
 * be included in all copies or substantial portions of the
 * Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY
 * KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
 * WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
 * PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS
 * OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
