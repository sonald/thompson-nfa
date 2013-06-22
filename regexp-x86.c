/*
 * x86 implementation of Thompson's
 * on-the-fly regular expression compiler.
 *
 * See also Thompson, Ken.  Regular Expression Search Algorithm,
 * Communications of the ACM 11(6) (June 1968), pp. 419-422.
 * 
 * Copyright (c) 2004 Jan Burgy.
 * Can be distributed under the MIT license, see bottom of file.
 */

#define _XOPEN_SOURCE 1000
#include <stdio.h>
#include <limits.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <stdlib.h>

enum	{
	LPAREN = CHAR_MAX + 1,
	RPAREN,		/* This should	*/
	ALTERN,		/* reflect the	*/
	CONCAT,		/* precedence	*/
	KLEENE		/* rules!	*/
};

static
unsigned char *prepare(const char *src)
{
	unsigned char	escape[CHAR_MAX + 1] = "";
	unsigned char	*dest = malloc(2 * (strlen(src) + 1));
	int	c, i, j = 0, concat = 0, nparen = 0;

	escape['a'] = '\a';
	escape['b'] = '\b';
	escape['f'] = '\f';
	escape['n'] = '\n';
	escape['r'] = '\r';
	escape['t'] = '\t';
	escape['v'] = '\v';
	for (i = 0; (c = "\"()*\\|"[i]); i++)
		escape[c] = c;
	
	for (i = 0; (c = src[i]); i++) {
		
		switch (c) {

			case '(':
				dest[j++] = LPAREN;
				concat = 0;
				nparen++;
				continue;
			case ')':
				dest[j++] = RPAREN;
				nparen--;
				break;
			case '*':
				dest[j++] = KLEENE;
				break;
			case '|':
				dest[j++] = ALTERN;
				concat = 0;
				continue;
			case '\\':
				c = escape[(int)src[i + 1]];
				c ? i++ : (c = '\\');
			default:
				if (concat)
					dest[j++] = CONCAT;
				dest[j++] = c;

		}
		concat = 1;
		if (nparen < 0)
			printf("unbalanced parentheses\n");
		
	}
	dest[j++] = RPAREN;
	dest[j++] = '\0';

	return	dest;
}

static
unsigned char *convert (const char *src)
{	/* http://cs.lasierra.edu/~ehwang/cptg454/postfix.pdf */
	unsigned char	stack[BUFSIZ] = "";
	unsigned char	*dest = prepare(src);
	int	c, i, j = 0, top = 0;

	stack[top++] = LPAREN;
	for (i = 0; (c = dest[i]); i++) {

		switch (c) {

			case LPAREN:
				stack[top++] = c;
				break;

			case RPAREN:
			case ALTERN:
			case CONCAT:
			case KLEENE:
				while (c <= stack[top - 1])
					dest[j++] = stack[--top];
				if (c == RPAREN)
					--top;	/* discard LPAREN */
				else
					stack[top++] = c;
				break;

			default:
				dest[j++] = c;
				break;

		}

	}
	dest[j++] = '\0';

	return	dest;
}

static
void *xalloc(size_t size)
{
	void	*p;
	size_t	pagesize = sysconf(_SC_PAGESIZE);

	size = (size + pagesize - 1) & ~(pagesize - 1);
	if (posix_memalign(&p, pagesize, size))
		return	NULL;
	mprotect(p, size, PROT_READ|PROT_WRITE|PROT_EXEC);
	return	p;	
}

static
unsigned char header[] = {
	0xC8, 0x94, 0x10, 0x00,				/*	enter	$400, $0		*/
	0x8B, 0x55, 0x08,				/* 	movl	8(%ebp), %edx		*/
	0xB8, 0xFF, 0x00, 0x00, 0x00,			/*	movl	$0xff, %eax		*/
	0x31, 0xC9,					/* 	xorl	%ecx, %ecx		*/
	0xE8, 0x00, 0x00, 0x00, 0x00,			/*	call	_next			*/
							/*_next:				*/
	0x83, 0x2C, 0x24, 0x05,				/*	sub	$5, (%esp)		*/
	0xA8, 0xFF,					/*	test	%al			*/
	0x75, 0x02,					/*	jnz	_L1			*/
	0xC9,						/*	leave				*/
	0xC3,						/*	ret				*/
							/*_L1:					*/
	0xE3, 0x0A,					/* 	jecxz	_L2			*/
	0x49,						/* 	decl	%ecx			*/
	0xFF, 0xB4, 0x8D, 0x70, 0xFE, 0xFF, 0xFF,	/* 	pushl	-400(%ebp,%ecx,4)	*/
	0xEB, 0xF4,					/* 	jmp	_L1			*/
							/*_L2:					*/
	0x8A, 0x02,					/* 	movb	(%edx), %al		*/
	0x42,						/* 	incl	%edx			*/
	0xE8, 0x0A, 0x00, 0x00, 0x00,			/* 	call	_code			*/
							/*_fail:				*/
	0xC3,						/* 	ret				*/
							/*_nnode:				*/
	0x8F, 0x84, 0x8D, 0x70, 0xFE, 0xFF, 0xFF,	/* 	popl	-400(%ebp,%ecx,4)	*/
	0x41,						/* 	incl	%ecx			*/
	0xC3,						/* 	ret				*/
};

