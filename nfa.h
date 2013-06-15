#ifndef _NFA_H
#define _NFA_H

struct State_;
struct REprivate_;

typedef struct RE_ {
    struct State_ *start;
    struct REprivate_ *priv;
} RE;


RE re;

// compile rep represented regex into RE_
RE *RE_compile(const char *rep);
// return 1 if matched, else 0
int RE_match(RE *re, const char *str);
void RE_free(RE *re);

#endif
