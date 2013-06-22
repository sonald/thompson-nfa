Thompson NFA implementation
===

### this is a DFA/NFA based regex matcher inspired by [Russ Cox][1]. 
I do make some modifications:

+ instead of convert re into postfix representation, I use a recursive descend parser.
+ wrap all temporary storages into a structure, so I can free resources later.

### I have included some of implementations from the article:

+ russ\_nfa.c NFA version
+ dfa0.c creating DFA state on-the-fly
+ dfa1.c use a bounded memory for caching DFA states
+ nfa-posix.y use a reversed NFA, and backward matching

plus, `nfa-posix.y` has a bug in **yylex** function. `+` should be considered as a metacharacter.

[1]: http://swtch.com/~rsc/regexp/regexp1.html
