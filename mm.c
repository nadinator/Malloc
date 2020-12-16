/*
 *
 *                        *****************************
 *
 *                                 Malloc Lab
 *                               November 2020
 *                                15-213 CMUQ
 *
 *
 *                          Written by Nadim Bou Alwan
 *                              Andrew ID nboualwa
 *
 *                       *****************************
 *
 *                                                                  
 *                                    mm.c
 *            64-bit struct-based segregated free list memory allocater
 *
 *                                                                            
 *  ************************************************************************  
 *                               DOCUMENTATION
 *                  (Template borrowed 15-213's mm-baseline.c)                                
 *                                                                            
 *  ** STRUCTURE. **                                                          
 *                                                                            
 *  Allocated and free blocks differ in their structure. Details below.
 *  
 *  HEADER: 8-byte, aligned to 8th byte of an 16-byte aligned heap, where     
 *          - The lowest order bit is 1 when the block is allocated, and      
 *            0 otherwise.   
 *          - The second lowest order bit is 1 when the previous block is
 *            allocated, and 0 otherwise (applies only to allocated blocks).                                                
 *          - The whole 8-byte value with the least two significant bits set
 *            to 0 represents the size of the block as a size_t.                    
 *            The size of a block includes the header and footer.            
 *  FOOTER: 8-byte, aligned to 0th byte of an 16-byte aligned heap. It        
 *          contains the exact copy of the block's header.                    
 *  The minimum blocksize is 32 bytes.                                        
 *                                                                            
 *  Allocated blocks contain the following:                                   
 *  HEADER, as defined above.                                                 
 *  PAYLOAD: Memory allocated for program to store information.               
 *  The size of an allocated block is exactly PAYLOAD + HEADER.      
 *                                                                            
 *  Free blocks contain the following:                                        
 *  HEADER, as defined above.                                                 
 *  FOOTER, as defined above.
 *  PREV_POINTER, that points to the previous free block in its segregated list
 *  NEXT_POINTER, that points to the next free block in its segregated list                                                 
 *  The size of an unallocated block is at least 32 bytes.                    
 *                                                                            
 *  Block Visualization.                                                      
 *                    block     block+8          block+size    
 *  Allocated blocks:   |  HEADER  |  ... PAYLOAD ... |           
 *                                                                            
 *                    block     block+8          block+16       block+24     block+size-8  block+size 
 *  Unallocated blocks: |  HEADER  |  PREV_POINTER  | NEXT_POINTER |  ...EMPTY...  |  FOOTER  |           
 *                                                                            
 *  ************************************************************************  
 *  ** INITIALIZATION. **                                                     
 *                                                                            
 *  The following visualization reflects the beginning of the heap.           
 *      start            start+8           start+16                           
 *  INIT: | PROLOGUE_FOOTER | EPILOGUE_HEADER |                               
 *  PROLOGUE_FOOTER: 8-byte footer, as defined above, that simulates the      
 *                    end of an allocated block. Also serves as padding.      
 *  EPILOGUE_HEADER: 8-byte block indicating the end of the heap.             
 *                   It simulates the beginning of an allocated block         
 *                   The epilogue header is moved when the heap is extended.  
 *                                                                            
 *  ************************************************************************  
 *  ** BLOCK ALLOCATION. **                                                   
 *                                                                            
 *  Upon memory request of size S, a block of size S + dsize, rounded up to   
 *  16 bytes, is allocated on the heap, where dsize is 2*8 = 16.              
 *  Selecting the block for allocation is performed by finding the first      
 *  block that can fit the content based on a first-fit search policy.                        
 *  The search starts by determing which segregated list to look in, based on 
 *  the requested size.
 *  After this, the selected list is iterated until:                                    
 *  - A sufficiently-large unallocated block is found, or                     
 *  - The end of the segregated free list is reached, which occurs              
 *    when no sufficiently-large unallocated block is available.
 *    The search then carries on until the next list, which repeats
 *    until all the lists have been exhausted. This means no appropriate
 *    free block was found.              
 *  In case that a sufficiently-large unallocated block is found, then        
 *  that block will be used for allocation. Otherwise--that is, when no       
 *  sufficiently-large unallocated block is found--then more unallocated      
 *  memory of size chunksize or requested size, whichever is larger, is       
 *  requested through mem_sbrk, and the search is redone.                     
 *                                                                            
 */

