#include "mymemory.h"

#include <lz4.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(__GLIBC__)
#include <malloc.h>
#endif

#include "../include/nanox.h"

#define PROXY_CHUNK_SIZE 16384
#define COLD_MIN_BYTES 256
#define LZ4_ACCELERATION 4

typedef enum {
    STATE_HOT = 0,
    STATE_COLD = 1
} block_state_t;

struct HandleSlot {
    void *actual_ptr;
    uint32_t raw_size;
    uint32_t comp_size;
    uint32_t ref_count;
    uint8_t state;
    uint8_t is_active;
    uint8_t _padding[2];
    uint64_t last_hot_time;
};

static struct HandleSlot *proxy_table = NULL;
static int proxy_table_capacity = 0;

static int expand_proxy_table(void)
{
    int new_cap = proxy_table_capacity + PROXY_CHUNK_SIZE;
    struct HandleSlot *new_table =
        (struct HandleSlot *)realloc(proxy_table, (size_t)new_cap * sizeof(struct HandleSlot));

    if (new_table == NULL)
        return 0;

    memset(new_table + proxy_table_capacity, 0,
           (size_t)PROXY_CHUNK_SIZE * sizeof(struct HandleSlot));
    proxy_table = new_table;
    proxy_table_capacity = new_cap;
    return 1;
}

static struct HandleSlot *allocate_slot(void *actual, size_t size)
{
    int i;

    if (proxy_table == NULL)
        mymemory_init();

    for (i = 0; i < proxy_table_capacity; ++i) {
        if (!proxy_table[i].is_active) {
            proxy_table[i].actual_ptr = actual;
            proxy_table[i].raw_size = (uint32_t)size;
            proxy_table[i].comp_size = 0;
            proxy_table[i].ref_count = 1;
            proxy_table[i].state = STATE_HOT;
            proxy_table[i].is_active = 1;
            proxy_table[i].last_hot_time = (uint64_t)time(NULL);
            return &proxy_table[i];
        }
    }

    if (!expand_proxy_table())
        return NULL;
    return allocate_slot(actual, size);
}

static int thaw_handle(struct HandleSlot *slot)
{
    char *hot_buf;
    int rc;

    if (slot == NULL || !slot->is_active)
        return 0;
    if (slot->state == STATE_HOT)
        return 1;

    hot_buf = (char *)malloc(slot->raw_size == 0 ? 1u : (size_t)slot->raw_size);
    if (hot_buf == NULL)
        return 0;

    rc = LZ4_decompress_safe((const char *)slot->actual_ptr, hot_buf,
                             (int)slot->comp_size, (int)slot->raw_size);
    if (rc < 0 || (uint32_t)rc != slot->raw_size) {
        free(hot_buf);
        return 0;
    }

    free(slot->actual_ptr);
    slot->actual_ptr = hot_buf;
    slot->comp_size = 0;
    slot->state = STATE_HOT;
    slot->last_hot_time = (uint64_t)time(NULL);
    return 1;
}

void mymemory_init(void)
{
    if (proxy_table == NULL)
        (void)expand_proxy_table();
}

MemoryHandle my_handle_alloc(size_t size)
{
    void *actual;
    struct HandleSlot *slot;
    size_t alloc_size = size == 0 ? 1u : size;

    actual = malloc(alloc_size);
    if (actual == NULL)
        return NULL;

    slot = allocate_slot(actual, size);
    if (slot == NULL) {
        free(actual);
        return NULL;
    }
    return slot;
}

MemoryHandle my_handle_calloc(size_t nmemb, size_t size)
{
    void *actual;
    struct HandleSlot *slot;
    size_t total = nmemb * size;
    size_t alloc_size = total == 0 ? 1u : total;

    actual = calloc(1, alloc_size);
    if (actual == NULL)
        return NULL;

    slot = allocate_slot(actual, total);
    if (slot == NULL) {
        free(actual);
        return NULL;
    }
    return slot;
}

