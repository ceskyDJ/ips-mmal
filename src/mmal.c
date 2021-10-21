/**
 * Implementace My MALloc
 * Demonstracni priklad pro 1. ukol IPS/2021
 *
 * @author Ales Smrcka
 * @author Michal Smahel (xsmahe01)
 */

#include "mmal.h"
#include <sys/mman.h> // mmap
#include <stdbool.h> // bool
#include <assert.h> // assert
#include <string.h> // memcpy

#ifdef NDEBUG
/**
 * The structure header encapsulates data of a single memory block.
 */
/*
 *   ---+------+----------------------------+---
 *      |Header|DDD not_free DDDDD...free...|
 *   ---+------+-----------------+----------+---
 *             |-- Header.asize -|
 *             |-- Header.size -------------|
 */
typedef struct header Header;
struct header {

    /**
     * Pointer to the next header. Cyclic list. If there is no other block,
     * points to itself.
     */
    Header *next;

    /// size of the block
    size_t size;

    /**
     * Size of block in bytes allocated for program. asize=0 means the block 
     * is not used by a program.
     */
    size_t asize;
};

/**
 * The arena structure.
 */
/*
 *   /--- arena metadata
 *   |     /---- header of the first block
 *   v     v
 *   +-----+------+-----------------------------+
 *   |Arena|Header|.............................|
 *   +-----+------+-----------------------------+
 *
 *   |--------------- Arena.size ---------------|
 */
typedef struct arena Arena;
struct arena {

    /**
     * Pointer to the next arena. Single-linked list.
     */
    Arena *next;

    /// Arena size.
    size_t size;
};

/**
 * Alignment size for arenas
 */
#define PAGE_SIZE (128*1024)

#endif // NDEBUG

/**
 * mmap's protection flags
 */
#define MMAP_PROT (PROT_READ|PROT_WRITE)
/**
 * mmaps's other flags
 */
#define MMAP_FLAGS (MAP_PRIVATE|MAP_ANONYMOUS)
/**
 * Minimum size of memory block assigned to caller program
 */
#define MIN_BLOCK_SIZE 32

/**
 * Finds maximum of two numbers
 * @param first First number
 * @param second Second number
 */
#define MAX(first, second) (((first) > (second)) ? (first) : (second))
/**
 * Aligns number to the alignment
 * @param number Number to be aligned
 * @param alignment Number to align for (ex.: 5 aligned to 4 is 8)
 */
#define ALIGN(number, alignment) (((number) + (alignment) - 1) / (alignment) * (alignment))
/**
 * Gives the first header of arena
 * @param arena Arena for which to search for the header
 */
#define FIRST_HEADER(arena) ((Header *)((char *)(arena) + sizeof(Arena)))
/**
 * Gives the next header
 * @param current Current header
 * @param offset Offset from the end of current header (or size stored in current header)
 */
#define NEXT_HEADER(current, offset) ((Header *)((char *)(current) + sizeof(Header) + (offset)))

/**
 * First arena of allocated memory from OS
 */
Arena *first_arena = NULL;

/**
 * Return size alligned to PAGE_SIZE
 */
size_t allign_page(size_t size)
{
    return ALIGN(size, PAGE_SIZE);
}

/**
 * Allocate a new arena using mmap.
 * @param req_size requested size in bytes. Should be alligned to PAGE_SIZE.
 * @return pointer to a new arena, if successfull. NULL if error.
 * @pre req_size > sizeof(Arena) + sizeof(Header) + MIN_BLOCK_SIZE
 */

/*
 *   +-----+------------------------------------+
 *   |Arena|....................................|
 *   +-----+------------------------------------+
 *
 *   |--------------- Arena.size ---------------|
 */
static
Arena *arena_alloc(size_t req_size)
{
    assert(req_size > sizeof(Arena) + sizeof(Header) + MIN_BLOCK_SIZE);

    size_t aligned_size = allign_page(req_size);

    // Allocate new arena with mmap
    Arena *arena;
    if ((arena = mmap(NULL, aligned_size, MMAP_PROT, MMAP_FLAGS, -1, 0)) == MAP_FAILED) {
        return NULL;
    }

    arena->size = aligned_size;
    arena->next = NULL;

    return arena;
}

/**
 * Appends a new arena to the end of the arena list.
 * @param a     already allocated arena
 */
static
void arena_append(Arena *a)
{
    // Find last arena in linked list
    Arena *last_arena = first_arena;
    while (last_arena->next != NULL) {
        last_arena = last_arena->next;
    }

    last_arena->next = a;
}

/**
 * Header structure constructor (alone, not used block).
 * @param hdr       pointer to block metadata.
 * @param size      size of free block
 * @pre size > 0
 */
