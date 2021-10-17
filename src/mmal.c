/**
 * Implementace My MALloc
 * Demonstracni priklad pro 1. ukol IPS/2021
 *
 * @authors Ales Smrcka, Michal Smahel (xsmahe01)
 */

#include "mmal.h"
#include <sys/mman.h> // mmap
#include <stdbool.h> // bool
#include <assert.h> // assert
#include <string.h> // memcpy

#ifdef NDEBUG
/**
 * The structure header encapsulates data of a single memory block.
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

#define PAGE_SIZE (128*1024)

#endif // NDEBUG

Arena *first_arena = NULL;

/**
 * Return size alligned to PAGE_SIZE
 */
size_t allign_page(size_t size)
{
    return (size + PAGE_SIZE - 1) / PAGE_SIZE * PAGE_SIZE;
}

/**
 * Allocate a new arena using mmap.
 * @param req_size requested size in bytes. Should be alligned to PAGE_SIZE.
 * @return pointer to a new arena, if successfull. NULL if error.
 * @pre req_size > sizeof(Arena) + sizeof(Header)
 */

/**
 *   +-----+------------------------------------+
 *   |Arena|....................................|
 *   +-----+------------------------------------+
 *
 *   |--------------- Arena.size ---------------|
 */
static
Arena *arena_alloc(size_t req_size)
{
    size_t aligned_size = allign_page(req_size);

    // Allocate new arena with mmap
    Arena *arena;
    if ((arena = mmap(NULL, aligned_size, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0)) == MAP_FAILED) {
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
    hdr->next = NULL;
    hdr->size = size;
    hdr->asize = 0; // Not allocated by program
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
    assert(size > 0);

    // 32 B is minimum block size
    return (hdr->size - (size + sizeof(Header) + 32)) > 0;
}

/**
 * Splits one block in two.
 * @param hdr       pointer to header of the big block
 * @param req_size  requested size of data in the (left) block.
 * @return pointer to the new (right) block header.
 * @pre   (hdr->size >= req_size + 2*sizeof(Header))
 */
/**
 * Before:        |---- hdr->size ---------|
 *
 *    -----+------+------------------------+----
 *         |Header|........................|
 *    -----+------+------------------------+----
 *            \----hdr->next---------------^
 */
/**
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
    // Count really allocated size (often bigger than req_size)
    size_t align = sizeof(size_t);
    // 32 B is minimum block size
    size_t alloc_size = ((req_size < 32) ? 32 : req_size);

    // Create new header (for block which is the rest of the old big block)
    Header *new_hdr = (Header *)((char *)hdr + sizeof(Header) + alloc_size);
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
    // There is a non-free block
    if (left->asize != 0 || right->asize != 0) {
        return false;
    }

    return (((char *)left + sizeof(Header) + left->size) == (char *)right);
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
    Header *first_hdr = (Header *)((char *)first_arena + sizeof(Arena));
    Header *best_fit = first_hdr;
    for (Header *curr_hdr = first_hdr->next; curr_hdr != first_hdr; curr_hdr = curr_hdr->next) {
        if (curr_hdr->size < best_fit->size && curr_hdr->size >= size) {
            best_fit = curr_hdr;
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
    Header *current_header = (Header *)((char *)first_arena + sizeof(Arena));
    while (current_header->next != hdr) {
        current_header = current_header->next;
    }

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

    // FIXME
    (void)size;
    return NULL;
}

/**
 * Free memory block.
 * @param ptr       pointer to previously allocated data
 * @pre ptr != NULL
 */
void mfree(void *ptr)
{
    (void)ptr;
    // FIXME
}

/**
 * Reallocate previously allocated block.
 * @param ptr       pointer to previously allocated data
 * @param size      a new requested size. Size can be greater, equal, or less
 * then size of previously allocated block.
 * @return pointer to reallocated space or NULL if size equals to 0.
 * @post header_of(return pointer)->size == size
 */
void *mrealloc(void *ptr, size_t size)
{
    // FIXME
    (void)ptr;
    (void)size;
    return NULL;
}