/* Do not change the following! */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stddef.h>
#include <assert.h>
#include <stddef.h>

#include "mm.h"
#include "memlib.h"

#ifdef DRIVER
/* create aliases for driver tests */
#define malloc mm_malloc
#define free mm_free
#define realloc mm_realloc
#define calloc mm_calloc
#define memset mem_memset
#define memcpy mem_memcpy
#endif /* def DRIVER */

/* You can change anything from here onward */

/*
 * If DEBUG is defined, enable printing on dbg_printf and contracts.
 * Debugging macros, with names beginning "dbg_" are allowed.
 * You may not define any other macros having arguments.
 */
// #define DEBUG // uncomment this line to enable debugging

#ifdef DEBUG
/* When debugging is enabled, these form aliases to useful functions */
#define dbg_printf(...) printf(__VA_ARGS__)
#define dbg_requires(...) assert(__VA_ARGS__)
#define dbg_assert(...) assert(__VA_ARGS__)
#define dbg_ensures(...) assert(__VA_ARGS__)
#else
/* When debugging is disnabled, no code gets generated for these */
#define dbg_printf(...)
#define dbg_requires(...)
#define dbg_assert(...)
#define dbg_ensures(...)
#endif

/* Basic constants */
typedef uint64_t word_t;
static const size_t wsize = sizeof(word_t);   // word, header, footer size (bytes)
static const size_t dsize = 2*wsize;          // double word size (bytes)
static const size_t min_block_size = 2*dsize; // Minimum block size
static const size_t chunksize = (1 << 12);    // requires (chunksize % 16 == 0)

#define ALIGNMENT 16
#define BLOCK_SIZE sizeof(block_t)
#define SEG_SIZE   8      // Number of segregated lists
#define SIZE_LIST1 32     // Max block size that seglist[SIZE_LIST(n-1)] holds
#define SIZE_LIST2 64 
#define SIZE_LIST3 128 
#define SIZE_LIST4 256 
#define SIZE_LIST5 512 
#define SIZE_LIST6 1024 
#define SIZE_LIST7 2048
#define SIZE_LIST8 4096

/* Basic structures */
typedef struct free_block {
/*
 * Doubly-linked list that links free blocks.
 */
    struct block *next;
    struct block *prev;
} free_t;

typedef struct block {
/* 
 * Block type structure.
 * Minimum block size: 32B 
 */
    /* Header contains size + allocation flag + alloc flag of previous block */
    word_t header; 
    /*
     * Allocated/free block union.
     * An allocated block does not require pointers, so which 
     * element of the union is used is based on whether the block
     * is allocated or not.
     */ 
    union alloc_or_free {
        struct free_block fb;
        /*
         * We don't know how big the payload will be. Declaring it as an
         * array of size 0 allows computing its starting address using
         * pointer notation.
         */
        char payload[0];
    } aof; 
    /*
     * Only free blocks have footers.
     * However, we can't declare the footer as part of the struct, since its
     * starting position is unknown.
     */
} block_t;

/* Global variables */
static block_t *heap_listp = NULL;      // Pointer to first block
static block_t *seg_listsp[SEG_SIZE];   // Array of free lists 

/* Function prototypes for internal helper routines */
static block_t *extend_heap(size_t size);
static void place(block_t *block, size_t asize);
static block_t *find_fit(size_t asize);
static block_t *coalesce(block_t *block);

static size_t max(size_t x, size_t y);
static size_t round_up(size_t size, size_t n);
static word_t pack(size_t size, bool alloc, bool alloc_prev);

static size_t extract_size(word_t header);
static size_t get_size(block_t *block);
static size_t get_payload_size(block_t *block);

static bool extract_alloc(word_t header);
static bool get_alloc(block_t *block);
static bool extract_alloc_prev(word_t word);
static bool get_alloc_prev(block_t *block);

static void write_header(block_t *block, size_t size, bool alloc, bool alloc_prev);
static void write_footer(block_t *block, size_t size, bool alloc);

static block_t *payload_to_header(void *bp);
static void *header_to_payload(block_t *block);

static block_t *find_next(block_t *block);
static word_t *find_prev_footer(block_t *block);
static block_t *find_prev(block_t *block);

static void insert_list(block_t *block);
static void remove_list(block_t *block);
static int get_seglist_size (size_t asize);


