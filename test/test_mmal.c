#undef NDEBUG

#include <stdio.h>
#include <assert.h>
#include "../src/mmal.h"
#include <unistd.h>

#define MSTR(x) #x
#define M2STR(x) MSTR(x)
#define HERE __FILE__ ":" M2STR(__LINE__) ": "

void debug_hdr(Header *h, int idx)
{
    printf("+- Header %d @ %p, data @ %p\n", idx, h, &h[1]);
    printf("|    | next           | size     | asize    |\n");
    printf("|    | %-14p | %-8lu | %-8lu |\n", h->next, h->size, h->asize);
}

void debug_arena(Arena *a, int idx)
{
    printf("Arena %d @ %p, size: %lu\n", idx, a, a->size);
    printf("|\n");
    char *arena_stop = (char*)a + a->size;
    Header *h = (Header*)&a[1];
    int i = 1;

    while ((char*)h >= (char*)a && (char*)h < arena_stop)
    {
        debug_hdr(h, i);
        i++;
        h = h->next;
        if (h == (Header*)&a[1])
            break;
    }
}

void debug_arenas(const char *msg)
{
    printf("%s\n", msg);
    printf("==========================================================\n");
    Arena *a = first_arena;
    for (int i = 1; a; i++)
    {
        debug_arena(a, i);
        a = a->next;
        printf("|\n");
    }
    printf("NULL\n");
}

int main()
{
    assert(first_arena == NULL);

    /***********************************************************************/
    // Prvni alokace
    // Mela by alokovat novou arenu, pripravit hlavicku v ni a prave jeden
    // blok.
    void *p1 = mmalloc(42);
    /**
     *   v----- first_arena
     *   +-----+------+----+------+----------------------------+
     *   |Arena|Header|XXXX|Header|............................|
     *   +-----+------+----+------+----------------------------+
     *       p1-------^
     */
    if (p1 == NULL)
        perror("mmalloc");
    assert(first_arena != NULL);
    assert(first_arena->next == NULL);
    assert(first_arena->size > 0);
    assert(first_arena->size <= PAGE_SIZE);
    Header *h1 = (Header*)(&first_arena[1]);
    Header *h2 = h1->next;
    assert(h1->asize == 42);
    assert((char*)h2 > (char*)h1);
    assert(h2->next == h1);
    assert(h2->asize == 0);

    debug_arenas(HERE "po mmalloc(42) = mmalloc(0x2a)");

    /***********************************************************************/
    // Druha alokace
    char *p2 = mmalloc(42);
    /**
     *   v----- first_arena
     *   +-----+------+----+------+----+------+----------------+
     *   |Arena|Header|XXXX|Header|XXXX|Header|................|
     *   +-----+------+----+------+----+------+----------------+
     *       p1-------^           ^
     *       p2-------------------/
     */
    Header *h3 = h2->next;
    assert(h3 != h1);
    assert(h2 != h3);
    assert(h3->next == h1);
    assert((char*)h2 < p2);
    assert(p2 < (char*)h3);

    debug_arenas(HERE "po 2. mmalloc(42) = mmalloc(0x2a)");

    /***********************************************************************/
    // Treti alokace
    void *p3 = mmalloc(16);
    /**
     *                p1          p2          p3
     *   +-----+------+----+------+----+------+-----+------+---+
     *   |Arena|Header|XXXX|Header|XXXX|Header|XXXXX|Header|...|
     *   +-----+------+----+------+----+------+-----+------+---+
     */
    // insert assert here
    debug_arenas(HERE "po 3. mmalloc(16) = mmalloc(0x10)");

    /***********************************************************************/
    // Uvolneni prvniho bloku
    mfree(p1);

    /**
     *                p1          p2          p3
     *   +-----+------+----+------+----+------+-----+------+---+
     *   |Arena|Header|....|Header|XXXX|Header|XXXXX|Header|...|
     *   +-----+------+----+------+----+------+-----+------+---+
     */
    // insert assert here
    debug_arenas(HERE "po mfree(p1)");

    /***********************************************************************/
    // Uvolneni posledniho zabraneho bloku
    mfree(p3);
    /**
     *                p1          p2          p3
     *   +-----+------+----+------+----+------+----------------+
     *   |Arena|Header|....|Header|XXXX|Header|................|
     *   +-----+------+----+------+----+------+----------------+
     */
    // insert assert here
    debug_arenas(HERE "po mfree(p3)");

    /***********************************************************************/
    // Uvolneni prostredniho bloku
    mfree(p2);
    /**
     *                p1          p2          p3
     *   +-----+------+----------------------------------------+
     *   |Arena|Header|........................................|
     *   +-----+------+----------------------------------------+
     */
    // insert assert here
    debug_arenas(HERE "po mfree(p2)");

    // Dalsi alokace se nevleze do existujici areny
    void *p4 = mmalloc(PAGE_SIZE*2);
    /**
     *   /-- first_arena
     *   v            p1          p2          p3
     *   +-----+------+----------------------------------------+
     *   |Arena|Header|........................................|
     *   +-----+------+----------------------------------------+
     *      \ next
     *       v            p4
     *       +-----+------+---------------------------+------+-----+
     *       |Arena|Header|XXXXXXXXXXXXXXXXXXXXXXXXXXX|Header|.....|
     *       +-----+------+---------------------------+------+-----+
     */
    Header *h4 = &((Header*)p4)[-1];
    assert(h1->next == h4);
    assert(h4->asize == PAGE_SIZE*2);
    assert(h4->next->next == h1);

    debug_arenas(HERE "po mmalloc(262144) = mmalloc(0x40000)");

    /***********************************************************************/
    p4 = mrealloc(p4, PAGE_SIZE*2 + 2);
    /**
     *                    p4
     *       +-----+------+-----------------------------+------+---+
     *       |Arena|Header|XXXXXXXXXXXXXXXXXXXXXXXXXXXxx|Header|...|
     *       +-----+------+-----------------------------+------+---+
     */

    assert(p4 != NULL);
    // h4 need not to be in the same location; would be nice, but not required
    h4 = &((Header*)p4)[-1];
    assert(h4->asize == PAGE_SIZE*2 + 2);
    debug_arenas(HERE "po mrealloc(p4, 262146) = mmrealloc(p4, 0x400002)");

    /***********************************************************************/
    mfree(p4);
    assert(h4->asize == 0);
    assert(h4->next == h1);

    debug_arenas(HERE "po mfree(p4)");

    return 0;
}