static
unsigned char footer[] = {
	0x4A,						/*	decl	%edx			*/
	0x89, 0xD0,					/*	mov	%edx, %eax		*/
	0xC9,						/*	leave				*/
	0xC3,						/*	ret				*/
};

typedef	char *(*function_t)(char *);

static
int codelen(const unsigned char *src)
{
	int	i, c, n = 0;

	for (i = 0; (c = src[i]); i++) {
		switch (c) {
			default:	n += 11;	break;
			case CONCAT:			break;
			case KLEENE:	n +=  5;	break;
			case ALTERN:	n +=  9;	break;
		}
	}
	return	n;
}

enum	{
	CMP	= 0x3C,
	JNZ	= 0x75,
	CALL	= 0xE8,
	JMP	= 0xEB
};

unsigned char *compile(const unsigned char *src)
{
	int	i, c, pc = sizeof header, top = 0;
	unsigned long	stack[BUFSIZ], tmp, fail = 0x31, nnode = 0x32;
	unsigned long	length = sizeof header + codelen(src) + sizeof footer;
	unsigned char	*code = xalloc(length);

	memmove(code, header, sizeof header);
	for (i = 0; (c = src[i]); i++) {

		switch (c) {

			default:
				stack[top] = pc + 1;
				code[pc + 0] = JMP;	code[pc + 1] = 0x00;
				code[pc + 2] = CMP;	code[pc + 3] = c;
				code[pc + 4] = JNZ;	code[pc + 5] = (fail - (pc + 6)) & 0xFF;
				tmp = nnode - (pc + 11);
				code[pc + 6] = CALL;	memcpy(code + pc + 7, &tmp, sizeof tmp);
				top += 1;
				pc += 11;
				break;

			case CONCAT:
				--top;
				break;

			case KLEENE:
				tmp = code[stack[top - 1]] + stack[top - 1] - (pc + 6);
				code[pc + 0] = CALL;	memcpy(code + pc + 1, &tmp, sizeof tmp);
				code[stack[top - 1]] = (pc - 1 - stack[top - 1]) & 0xFF;
				pc += 5;
				break;

			case ALTERN:
				code[pc + 0] = JMP;
				code[pc + 1] = 0x07;
				tmp = code[stack[top - 1]] + stack[top - 1] - (pc + 6);
				code[pc + 2] = CALL;	memcpy(code + pc + 3, &tmp, sizeof tmp);
				tmp = code[stack[top - 2]] + stack[top - 2] - (pc + 8);
				code[pc + 7] = JMP;	code[pc + 8] = tmp & 0xFF;
				code[stack[top - 2]] = (pc + 3 - stack[top - 2]) & 0xFF;
				code[stack[top - 1]] = (pc + 8 - stack[top - 1]) & 0xFF;
				pc += 9;
				--top;
				break;

		}

	}
	memmove(code + pc, footer, sizeof footer);

	return	code;
}

function_t study(const char *re)
{
	unsigned char	*p = convert(re);
	unsigned char	*q = compile(p);

	if (p) free (p), p = NULL;
	return	(function_t)q;
}

int main(void)
{
	short	i;
	struct	{
		char	*r;
		char	*s;
	} test[] = {
		/*
		{ "abcdefg",	"abcdefg"	},
		{ "(a|b)*a",	"ababababab"	},
		{ "(a|b)*a",	"aaaaaaaaba"	},
		{ "(a|b)*a",	"aaaaaabac"	},
		{ "a(b|c)*d",	"abccbcccd"	},
		{ "a(b|c)*d",	"abccbcccde"	},
		*/
		{ "a(b|c)*d",	"abcccccccc"	},
		/*
		{ "a(b|c)*d",	"abcd"		},
		*/
		{ NULL,		NULL		}
	};

	for (i = 0; test[i].r; i++) {
		function_t search = study(test[i].r);
		char	*t;

		printf("search %s %s\n", test[i].r, test[i].s);
		t = (*search)(test[i].s);
		if (t)	printf("match found after %d bytes\n", t - test[i].s);
		else	printf("match not found\n");
		free((void *)search);
	}

	return	0;
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