/**
 *   +-----+------+------------------------+----+
 *   | ... |Header|........................| ...|
 *   +-----+------+------------------------+----+
 *
 *                |-- Header.size ---------|
 */
static
void hdr_ctor(Header *hdr, size_t size)
{
    assert(size > 0);

    hdr->next = NULL;
    hdr->size = size;
    hdr->asize = 0; // Not user allocated (by caller program)
}

/**
 * Checks if the given free block should be split in two separate blocks.
 * @param hdr       header of the free block
 * @param size      requested size of data
 * @return true if the block should be split
 * @pre hdr->asize == 0
 * @pre size > 0
 */
static
bool hdr_should_split(Header *hdr, size_t size)
{
    assert(hdr->asize == 0);
    assert(size > 0);

    size_t aligned_size = ALIGN(size, sizeof(size_t));

    return (hdr->size - (MAX(aligned_size, MIN_BLOCK_SIZE) + sizeof(Header) + MIN_BLOCK_SIZE)) > 0;
}

/**
 * Splits one block in two.
 * @param hdr       pointer to header of the big block
 * @param req_size  requested size of data in the (left) block.
 * @return pointer to the new (right) block header.
 * @pre   (hdr->size >= MAX(req_size, MIN_BLOCK_SIZE) + sizeof(Header) + MIN_BLOCK_SIZE)
 */
/*
 * Before:        |---- hdr->size ---------|
 *
 *    -----+------+------------------------+----
 *         |Header|........................|
 *    -----+------+------------------------+----
 *            \----hdr->next---------------^
 */
/*
 * After:         |- req_size -|
 *
 *    -----+------+------------+------+----+----
 *     ... |Header|............|Header|....|
 *    -----+------+------------+------+----+----
 *             \---next--------^  \--next--^
 */
static
Header *hdr_split(Header *hdr, size_t req_size)
{
    size_t aligned_size = ALIGN(req_size, sizeof(size_t));
    size_t alloc_size = MAX(aligned_size, MIN_BLOCK_SIZE);

    assert((hdr->size - (alloc_size + sizeof(Header) + MIN_BLOCK_SIZE)) > 0);

    // Create new header (for block which is the rest of the old big block)
    Header *new_hdr = NEXT_HEADER(hdr, alloc_size);
    hdr_ctor(new_hdr, hdr->size - sizeof(Header) - alloc_size);

    // Update old header
    hdr->size = alloc_size;
    hdr->asize = req_size;

    // Update list pointers
    new_hdr->next = hdr->next;
    hdr->next = new_hdr;

    return new_hdr;
}

/**
 * Detect if two adjacent blocks could be merged.
 * @param left      left block
 * @param right     right block
 * @return true if two block are free and adjacent in the same arena.
 * @pre left->next == right
 * @pre left != right
 */
static
bool hdr_can_merge(Header *left, Header *right)
{
    assert(left->next == right);
    assert(left != right);

    // There is a non-free block
    if (left->asize != 0 || right->asize != 0) {
        return false;
    }

    return (NEXT_HEADER(left, left->size) == right);
}

/**
 * Merge two adjacent free blocks.
 * @param left      left block
 * @param right     right block
 * @pre left->next == right
 * @pre left != right
 */
static
void hdr_merge(Header *left, Header *right)
{
    assert(left->next == right);
    assert(left != right);

    left->size = left->size + sizeof(Header) + right->size;
    left->next = right->next;
}

/**
 * Finds the free block that fits best to the requested size.
 * @param size      requested size
 * @return pointer to the header of the block or NULL if no block is available.
 * @pre size > 0
 */
static
Header *best_fit(size_t size)
{
    assert(size > 0);

    Header *first_hdr = FIRST_HEADER(first_arena);
    Header *best_fit = NULL;
    for (Header *curr_hdr = first_hdr->next; curr_hdr != first_hdr; curr_hdr = curr_hdr->next) {
        // Check required conditions for header:
        //  1. it's free,
        //  2. it's big enough
        if (curr_hdr->asize == 0 && curr_hdr->size >= size) {
            // We are using best fit --> better is smaller block
            if (best_fit == NULL || curr_hdr->size < best_fit->size) {
                best_fit = curr_hdr;
            }
        }
    }

    return best_fit;
}

/**
 * Search the header which is the predecessor to the hdr. Note that if 
 * @param hdr       successor of the search header
 * @return pointer to predecessor, hdr if there is just one header.
 * @pre first_arena != NULL
 * @post predecessor->next == hdr
 */
static
Header *hdr_get_prev(Header *hdr)
{
    assert(first_arena != NULL);

    Header *current_header = FIRST_HEADER(first_arena);
    while (current_header->next != hdr) {
        current_header = current_header->next;
    }

    assert(current_header->next == hdr);

    return current_header;
}