/* align: rounds up to the nearest multiple of ALIGNMENT */
static size_t align(size_t x) 
{
    return ALIGNMENT * ((x+ALIGNMENT-1)/ALIGNMENT);
}

/*
 * mm_init: initializes the heap; it is run once when heap_start == NULL.
 *          prior to any extend_heap operation, this is the heap:
 *              start            start+8           start+16
 *          INIT: | PROLOGUE_FOOTER | EPILOGUE_HEADER |
 * heap_listp ends up pointing to the epilogue header.
 * free_listp is re-initialized to NULL.
 */
bool mm_init(void) 
{
    dbg_printf("Initializing...\n");

    /* Create the initial empty heap */
    word_t *start = (word_t *)(mem_sbrk(2*wsize));

    if (start == (void *)-1) 
    {
        dbg_printf("Not enough memory available.\n");
        return false;
    }

    start[0] = pack(0, true, true); // Prologue footer
    start[1] = pack(0, true, true); // Epilogue header
    // Heap starts with first block header (epilogue)
    heap_listp = (block_t *) &(start[1]);

    /* Initialize segregated lists */
    for (int i = 0; i < SEG_SIZE; i++)
        seg_listsp[i] = NULL;

    dbg_printf("Extending heap...\n");

    if (extend_heap(chunksize) == NULL)
    {
        dbg_printf("extend_heap(chunksize) returned NULL.\n");
        return false;
    }

    dbg_printf("Initial heap extension successful.\n");
    return true;
}

/*
 * malloc: allocates a block with size at least (size + dsize), rounded up to
 *         the nearest 16 bytes, with a minimum of 2*dsize. Seeks a
 *         sufficiently-large unallocated block on the heap to be allocated.
 *         If no such block is found, extends heap by the maximum between
 *         chunksize and (size + dsize) rounded up to the nearest 16 bytes,
 *         and then attempts to allocate all, or a part of, that memory.
 *         Returns NULL on failure, otherwise returns a pointer to such block.
 *         The allocated block will not be used for further allocations until
 *         freed.
 */
void *malloc (size_t size) 
{
    dbg_printf("Malloc(%zd), at beginning\n", size);
    size_t asize;      // Adjusted block size
    size_t extendsize; // Amount to extend heap if no fit is found
    block_t *block;    // Pointer to block
    void *bp;          // Pointer to payload

    /* Initialize heap if it isn't initialized */
    if (heap_listp == NULL)
        mm_init();

    /* Ignore spurious request */
    if (size == 0)
        return NULL;

    /* Adjust block size to include overhead and meet alignment requirements */
    asize = max(2*dsize, align(size + wsize));
    dbg_printf("size %zd rounded to asize %zd.\n", size, asize);

    /* Search the segregated lists for a fit */
    block = find_fit(asize);

    /* If no fit is found, request more memory and then place the block */
    if (block == NULL)
    {  
        extendsize = max(asize, chunksize);
        dbg_printf("No fit found, extending heap by %zd.\n", extendsize);
        block = extend_heap(extendsize);

        /* Check that extend_heap does not return NULL (error) */
        if (block == NULL)
        {
            dbg_printf("extend_heap(%zd) returned NULL.\n", extendsize);
            return NULL;
        }
    }

    place(block, asize);
    bp = header_to_payload(block);
    dbg_printf("Malloc(%zd) --> %p, completed.\n", size, bp);
    return bp;
}

/*
 * free: Frees the block such that it is no longer allocated while doing
 *       necessary coalescing. Block will be available for use on malloc.
 */
void free (void *ptr) 
{    
    block_t *block;

    if (ptr == NULL) 
        return;

    block = payload_to_header(ptr);
    /* Coalesce removes the block from its seglist and coalesces */
    coalesce(block); 
}

/*
 * realloc: returns a pointer to an allocated region of at least size bytes:
 *          if ptrv is NULL, then call malloc(size);
 *          if size == 0, then call free(ptr) and returns NULL;
 *          else allocates new region of memory, copies old data to new memory,
 *          and then free old block. Returns old block if realloc fails or
 *          returns new pointer on success.
 */