MemoryHandle my_handle_realloc(MemoryHandle h, size_t size)
{
    struct HandleSlot *slot = (struct HandleSlot *)h;
    void *new_ptr;
    size_t alloc_size = size == 0 ? 1u : size;

    if (slot == NULL)
        return my_handle_alloc(size);
    if (!slot->is_active)
        return NULL;
    if (!thaw_handle(slot))
        return NULL;

    new_ptr = realloc(slot->actual_ptr, alloc_size);
    if (new_ptr == NULL)
        return NULL;

    slot->actual_ptr = new_ptr;
    slot->raw_size = (uint32_t)size;
    slot->comp_size = 0;
    slot->state = STATE_HOT;
    slot->last_hot_time = (uint64_t)time(NULL);
    return slot;
}

void my_handle_ref(MemoryHandle h)
{
    struct HandleSlot *slot = (struct HandleSlot *)h;

    if (slot != NULL && slot->is_active)
        slot->ref_count++;
}

void my_handle_free(MemoryHandle h)
{
    struct HandleSlot *slot = (struct HandleSlot *)h;

    if (slot == NULL || !slot->is_active)
        return;
    if (slot->ref_count > 1) {
        slot->ref_count--;
        return;
    }

    free(slot->actual_ptr);
    memset(slot, 0, sizeof(*slot));
}

int my_handle_ref_count(MemoryHandle h)
{
    struct HandleSlot *slot = (struct HandleSlot *)h;

    if (slot == NULL || !slot->is_active)
        return 0;
    return (int)slot->ref_count;
}

void *handle_deref(MemoryHandle h)
{
    struct HandleSlot *slot = (struct HandleSlot *)h;

    if (slot == NULL || !slot->is_active)
        return NULL;
    if (!thaw_handle(slot))
        return NULL;

    slot->last_hot_time = (uint64_t)time(NULL);
    return slot->actual_ptr;
}

int mymemory_freeze(void *p)
{
    struct HandleSlot *slot = (struct HandleSlot *)p;
    char *comp_buf;
    char *shrunk_buf;
    int max_comp;
    int actual_comp;

    if (slot == NULL || !slot->is_active || slot->state == STATE_COLD)
        return 0;
    if (slot->raw_size < COLD_MIN_BYTES)
        return 0;

    max_comp = LZ4_compressBound((int)slot->raw_size);
    comp_buf = (char *)malloc((size_t)max_comp);
    if (comp_buf == NULL)
        return 0;

    actual_comp = LZ4_compress_fast((const char *)slot->actual_ptr, comp_buf,
                                    (int)slot->raw_size, max_comp,
                                    LZ4_ACCELERATION);
    if (actual_comp <= 0 || (uint32_t)actual_comp >= slot->raw_size) {
        free(comp_buf);
        return 0;
    }

    shrunk_buf = (char *)realloc(comp_buf, (size_t)actual_comp);
    if (shrunk_buf == NULL)
        shrunk_buf = comp_buf;

    free(slot->actual_ptr);
    slot->actual_ptr = shrunk_buf;
    slot->comp_size = (uint32_t)actual_comp;
    slot->state = STATE_COLD;
    return 1;
}

void mymemory_freeze_timeout(void)
{
    int i;
    int froze_any = 0;
    uint64_t now;

    if (proxy_table == NULL || nanox_cfg.cold_storage_timeout <= 0)
        return;

    now = (uint64_t)time(NULL);
    for (i = 0; i < proxy_table_capacity; ++i) {
        if (!proxy_table[i].is_active || proxy_table[i].state != STATE_HOT)
            continue;
        if (now < proxy_table[i].last_hot_time)
            continue;
        if ((now - proxy_table[i].last_hot_time) <=
            (uint64_t)nanox_cfg.cold_storage_timeout)
            continue;
        froze_any |= mymemory_freeze(&proxy_table[i]);
    }

#if defined(__GLIBC__)
    if (froze_any)
        malloc_trim(0);
#else
    (void)froze_any;
#endif
}

void mymemory_compact(void)
{
    /* Hot/cold storage now uses malloc-backed blocks, so there is no arena
       compaction step beyond the allocator's own trimming behavior. */
}

void mymemory_shutdown(void)
{
    int i;

    if (proxy_table != NULL) {
        for (i = 0; i < proxy_table_capacity; ++i) {
            if (proxy_table[i].is_active)
                free(proxy_table[i].actual_ptr);
        }
        free(proxy_table);
        proxy_table = NULL;
        proxy_table_capacity = 0;
    }
}
