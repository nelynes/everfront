// memcpy/memset for the freestanding build (gcc emits calls to them).
// Built with -fno-tree-loop-distribute-patterns so these loops are not
// turned back into calls to themselves.
typedef unsigned int size_t;

void *memcpy(void *dst, const void *src, size_t n) {
    unsigned char *d = dst;
    const unsigned char *s = src;
    while (n--) *d++ = *s++;
    return dst;
}

void *memset(void *dst, int c, size_t n) {
    unsigned char *d = dst;
    while (n--) *d++ = (unsigned char) c;
    return dst;
}

int memcmp(const void *a, const void *b, size_t n) {
    const unsigned char *x = a, *y = b;
    for (; n; n--, x++, y++) if (*x != *y) return *x - *y;
    return 0;
}

char *strchr(const char *s, int c) {
    for (;; s++) {
        if (*s == (char) c) return (char *) s;
        if (!*s) return 0;
    }
}