void *realloc(void *oldptr, size_t size) 
{
    block_t *bp = payload_to_header(oldptr);
    size_t copysize;
    void *newptr;

    /* If size == 0, then free block and return NULL */
    if (size == 0)
    {
        free(oldptr);
        return NULL;
    }

    /* If ptr is NULL, then equivalent to malloc */
    if (oldptr == NULL)
        return malloc(size);

    /* Otherwise, proceed with reallocation */
    newptr = malloc(size);
    /* If malloc fails, the original block is left untouched */
    if (!newptr)
        return NULL;

    /* Copy the old data */
    copysize = get_payload_size(bp); // gets size of old payload
    if(size < copysize)
        copysize = size;
    memcpy(newptr, oldptr, copysize);

    /* Free the old block */
    free(oldptr);

    return newptr;
}

/*
 * calloc: Allocates a block with size at least (elements * size + dsize)
 *         through malloc, then initializes all bits in allocated memory to 0.
 *         Returns NULL on failure.
 */
void *calloc(size_t nmemb, size_t size)
{
    void *bp;
    size_t asize = nmemb * size;

    /* Check if multiplication overflowed */
    if (asize/nmemb != size)
        return NULL;
    
    bp = malloc(asize);
    if (bp == NULL)
        return NULL;

    /* Initialize all bits to 0 */
    memset(bp, 0, asize);

    return bp;
}

/* mm_checkheap: checks the heap for correctness; returns true if
 *               the heap is correct, and false otherwise.
 *               can call this function using mm_checkheap(__LINE__);
 *               to identify the line number of the call site.
 */
bool mm_checkheap(int lineno)
{
    bool check = true;
    block_t *ptr;       // Generic block pointer for checking 

    /*** Checking epilogue and prologue blocks ***/
    ptr = heap_listp; // Prologue pointer
    /* Check prologue size */
    if (get_size(ptr) != 0)
        check = false;
    /* Check that prologue is at beginning */
    if (ptr != mem_heap_lo())
        check = false;
    /* Check allocation status */
    if (!get_alloc(ptr))
        check = false;

    ptr = (block_t *)((char *)mem_heap_hi() - (wsize-1)); // Epilogue pointer
    /* Check epilogue size */
    if (get_size(ptr) != 0)
        check = false;
    /* Check allocation status */
    if (!get_alloc(ptr))
        check = false;

    /* Return if error */
    if (!check)
    {
        dbg_printf("Failed mm_checkheap at lineno: %d", lineno);
        return false;   
    }

    /*** Iterating through heap while making multiple checks ***/
    ptr = heap_listp;     // Starting at beginning of heap
    int wrap_count = 0;   // Increment on epilogue and prologue 
    block_t *next_ptr;    // The next block from the current one
    while (wrap_count < 2)
    {
        /* Iterate on prologue+epilogue */
        if (get_size(ptr) == 0)
            wrap_count++;

        /* Allocated blocks */ 
        else if (get_alloc(ptr))
        {
            /* Check alignment */
            if (((unsigned int)ptr % ALIGNMENT) != 0)
                check = false;
            /* Check Header according to specification above */
            next_ptr = find_next(ptr);
            if (get_alloc(next_ptr) && !get_alloc_prev(next_ptr))
                check = false;
            if ((get_size(ptr) % ALIGNMENT) != 0)
                check = false;
        }

        /* Free blocks */
        else if (!get_alloc(ptr))
        {
            next_ptr = find_next(ptr);
            word_t *footerp = find_prev_footer(next_ptr); // Block footer
            /* Check coalescing */
            if (!get_alloc(next_ptr))
                check = false;
            /* Check alignment */
            if (((size_t)ptr % dsize) != 0)
                check = false;
            /* Check Header+Footer */
            if (get_alloc_prev(next_ptr))
                check = false;
            if (get_size(ptr) != extract_size(*footerp))
                check = false;
        }

        ptr = find_next(ptr); // Next block in heap
    }

    if (!check)
        dbg_printf("Failed mm_checkheap at lineno: %d", lineno);

    return check;
}


/******** The remaining functions below are helper and debug routines ********/


/*
 * insert_list: insert the block into the free list by moving pointers 
 *              around. Using LIFO ordering for insertion.
 */
static void insert_list(block_t *block)
{
    dbg_printf("Inserting block %p into free list.\n", block);

    /* Find which seglist to insert the block into based on its size */
    int i = get_seglist_size(get_size(block));

    /* Set pointer to previous block to NULL */
    block->aof.fb.prev = NULL;

    /* Add block to the front of an empty/uninitialized seglist[i] */
    if (seg_listsp[i] == NULL)
    {
        block->aof.fb.next = NULL;
        seg_listsp[i] = block;
        dbg_printf("Inserted into empty list.\n");
    }

    /* Add block to the front of a non-empty seglist[i] */
    else
    {
        seg_listsp[i]->aof.fb.prev = block;
        block->aof.fb.next = seg_listsp[i];
        seg_listsp[i] = block;
        dbg_printf("Inserted into non-empty list.\n");
    }

    dbg_printf("free_listp = %p\n", free_listp);
}


