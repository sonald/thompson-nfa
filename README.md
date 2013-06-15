Thompson NFA implementation

this is a NFA based regex matcher inspired by [Russ Cox][1]. 
I do make some modifications:
+ instead of convert re into postfix representation, I use a recursive descend parser.
+ wrap all temporary storages into a structure, so I can free resources later.

1: http://swtch.com/~rsc/regexp/regexp1.html
