CC=clang
YACC=bison
CFLAGS1=-g -DSTANDALONE -Wall
CFLAGS2=-DDEBUG $(CFLAGS1)
SRCS=thompson_nfa.c

all: libnfa.dylib igrep igrepvm

igrep: $(SRCS)
	$(CC) $(CFLAGS1) $^ -o $@

libnfa.dylib: $(SRCS)
	$(CC) -g -shared $^ -o $@

igrepvm: revmparser.tab.c revm.c 
	$(CC) $(CFLAGS2) $^ -o $@

revmparser.tab.c: revmparser.y 
	$(YACC) -o $@ $^

revm.c: revm.h
revmparser.y: revm.h

debug:  $(SRCS)
	$(CC) $(CFLAGS2) $^ -o igrep

test: igrep
	./igrep 'a?a?a' aaaaaa

thompson_nfa.c: nfa.h

.PHONY: clean

clean:
	rm -rf igrep libnfa.dylib *.dSYM