/*
 * remove_list: remove the block from the free list by moving pointers around 
 */
static void remove_list(block_t *block)
{    
    dbg_printf("Removing block %p from free list.\n", block);
    
    /* Find which seglist to insert the block into based on its size */
    int i = get_seglist_size(get_size(block));

    /* Check if block is the first element ("head") of the list */
    if (block->aof.fb.prev == NULL)
        seg_listsp[i] = block->aof.fb.next;

    /* Check if block is not the last element of the list */
    if (block->aof.fb.next != NULL)
        block->aof.fb.next->aof.fb.prev = block->aof.fb.prev;

    /* Check if block is not the first element of the list */
    if (block->aof.fb.prev != NULL)
        block->aof.fb.prev->aof.fb.next = block->aof.fb.next;

    dbg_printf("Removal complete.\n");
}

/*
 * extend_heap: Extends the heap with the requested number of bytes, and
 *              recreates epilogue header. Returns a pointer to the result of
 *              coalescing the newly-created block with previous free block, if
 *              applicable, or NULL in failure.
 */
static block_t *extend_heap(size_t asize) 
{
    dbg_printf("Called extend_heap(%zd)\n", asize);

    void *bp; // Pointer to start of new heap memory

    if ((bp = mem_sbrk(asize)) == (void *)-1)
        return NULL;
        
    /* Initialize new free block's header and footer */
    block_t *block = payload_to_header(bp);
    write_header(block, asize, false, get_alloc_prev(block));
    write_footer(block, asize, false);
    
    /* Create new epilogue header */
    block_t *block_next = find_next(block);
    write_header(block_next, 0, true, false);
    
    dbg_printf("extend_heap() successful.\n");

    /* Coalesce in case the previous block was free */
    return coalesce(block);
}

/* coalesce: Coalesces current block with previous and next blocks if
 *           either or both are unallocated; otherwise the block is not
 *           modified. Then, insert_list coalesced block into the segregated list.
 *           Returns pointer to the coalesced block. After coalescing, the
 *           immediate contiguous previous and next blocks must be allocated.
 */
static block_t *coalesce(block_t *block) 
{
    block_t *block_next = find_next(block);

    size_t size = get_size(block);
    bool next_alloc = get_alloc(block_next); // Allocation flag of next block
    bool prev_alloc = get_alloc_prev(block); // Allocation flag of previous block

    if (prev_alloc && next_alloc)              // Case 1
    {
        dbg_printf("coalesce: Case 1\n");
        /* Update size and write header & footer of block */
        write_header(block, size, false, true);
        write_footer(block, size, false);
        /* Insert the updated block into the list */
        insert_list(block);
    }

    else if (prev_alloc && !next_alloc)        // Case 2
    {
        dbg_printf("coalesce: Case 2\n");
        /* Remove the next block from the free list */
        remove_list(block_next);
        /* Update current block with the size of the next block */
        size += get_size(block_next);
        write_header(block, size, false, true);
        write_footer(block, size, false);
        /* Insert the updated block into the list */
        insert_list(block);
    }

    else if (!prev_alloc && next_alloc)        // Case 3
    {
        dbg_printf("coalesce: Case 3\n");
        /* Previous block is free, so can use its footer to get its header */
        block_t *block_prev = find_prev(block);
        /* Remove previous block from the seglist to add again later */
        remove_list(block_prev);
        /* Update size and write header & footer of previous block */
        size += get_size(block_prev);
        write_header(block_prev, size, false, get_alloc_prev(block_prev));
        write_footer(block_prev, size, false);
        /* Re-insert updated block_prev into seglist */
        insert_list(block_prev);
        /* Make returned block the previous block */
        block = block_prev;
    }

    else                                       // Case 4
    {
        dbg_printf("coalesce: Case 4\n");
        /* Previous block is free, so can use its footer to get its header */
        block_t *block_prev = find_prev(block);
        /* Remove next & previous blocks from their seg lists */
        remove_list(block_next);
        remove_list(block_prev);
        /* Update size and headers of the previous block */
        size += get_size(block_next) + get_size(block_prev);
        write_header(block_prev, size, false, get_alloc_prev(block_prev));
        write_footer(block_prev, size, false);
        /* Re-insert updated block_prev into seglist */
        insert_list(block_prev);
        /* Make returned block the previous block */
        block = block_prev;
    }

    /* Set previous block allocation flag of new next block to false */
    block_next = find_next(block);
    next_alloc = get_alloc(block_next); 
    size = get_size(block_next);
    write_header(block_next, size, next_alloc, false);

    return block;
}

