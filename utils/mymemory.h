#ifndef MYMEMORY_H
#define MYMEMORY_H

#include <stddef.h>

void mymemory_init(void);
void *mymalloc(size_t size);
void myfree(void *ptr);
void *mycalloc(size_t nmemb, size_t size);
void *myrealloc(void *ptr, size_t size);

#undef malloc
#undef free
#undef calloc
#undef realloc

#define malloc mymalloc
#define free myfree
#define calloc mycalloc
#define realloc myrealloc

#endif
