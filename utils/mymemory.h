#ifndef MYMEMORY_H
#define MYMEMORY_H

#include <stddef.h>

/*
 * MemoryHandle: An opaque pointer provided to the user.
 * Internally, it maps to a HandleSlot structure. All data access must
 * go through handle_deref() to ensure safety across memory compactions.
 */
typedef struct HandleSlot* MemoryHandle;

/* Initialize and shutdown the custom memory management system */
void mymemory_init(void);
void mymemory_shutdown(void);

/* Handle-based allocation interfaces */
MemoryHandle my_handle_alloc(size_t size);
MemoryHandle my_handle_calloc(size_t nmemb, size_t size);
MemoryHandle my_handle_realloc(MemoryHandle h, size_t size);
void my_handle_free(MemoryHandle h);

/* 
 * Dereference macro/function for data access.
 * Triggers lazy LZ4 decompression if the memory block is currently COLD.
 * ALWAYS use this right before accessing the data. DO NOT cache the returned pointer.
 */
void* restrict handle_deref(MemoryHandle h);

/* 
 * Serialize a HOT memory block into a COLD (LZ4 compressed) state.
 * Returns 1 on success, 0 on failure (e.g., no compression benefit).
 */
int mymemory_freeze(void *p);

/* Force trigger memory compaction (sliding) for optimization/debugging */
void mymemory_compact(void);

/* 
 * Prohibit the use of standard memory library functions in the application
 * to enforce the handle-based architecture.
 */
#undef malloc
#undef free
#undef calloc
#undef realloc

#endif // MYMEMORY_H