/*
 * place: Places block with size of asize at the start of bp. If the remaining
 *        size is at least the minimum block size, then split the block to the
 *        the allocated block and the remaining block as free, which is then
 *        inserted into the explicit (segregated, hopefully, soon) list. 
 *        Requires that the block is initially unallocated.
 */
static void place(block_t *block, size_t asize)
{
    block_t *block_next;
    size_t csize = get_size(block);   // Current block size

    /* Block must be removed as it is still in its free list */
    remove_list(block);

    /* Splitting case */
    if ((csize - asize) >= min_block_size)
    {
        /* 
         * Splitting occurs when the difference between the current 
         * and requested block size is greater than or equal to the
         * minimum block size 
         */

        block_t *block_next_next; // The block after the new free block
        size_t next_next_size;
        bool next_next_alloc;

        /* 
         * Set block to allocated. We know the previous block is never free 
         * because of coalescing so we set the final paramater to "true".
         * And because of block specification, allocated blocks
         * have no footers so we only write to the header.
         */
        write_header(block, asize, true, true);

        /* Set size of next block to whatever is left after splitting */
        block_next = find_next(block);
        write_header(block_next, csize-asize, false, true);
        write_footer(block_next, csize-asize, false); // Free block *does* have a footer
        /* Insert the new splitted block into the free list */
        dbg_printf("Splitting occured: placing block_next in free list.\n");
        insert_list(block_next);

        /* Write to the previous allocation flag of the new free block's next block */
        block_next_next = find_next(block_next);
        next_next_size = get_size(block_next_next);
        next_next_alloc = get_alloc(block_next_next);
        write_header(block_next_next, next_next_size, next_next_alloc, false);

        dbg_printf("Placement successful.\n");
    }

    /* No splitting */
    else
    { 
        size_t next_size;
        bool next_alloc;

        /* Update header values (same details apply as previous case) */
        write_header(block, csize, true, true);

        /* Write to the previous allocation flag of the next block */
        block_next = find_next(block);
        next_size = get_size(block_next);
        next_alloc = get_alloc(block_next);
        write_header(block_next, next_size, next_alloc, true);
    }
}

/*
 * find_fit: Looks for a free block with at least asize bytes with
 *           first-fit policy. Returns NULL if none is found.
 */
static block_t *find_fit(size_t asize)
{
    dbg_printf("find_fit(%zd) called\n", asize);

    block_t *block; 
    size_t csize;

    /* Iterate through each segregated list */
    for (int i = get_seglist_size(asize); i < SEG_SIZE; i++)
    {
        block = seg_listsp[i];
        /* Iterate through the free blocks of seg_listsp[i] */
        while (block != NULL)
        {
            csize = get_size(block);
            /* If the shoe fits, return the block */
            if (asize <= csize)
            {
                dbg_printf("Free block found, removing it from list & returning.\n");
                return block;
            }
            
            block = block->aof.fb.next;
        }
    }

    dbg_printf("find_fit found no free block, returning NULL\n");
    /* No fit found */
    return NULL;
}

/*
 * get_seglist_size: returns the index of the which segregated list to 
 *                   start looking in for a free block of size asize. Each
 *                   seglist[n] holds blocks of sizes up to SIZE_LISTn so
 *                   we look for which one asize fits into.
 */
static int get_seglist_size (size_t asize) 
{
    int index;

    if (asize <= SIZE_LIST1)
        index = 0;
    else if ((SIZE_LIST1 < asize) && (asize <= SIZE_LIST2))
        index = 1;
    else if ((SIZE_LIST2 < asize) && (asize <= SIZE_LIST3))
        index = 2;
    else if ((SIZE_LIST3 < asize) && (asize <= SIZE_LIST4))
        index = 3;
    else if ((SIZE_LIST4 < asize) && (asize <= SIZE_LIST5))
        index = 4;
    else if ((SIZE_LIST5 < asize) && (asize <= SIZE_LIST6))
        index = 5;
    else if ((SIZE_LIST6 < asize) && (asize <= SIZE_LIST7))
        index = 6;
    else
        index = 7;

    return index;
}

