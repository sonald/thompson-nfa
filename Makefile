CC=clang
CFLAGS1=-g -DSTANDALONE
CFLAGS2=-g -DDEBUG
SRCS=thompson_nfa.c

all: libnfa.dylib igrep

igrep: $(SRCS)
	$(CC) $(CFLAGS1) $^ -o $@

libnfa.dylib: $(SRCS)
	$(CC) -g -shared $^ -o $@

test: igrep
	./igrep 'a?a?a' aaaaaa

thompson_nfa.c: nfa.h

.PHONY: clean

clean:
	rm -rf igrep libnfa.dylib *.dSYM
