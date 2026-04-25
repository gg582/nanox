#include "mymemory.h"
#include <lz4.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>
#include "../include/nanox.h"

#define ARENA_INITIAL_SIZE (1024 * 1024 * 8)
#define CHUNK_SIZE (128 * 1024)

typedef enum { STATE_HOT, STATE_COLD } block_state_t;

struct HandleSlot {
    void* actual_ptr;       /* 8 bytes */
    uint32_t raw_size;      /* 4 bytes */
    uint32_t comp_size;     /* 4 bytes */
    uint32_t ref_count;     /* 4 bytes */
    uint8_t state;          /* 1 byte */
    uint8_t is_active;      /* 1 byte */
    uint8_t _padding[2];    /* 2 bytes */
    uint64_t last_hot_time; /* 8 bytes */
};

typedef struct BlockHeader {
    struct HandleSlot *handle; /* 8 bytes */
    uint32_t size;             /* 4 bytes */
    uint32_t is_free;          /* 4 bytes */
} BlockHeader;

#define HEADER_SIZE (sizeof(BlockHeader))
#define FOOTER_SIZE (sizeof(uint32_t))
#define MIN_BLOCK_SIZE (HEADER_SIZE + FOOTER_SIZE + 16)

typedef struct Arena {
    struct Arena *next;
    size_t total_size;
    size_t used_count;
    char *data;
} Arena;

static Arena *arena_head = NULL;
static struct HandleSlot *proxy_table = NULL;
#define MAX_PROXIES 262144

void mymemory_init(void) {
    if (!proxy_table) {
        proxy_table = (struct HandleSlot*)calloc(MAX_PROXIES, sizeof(struct HandleSlot));
    }
}

static struct HandleSlot* get_new_proxy(void* actual, size_t size) {
    for (int i = 0; i < MAX_PROXIES; i++) {
        if (!proxy_table[i].is_active) {
            proxy_table[i].is_active = 1;
            proxy_table[i].actual_ptr = actual;
            proxy_table[i].raw_size = (uint32_t)size;
            proxy_table[i].state = STATE_HOT;
            proxy_table[i].comp_size = 0;
            proxy_table[i].ref_count = 1;
            proxy_table[i].last_hot_time = (uint64_t)time(NULL);
            return &proxy_table[i];
        }
    }
    return NULL;
}

static Arena* create_arena(size_t req) {
    size_t s = (req + sizeof(Arena) + 4096) & ~4095;
    if (s < ARENA_INITIAL_SIZE) s = ARENA_INITIAL_SIZE;
    
    Arena *a = (Arena*)malloc(sizeof(Arena));
    if (!a) return NULL;
    a->data = (char*)malloc(s);
    if (!a->data) { free(a); return NULL; }
    
    a->total_size = s;
    a->used_count = 0;
    a->next = arena_head;
    arena_head = a;

    BlockHeader *h = (BlockHeader*)a->data;
    h->size = (uint32_t)s;
    h->is_free = 1;
    h->handle = NULL;
    *(uint32_t*)(a->data + s - FOOTER_SIZE) = (uint32_t)s;
    
    return a;
}

static void* arena_alloc(Arena *a, size_t size, struct HandleSlot **out_slot) {
    size_t total_needed = (size + HEADER_SIZE + FOOTER_SIZE + 7) & ~7;
    char *curr = a->data;
    while (curr < a->data + a->total_size) {
        BlockHeader *h = (BlockHeader*)curr;
        if (h->is_free && h->size >= total_needed) {
            if (h->size >= total_needed + MIN_BLOCK_SIZE) {
                uint32_t old_size = h->size;
                h->size = (uint32_t)total_needed;
                h->is_free = 0;
                
                BlockHeader *next_h = (BlockHeader*)(curr + total_needed);
                next_h->size = old_size - (uint32_t)total_needed;
                next_h->is_free = 1;
                next_h->handle = NULL;
                *(uint32_t*)(curr + total_needed + next_h->size - FOOTER_SIZE) = next_h->size;
            } else {
                h->is_free = 0;
            }
            *(uint32_t*)(curr + h->size - FOOTER_SIZE) = h->size;
            
            a->used_count++;
            struct HandleSlot *slot = get_new_proxy(curr + HEADER_SIZE, size);
            h->handle = slot;
            *out_slot = slot;
            return curr + HEADER_SIZE;
        }
        curr += h->size;
    }
    return NULL;
}

void* mymalloc(size_t s) {
    if (!proxy_table) mymemory_init();
    struct HandleSlot *slot = NULL;
    for (Arena *a = arena_head; a; a = a->next) {
        void* p = arena_alloc(a, s, &slot);
        if (p) return (void*)slot;
    }
    Arena *new_a = create_arena(s + HEADER_SIZE + FOOTER_SIZE);
    if (!new_a) return NULL;
    return (void*)arena_alloc(new_a, s, &slot);
}

void myfree(void* p) {
    if (!p) return;
    struct HandleSlot *slot = (struct HandleSlot*)p;
    if (!slot->is_active) return;

    if (slot->ref_count > 1) {
        slot->ref_count--;
        return;
    }
    slot->ref_count = 0;

    if (slot->state == STATE_COLD) {
        free(slot->actual_ptr);
        slot->is_active = 0;
        return;
    }

    BlockHeader *h = (BlockHeader*)((char*)slot->actual_ptr - HEADER_SIZE);
    h->is_free = 1;
    h->handle = NULL;
    slot->is_active = 0;

    for (Arena *a = arena_head; a; a = a->next) {
        if ((char*)h >= a->data && (char*)h < a->data + a->total_size) {
            a->used_count--;
            break;
        }
    }
}