/*
 * max: returns x if x > y, and y otherwise.
 */
static size_t max(size_t x, size_t y)
{
    return (x > y) ? x : y;
}

/*
 * round_up: Rounds size up to next multiple of n
 */
static size_t round_up(size_t size, size_t n)
{
    return (n * ((size + (n-1)) / n));
}

/*
 * pack: returns a header reflecting a specified size and its alloc status.
 *       If the block is allocated, the lowest bit is set to 1, and 0 otherwise.
 */
static word_t pack(size_t size, bool alloc, bool alloc_prev)
{
    if (alloc)
        size |= 1; // set alloc (lowest) bit of current block
    if (alloc_prev)
        size |= 2; // set alloc_prev (2nd lowest) bit of current block
    return size;
}

/*
 * extract_size: returns the size of a given header value based on the header
 *               specification above.
 */
static size_t extract_size(word_t word)
{
    return (word & ~((word_t)0xF));
}

/*
 * get_size: returns the size of a given block by clearing the lowest 4 bits
 *           (as the heap is 16-byte aligned).
 */
static size_t get_size(block_t *block)
{
    return extract_size(block->header);
}

/*
 * get_payload_size: returns the payload size of a given block, equal to
 *                   the entire block size minus the header size.
 */
static word_t get_payload_size(block_t *block)
{
    size_t asize = get_size(block);
    return asize - wsize;
}

/*
 * extract_alloc: returns the allocation status of a given header value based
 *                on the header specification above.
 */
static bool extract_alloc(word_t word)
{
    return (bool)(word & 0x1);
}

/*
 * extract_alloc_prev: returns the previous block's allocation status of a 
 *                     given header value based on the header specification 
 *                     above.
 */
static bool extract_alloc_prev(word_t word)
{
    return (bool)(word & 0x2);
}

/*
 * get_alloc: returns true when the block is allocated based on the
 *            block header's lowest bit, and false otherwise.
 */
static bool get_alloc(block_t *block)
{
    return extract_alloc(block->header);
}

/*
 * get_alloc_prev: returns true when the previous block is allocated based on the
 *                 block header's second-lowest bit, and false otherwise.
 */
static bool get_alloc_prev(block_t *block)
{
    return extract_alloc_prev(block->header);
}

/*
 * write_header: given a block and its size and allocation status,
 *               writes an appropriate value to the block header.
 */
static void write_header(block_t *block, size_t size, bool alloc, bool alloc_prev)
{
    block->header = pack(size, alloc, alloc_prev);
}

/*
 * write_footer: given a block and its size and allocation status,
 *               writes an appropriate value to the block footer by first
 *               computing the position of the footer.
 */
static void write_footer(block_t *block, size_t size, bool alloc)
{
    word_t *footerp = (word_t *)((block->aof.payload) + get_size(block) - dsize);
    *footerp = pack(size, alloc, true); // Last value doesn't actually matter
}

/*
 * find_next: returns the next consecutive block on the heap by adding the
 *            size of the block.
 */
static block_t *find_next(block_t *block)
{
    return (block_t *)(((char *)block) + get_size(block));
}

/*
 * find_prev_footer: returns the footer of the previous block.
 */
static word_t *find_prev_footer(block_t *block)
{
    /* Compute previous footer position as one word before the header */
    return (&(block->header)) - 1;
}

/*
 * find_prev: returns the previous block position by checking the previous
 *            block's footer and calculating the start of the previous block
 *            based on its size.
 */
static block_t *find_prev(block_t *block)
{
    word_t *footerp = find_prev_footer(block);
    size_t size = extract_size(*footerp);
    return (block_t *)((char *)block - size);
}

/*
 * payload_to_header: given a payload pointer, returns a pointer to the
 *                    corresponding block.
 */
static block_t *payload_to_header(void *bp)
{
    return (block_t *)(((char *)bp) - offsetof(block_t, aof.payload));
}

/*
 * header_to_payload: given a block pointer, returns a pointer to the
 *                    corresponding payload.
 */
static void *header_to_payload(block_t *block)
{
    return (void *)(block->aof.payload);
}