/**
 * Allocate memory. Use best-fit search of available block.
 * @param size      requested size for program
 * @return pointer to allocated data or NULL if error or size = 0.
 */
void *mmalloc(size_t size)
{
    // Check for bad input value
    if (size == 0) {
        return NULL;
    }

    // Prepare header for user allocation
    Header *best_fit_hdr;
    if (first_arena == NULL) {
        // No arena has been created yet --> make essential inits
        // Create arena
        size_t arena_size = MAX(size, PAGE_SIZE);
        if ((first_arena = arena_alloc(arena_size)) == NULL) {
            // OS can't give us a new memory block
            return NULL;
        }

        // Init arena with first header and use it for next actions
        best_fit_hdr = FIRST_HEADER(first_arena);
        hdr_ctor(best_fit_hdr, arena_size - sizeof(Arena));
        best_fit_hdr->next = best_fit_hdr;
    } else {
        // The function has already been used, so we have all initialized
        // Try to find header with free space for a new allocation
        best_fit_hdr = best_fit(size);
        if (best_fit_hdr == NULL) {
            // No arena can store this block --> we need a new one
            size_t arena_size = MAX(size, PAGE_SIZE);
            Arena *new_arena;
            if ((new_arena = arena_alloc(arena_size)) == NULL) {
                // OS can't give us a new memory block
                return NULL;
            }

            // Init new arena with header and use this header for next actions
            best_fit_hdr = FIRST_HEADER(new_arena);
            hdr_ctor(best_fit_hdr, arena_size - sizeof(Arena));

            // Set a next header, which is the first one (cyclic list)
            Header *first_hdr = FIRST_HEADER(first_arena);
            best_fit_hdr->next = first_hdr;

            // Find penultimate header (previously last)
            Header *penultimate_hdr = first_hdr;
            for (Header *curr_hdr = first_hdr->next; curr_hdr != first_hdr; curr_hdr = curr_hdr->next) {
                penultimate_hdr = curr_hdr;
            }

            // Add link to the penultimate header
            penultimate_hdr->next = best_fit_hdr;

            // Add arena to the list of arenas
            arena_append(new_arena);
        }
    }

    // Split header when it's too large
    if (hdr_should_split(best_fit_hdr, size)) {
        hdr_split(best_fit_hdr, size);
    }

    // Update used header
    best_fit_hdr->asize = size;

    // Return pointer to user allocated space inside used header
    return (void *)((char *)best_fit_hdr + sizeof(Header));
}

/**
 * Free memory block.
 * @param ptr       pointer to previously allocated data
 * @pre ptr != NULL
 */
void mfree(void *ptr)
{
    // Header for allocated space
    Header *processed_hdr = (Header *)((char *)ptr - sizeof(Header));

    // Set block of the header as not used
    processed_hdr->asize = 0;

    // Inform previous header about this change (I'm not available, use my successor)
    Header *prev_hdr = hdr_get_prev(processed_hdr);

    // Merge with surrounding blocks if possible
    // This and next
    // The next one must go before the previous one, because when merged,
    // processed_hdr will stay and has the right pointer to next header
    Header *next_hdr = processed_hdr->next;
    if (hdr_can_merge(processed_hdr, next_hdr)) {
        hdr_merge(processed_hdr, next_hdr);
    }

    // Previous and this
    if (hdr_can_merge(prev_hdr, processed_hdr)) {
        hdr_merge(prev_hdr, processed_hdr);
    }
}

/**
 * Reallocate previously allocated block.
 * @param ptr       pointer to previously allocated data
 * @param size      a new requested size. Size can be greater, equal, or less
 * then size of previously allocated block.
 * @return pointer to reallocated space or NULL if size equals to 0 or if error.
 * @post header_of(return pointer)->asize == size
 */
void *mrealloc(void *ptr, size_t size)
{
    // Header for allocated space
    Header *processed_hdr = (Header *)((char *)ptr - sizeof(Header));

    // Block is big enough for containing data of the new size
    // This is used for shrinking, too
    if (processed_hdr->size <= size) {
        // Update block's used size
        processed_hdr->asize = size;

        return ptr;
    }

    // The block currently in use is too small --> we need to change it for the bigger one
    // Backup old size
    size_t old_size = processed_hdr->asize;

    // Free up current block
    mfree(ptr);

    // Allocate new block with the right size
    void *new_ptr;
    if ((new_ptr = mmalloc(size)) == NULL) {
        // mmalloc() can't allocate 0 bytes
        // mmalloc() can end with error due to allocation problems
        return NULL;
    }

    // Move data from old block to the new one
    memcpy(new_ptr, ptr, old_size);

    return new_ptr;
}