void my_handle_ref(MemoryHandle h) {
    if (!h) return;
    struct HandleSlot *slot = (struct HandleSlot*)h;
    if (slot->is_active) slot->ref_count++;
}

int my_handle_ref_count(MemoryHandle h) {
    if (!h) return 0;
    struct HandleSlot *slot = (struct HandleSlot*)h;
    return slot->is_active ? (int)slot->ref_count : 0;
}

void mymemory_compact(void) {
    Arena **curr_a = &arena_head;
    while (*curr_a) {
        Arena *a = *curr_a;
        if (a->used_count == 0) {
            *curr_a = a->next;
            free(a->data);
            free(a);
            continue;
        }

        char *p = a->data;
        while (p < a->data + a->total_size) {
            BlockHeader *h = (BlockHeader*)p;
            if (h->is_free) {
                char *next_p = p + h->size;
                while (next_p < a->data + a->total_size) {
                    BlockHeader *next_h = (BlockHeader*)next_p;
                    if (next_h->is_free) {
                        h->size += next_h->size;
                        *(uint32_t*)(p + h->size - FOOTER_SIZE) = h->size;
                        next_p = p + h->size;
                    } else break;
                }
            }
            p += h->size;
        }
        curr_a = &((*curr_a)->next);
    }
}

void* restrict _mymemory_deref(void* handle) {
    if (!handle) return NULL;
    struct HandleSlot *s = (struct HandleSlot*)handle;
    s->last_hot_time = (uint64_t)time(NULL);
    if (s->state == STATE_HOT) return s->actual_ptr;

    void *new_hot_handle = mymalloc(s->raw_size);
    if (!new_hot_handle) return NULL;
    
    struct HandleSlot *new_s = (struct HandleSlot*)new_hot_handle;
    LZ4_decompress_safe((char*)s->actual_ptr, (char*)new_s->actual_ptr, (int)s->comp_size, (int)s->raw_size);
    
    free(s->actual_ptr);
    s->actual_ptr = new_s->actual_ptr;
    s->state = STATE_HOT;
    
    BlockHeader *h = (BlockHeader*)((char*)s->actual_ptr - HEADER_SIZE);
    h->handle = s;
    new_s->is_active = 0;
    
    return s->actual_ptr;
}

int mymemory_freeze(void* p) {
    if (!p) return 0;
    struct HandleSlot *s = (struct HandleSlot*)p;
    if (s->state == STATE_COLD || !s->is_active) return 0;

    int max_comp = LZ4_compressBound((int)s->raw_size);
    char *comp_buf = (char*)malloc(max_comp);
    if (!comp_buf) return 0;

    int actual_comp = LZ4_compress_default((char*)s->actual_ptr, comp_buf, (int)s->raw_size, max_comp);
    
    if (actual_comp > 0) {
        void *old_ptr = s->actual_ptr;
        s->actual_ptr = realloc(comp_buf, actual_comp);
        if (!s->actual_ptr) s->actual_ptr = comp_buf;
        s->comp_size = (uint32_t)actual_comp;
        s->state = STATE_COLD;

        BlockHeader *h = (BlockHeader*)((char*)old_ptr - HEADER_SIZE);
        h->is_free = 1;
        h->handle = NULL;
        
        for (Arena *a = arena_head; a; a = a->next) {
            if ((char*)h >= a->data && (char*)h < a->data + a->total_size) {
                a->used_count--;
                break;
            }
        }
        return 1;
    }
    free(comp_buf);
    return 0;
}

MemoryHandle my_handle_alloc(size_t size) { return (MemoryHandle)mymalloc(size); }
MemoryHandle my_handle_calloc(size_t nmemb, size_t size) { 
    MemoryHandle h = (MemoryHandle)mymalloc(nmemb * size);
    if (h) memset(handle_deref(h), 0, nmemb * size);
    return h;
}
MemoryHandle my_handle_realloc(MemoryHandle h, size_t size) {
    if (!h) return my_handle_alloc(size);
    MemoryHandle nh = my_handle_alloc(size);
    if (nh) {
        struct HandleSlot *os = (struct HandleSlot*)h;
        size_t cp = os->raw_size < size ? os->raw_size : size;
        memcpy(handle_deref(nh), handle_deref(h), cp);
        my_handle_free(h);
    }
    return nh;
}
void my_handle_free(MemoryHandle h) { myfree((void*)h); }
void* restrict handle_deref(MemoryHandle h) { return _mymemory_deref((void*)h); }

void mymemory_freeze_timeout(void) {
    if (nanox_cfg.cold_storage_timeout <= 0) return;
    uint64_t now = (uint64_t)time(NULL);
    for (int i = 0; i < MAX_PROXIES; i++) {
        if (proxy_table[i].is_active && proxy_table[i].state == STATE_HOT) {
            if (now >= proxy_table[i].last_hot_time && 
                (now - proxy_table[i].last_hot_time) > (uint64_t)nanox_cfg.cold_storage_timeout) {
                mymemory_freeze(&proxy_table[i]);
            }
        }
    }
}

void mymemory_shutdown(void) {
    while (arena_head) {
        Arena *a = arena_head;
        arena_head = a->next;
        free(a->data);
        free(a);
    }
    if (proxy_table) {
        for (int i = 0; i < MAX_PROXIES; i++) {
            if (proxy_table[i].is_active && proxy_table[i].state == STATE_COLD) {
                free(proxy_table[i].actual_ptr);
            }
        }
        free(proxy_table);
        proxy_table = NULL;
    }
}
