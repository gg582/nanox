#include "mymemory.h"
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h> // For sbrk (though we use a static buffer)
#include <sys/mman.h> // For mmap if needed

#define MEMORY_POOL_SIZE (1024 * 1024) // 1MB pool

// Structure for memory block metadata, stored *before* the user data
typedef struct MemoryBlock {
    size_t size;        // Size of the user data part
    struct MemoryBlock *next_free; // Pointer to the next free block (if this block is free)
    char is_free;       // Flag to indicate if the block is free
} MemoryBlock;

static char memory_pool[MEMORY_POOL_SIZE]; // The actual memory pool
static MemoryBlock *free_list_head = NULL;
static size_t total_allocated = 0;

// Helper function to get the MemoryBlock header for a given user pointer
static MemoryBlock *get_block_from_ptr(void *ptr) {
    if (ptr == NULL) return NULL;
    return (MemoryBlock *)((char *)ptr - sizeof(MemoryBlock));
}

// Helper function to get the user data pointer from a MemoryBlock
static void *get_ptr_from_block(MemoryBlock *block) {
    if (block == NULL) return NULL;
    return (void *)((char *)block + sizeof(MemoryBlock));
}

void mymemory_init(void) {
    // Initialize the memory pool by creating a single large free block
    free_list_head = (MemoryBlock *)memory_pool;
    free_list_head->size = MEMORY_POOL_SIZE - sizeof(MemoryBlock); // Size is for user data
    free_list_head->next_free = NULL;
    free_list_head->is_free = 1;
    total_allocated = 0;
}

void *mymalloc(size_t size) {
    if (size == 0) {
        return NULL;
    }

    // Ensure alignment: align to a multiple of 8 bytes
    size = (size + 7) & ~7;

    MemoryBlock *current = free_list_head;
    MemoryBlock *previous = NULL;

    // Find the first fit block that is large enough
    while (current != NULL) {
        if (current->is_free && current->size >= size) {
            // Found a suitable block

            // Check if splitting the block is worthwhile
            // We need enough space for the allocated block (size + header) AND at least one FreeBlock header
            if (current->size >= size + sizeof(MemoryBlock)) {
                // Split the block
                MemoryBlock *new_free_block = (MemoryBlock *)((char *)current + sizeof(MemoryBlock) + size);
                new_free_block->size = current->size - size - sizeof(MemoryBlock);
                new_free_block->next_free = current->next_free;
                new_free_block->is_free = 1;

                // Update the free list pointers
                if (previous == NULL) {
                    free_list_head = new_free_block;
                } else {
                    previous->next_free = new_free_block;
                }
                current->size = size; // Update the size of the allocated block
            } else {
                // Use the whole block, it's not large enough to split optimally
                if (previous == NULL) {
                    free_list_head = current->next_free;
                } else {
                    previous->next_free = current->next_free;
                }
            }

            current->is_free = 0;
            current->next_free = NULL; // Mark as not free
            total_allocated += size;
            // Zero out the allocated memory (as per calloc behavior, good practice)
            memset(get_ptr_from_block(current), 0, size);
            return get_ptr_from_block(current);
        }
        previous = current;
        current = current->next_free;
    }

    // No suitable block found
    fprintf(stderr, "mymalloc: failed to allocate %zu bytes (pool exhausted or fragmented)\n", size);
    return NULL;
}

void myfree(void *ptr) {
    if (ptr == NULL) {
        return;
    }

    MemoryBlock *block_to_free = get_block_from_ptr(ptr);
    if (!block_to_free || block_to_free->is_free) {
        // Attempt to free invalid pointer or already freed memory
        fprintf(stderr, "myfree: invalid pointer or double free detected\n");
        return;
    }

    block_to_free->is_free = 1;
    // Add to the front of the free list. Merging would be a further optimization.
    block_to_free->next_free = free_list_head;
    free_list_head = block_to_free;
    total_allocated -= block_to_free->size;
}

void *mycalloc(size_t nmemb, size_t size) {
    size_t total_size = nmemb * size;
    void *ptr = mymalloc(total_size);
    if (ptr != NULL) {
        memset(ptr, 0, total_size);
    }
    return ptr;
}

void *myrealloc(void *ptr, size_t size) {
    if (ptr == NULL) {
        return mymalloc(size);
    }
    if (size == 0) {
        myfree(ptr);
        return NULL;
    }

    MemoryBlock *block = get_block_from_ptr(ptr);
    size_t old_size = block->size;

    // If new size is smaller and block is large enough to split, try splitting
    if (size < old_size && old_size >= size + sizeof(MemoryBlock)) {
        // Split the block
        MemoryBlock *new_free_block = (MemoryBlock *)((char *)block + sizeof(MemoryBlock) + size);
        new_free_block->size = old_size - size - sizeof(MemoryBlock);
        new_free_block->next_free = block->next_free;
        new_free_block->is_free = 1;
        block->size = size;
        // Update free list - this is tricky as block might not be at head
        // For simplicity, we'll just add the new free block to the head
        new_free_block->next_free = free_list_head;
        free_list_head = new_free_block;

        total_allocated -= (old_size - size - sizeof(MemoryBlock)); // Adjust total allocated
        return ptr; // Return original pointer, as it was just resized in place
    }

    // If new size is larger or cannot split, allocate new, copy, free old
    void *new_ptr = mymalloc(size);
    if (new_ptr == NULL) {
        return NULL; // Failed to reallocate
    }

    // Copy data from old block to new block
    memcpy(new_ptr, ptr, old_size < size ? old_size : size);
    myfree(ptr);
    return new_ptr;
}

