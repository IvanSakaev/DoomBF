#include "doom_crt.h"
#include "doom_env.h"

typedef struct _HeapBlockLink
{
    struct _HeapBlockLink* pxNextFreeBlock; /**< The next free block in the list. */
    size_t xBlockSize;                     /**< The size of the free block. */
} HeapBlockLink;

typedef struct _HeapCfg
{
    /* Create a couple of list links to mark the start and end of the list. */
    HeapBlockLink HeapStart;
    HeapBlockLink* pHeapEnd;

    /* Keeps track of the number of calls to allocate and free memory as well as the
     * number of free bytes remaining, but says nothing about fragmentation. */
    size_t HeapFreeBytesRemaining;
    size_t HeapMinimumEverFreeBytesRemaining;
    size_t HeapNumberOfSuccessfulAllocations;
    size_t HeapNumberOfSuccessfulFrees;

    void (*pAssert)(const char *text, int line);
} HeapCfg;

typedef struct _HeapRegion
{
    uint8_t* pucStartAddress;
    size_t   xSizeInBytes;
} HeapRegion;

void Heap_ResetState(HeapCfg *p_heap);
void Heap_DefineHeapRegions(HeapCfg* p_heap, const HeapRegion* const pxHeapRegions);
void* Heap_Calloc(HeapCfg* p_heap, size_t xNum, size_t xSize);
size_t Heap_GetMinimumEverFreeHeapSize(HeapCfg* p_heap);
size_t Heap_GetFreeHeapSize(HeapCfg* p_heap);
void Heap_Free(HeapCfg* p_heap, void* pv);
void* Heap_Malloc(HeapCfg* p_heap, size_t xWantedSize);

/*
 * A sample implementation of pvPortMalloc() that allows the heap to be defined
 * across multiple non-contiguous blocks and combines (coalescences) adjacent
 * memory blocks as they are freed.
 *
 * See heap_1.c, heap_2.c, heap_3.c and heap_4.c for alternative
 * implementations, and the memory management pages of https://www.FreeRTOS.org
 * for more information.
 *
 * Usage notes:
 *
 * vPortDefineHeapRegions() ***must*** be called before pvPortMalloc().
 * pvPortMalloc() will be called if any task objects (tasks, queues, event
 * groups, etc.) are created, therefore vPortDefineHeapRegions() ***must*** be
 * called before any other objects are defined.
 *
 * vPortDefineHeapRegions() takes a single parameter.  The parameter is an array
 * of HeapRegion_t structures.  HeapRegion_t is defined in portable.h as
 *
 * typedef struct HeapRegion
 * {
 *  uint8_t *pucStartAddress; << Start address of a block of memory that will be part of the heap.
 *  size_t xSizeInBytes;      << Size of the block of memory.
 * } HeapRegion_t;
 *
 * The array is terminated using a NULL zero sized region definition, and the
 * memory regions defined in the array ***must*** appear in address order from
 * low address to high address.  So the following is a valid example of how
 * to use the function.
 *
 * HeapRegion_t xHeapRegions[] =
 * {
 *  { ( uint8_t * ) 0x80000000UL, 0x10000 }, << Defines a block of 0x10000 bytes starting at address 0x80000000
 *  { ( uint8_t * ) 0x90000000UL, 0xa0000 }, << Defines a block of 0xa0000 bytes starting at address of 0x90000000
 *  { NULL, 0 }                << Terminates the array.
 * };
 *
 * vPortDefineHeapRegions( xHeapRegions ); << Pass the array into vPortDefineHeapRegions().
 *
 * Note 0x80000000 is the lower address so appears in the array first.
 *
 */

 /* Defining MPU_WRAPPERS_INCLUDED_FROM_API_FILE prevents task.h from redefining
  * all the API functions to use the MPU wrappers.  That should only be done when
  * task.h is included from an application file. */


//OS_ASSERT

#ifdef _WIN32
#define portPOINTER_SIZE_TYPE    uint64_t
#else
#ifdef _BF
#define portPOINTER_SIZE_TYPE    uint32_t
#else
#define portPOINTER_SIZE_TYPE    uint64_t
#endif
#endif

#define HEAP_BYTE_ALIGNMENT         8
#define HEAP_BYTE_ALIGNMENT_MASK    (0x0007)
#ifndef HEAP_CLEAR_MEMORY_ON_FREE
#define HEAP_CLEAR_MEMORY_ON_FREE    0
#endif

/* Block sizes must not get too small. */
#define heapMINIMUM_BLOCK_SIZE    ( ( size_t ) ( HEAP_STRUCT_SIZE << 1 ) )

/* Assumes 8bit bytes! */
#define heapBITS_PER_BYTE         ( ( size_t ) 8 )

/* Max value that fits in a size_t type. */
#define heapSIZE_MAX              ( ~( ( size_t ) 0 ) )

/* Check if multiplying a and b will result in overflow. */
#define heapMULTIPLY_WILL_OVERFLOW( a, b )     ( ( ( a ) > 0 ) && ( ( b ) > ( heapSIZE_MAX / ( a ) ) ) )

/* Check if adding a and b will result in overflow. */
#define heapADD_WILL_OVERFLOW( a, b )          ( ( a ) > ( heapSIZE_MAX - ( b ) ) )

/* Check if the subtraction operation ( a - b ) will result in underflow. */
#define heapSUBTRACT_WILL_UNDERFLOW( a, b )    ( ( a ) < ( b ) )

/* MSB of the xBlockSize member of an HeapBlockLink structure is used to track
 * the allocation status of a block.  When MSB of the xBlockSize member of
 * an HeapBlockLink structure is set then the block belongs to the application.
 * When the bit is free the block is still part of the free heap space. */
#define heapBLOCK_ALLOCATED_BITMASK    ( ( ( size_t ) 1 ) << ( ( sizeof( size_t ) * heapBITS_PER_BYTE ) - 1 ) )
#define heapBLOCK_SIZE_IS_VALID( xBlockSize )    ( ( ( xBlockSize ) & heapBLOCK_ALLOCATED_BITMASK ) == 0 )
#define heapBLOCK_IS_ALLOCATED( pxBlock )        ( ( ( pxBlock->xBlockSize ) & heapBLOCK_ALLOCATED_BITMASK ) != 0 )
#define heapALLOCATE_BLOCK( pxBlock )            ( ( pxBlock->xBlockSize ) |= heapBLOCK_ALLOCATED_BITMASK )
#define heapFREE_BLOCK( pxBlock )                ( ( pxBlock->xBlockSize ) &= ~heapBLOCK_ALLOCATED_BITMASK )

 
#define heapPROTECT_BLOCK_POINTER( pxBlock )    ( pxBlock )

#define heapVALIDATE_BLOCK_POINTER( pxBlock )

#define HEAP_ASSERT(abc) if (!(abc)) { if (p_heap && p_heap->pAssert) {p_heap->pAssert(#abc, __LINE__);} }

/*
 * Inserts a block of memory that is being freed into the correct position in
 * the list of free memory blocks.  The block being freed will be merged with
 * the block in front it and/or the block behind it if the memory blocks are
 * adjacent to each other.
 */
static void Heap_InsertBlockIntoFreeList(HeapCfg* p_heap, HeapBlockLink* pxBlockToInsert);
void Heap_DefineRegions(HeapCfg* p_heap, const HeapRegion* const pxHeapRegions);


/* The size of the structure placed at the beginning of each allocated memory
 * block must by correctly byte aligned. */
static const size_t HEAP_STRUCT_SIZE = (sizeof(HeapBlockLink) + ((size_t)(HEAP_BYTE_ALIGNMENT - 1))) & ~((size_t)HEAP_BYTE_ALIGNMENT_MASK);


/*-----------------------------------------------------------*/

void* Heap_Malloc(HeapCfg * p_heap, size_t xWantedSize)
{
    HeapBlockLink* pxBlock = NULL;
    HeapBlockLink* pxPreviousBlock = NULL;
    HeapBlockLink* pxNewBlockLink = NULL;
    void* pvReturn = NULL;
    size_t xAdditionalRequiredSize= 0;

    /* The heap must be initialised before the first call to
     * pvPortMalloc(). */
    HEAP_ASSERT(p_heap->pHeapEnd);

    if (xWantedSize > 0)
    {
        /* The wanted size must be increased so it can contain a HeapBlockLink
         * structure in addition to the requested amount of bytes. */
        if (heapADD_WILL_OVERFLOW(xWantedSize, HEAP_STRUCT_SIZE) == 0)
        {
            xWantedSize += HEAP_STRUCT_SIZE;

            /* Ensure that blocks are always aligned to the required number
             * of bytes. */
            if ((xWantedSize & HEAP_BYTE_ALIGNMENT_MASK) != 0x00)
            {
                /* Byte alignment required. */
                xAdditionalRequiredSize = HEAP_BYTE_ALIGNMENT - (xWantedSize & HEAP_BYTE_ALIGNMENT_MASK);

                if (heapADD_WILL_OVERFLOW(xWantedSize, xAdditionalRequiredSize) == 0)
                {
                    xWantedSize += xAdditionalRequiredSize;
                }
                else
                {
                    xWantedSize = 0;
                }
            }
        }
        else
        {
            xWantedSize = 0;
        }
    }

    {
        /* Check the block size we are trying to allocate is not so large that the
         * top bit is set.  The top bit of the block size member of the HeapBlockLink
         * structure is used to determine who owns the block - the application or
         * the kernel, so it must be free. */
        if (heapBLOCK_SIZE_IS_VALID(xWantedSize) != 0)
        {
            if ((xWantedSize > 0) && (xWantedSize <= p_heap->HeapFreeBytesRemaining))
            {
                /* Traverse the list from the start (lowest address) block until
                 * one of adequate size is found. */
                pxPreviousBlock = &p_heap->HeapStart;
                pxBlock = heapPROTECT_BLOCK_POINTER(p_heap->HeapStart.pxNextFreeBlock);
                heapVALIDATE_BLOCK_POINTER(pxBlock);

                while ((pxBlock->xBlockSize < xWantedSize) && (pxBlock->pxNextFreeBlock != heapPROTECT_BLOCK_POINTER(NULL)))
                {
                    pxPreviousBlock = pxBlock;
                    pxBlock = heapPROTECT_BLOCK_POINTER(pxBlock->pxNextFreeBlock);
                    heapVALIDATE_BLOCK_POINTER(pxBlock);
                }

                /* If the end marker was reached then a block of adequate size
                 * was not found. */
                if (pxBlock != p_heap->pHeapEnd)
                {
                    /* Return the memory space pointed to - jumping over the
                     * HeapBlockLink structure at its start. */
                    pvReturn = (void*)(((uint8_t*)heapPROTECT_BLOCK_POINTER(pxPreviousBlock->pxNextFreeBlock)) + HEAP_STRUCT_SIZE);
                    heapVALIDATE_BLOCK_POINTER(pvReturn);

                    /* This block is being returned for use so must be taken out
                     * of the list of free blocks. */
                    pxPreviousBlock->pxNextFreeBlock = pxBlock->pxNextFreeBlock;

                    /* If the block is larger than required it can be split into
                     * two. */
                    HEAP_ASSERT(heapSUBTRACT_WILL_UNDERFLOW(pxBlock->xBlockSize, xWantedSize) == 0);

                    if ((pxBlock->xBlockSize - xWantedSize) > heapMINIMUM_BLOCK_SIZE)
                    {
                        /* This block is to be split into two.  Create a new
                         * block following the number of bytes requested. The void
                         * cast is used to prevent byte alignment warnings from the
                         * compiler. */
                        pxNewBlockLink = (void*)(((uint8_t*)pxBlock) + xWantedSize);
                        HEAP_ASSERT((((portPOINTER_SIZE_TYPE)pxNewBlockLink) & HEAP_BYTE_ALIGNMENT_MASK) == 0);

                        /* Calculate the sizes of two blocks split from the
                         * single block. */
                        pxNewBlockLink->xBlockSize = pxBlock->xBlockSize - xWantedSize;
                        pxBlock->xBlockSize = xWantedSize;

                        /* Insert the new block into the list of free blocks. */
                        pxNewBlockLink->pxNextFreeBlock = pxPreviousBlock->pxNextFreeBlock;
                        pxPreviousBlock->pxNextFreeBlock = heapPROTECT_BLOCK_POINTER(pxNewBlockLink);
                    }

                    p_heap->HeapFreeBytesRemaining -= pxBlock->xBlockSize;

                    if (p_heap->HeapFreeBytesRemaining < p_heap->HeapMinimumEverFreeBytesRemaining)
                    {
                        p_heap->HeapMinimumEverFreeBytesRemaining = p_heap->HeapFreeBytesRemaining;
                    }

                    /* The block is being returned - it is allocated and owned
                     * by the application and has no "next" block. */
                    heapALLOCATE_BLOCK(pxBlock);
                    pxBlock->pxNextFreeBlock = NULL;
                    p_heap->HeapNumberOfSuccessfulAllocations++;
                }
            }
        }

        //traceMALLOC(pvReturn, xWantedSize);
    }
    HEAP_ASSERT((((portPOINTER_SIZE_TYPE)pvReturn) & (portPOINTER_SIZE_TYPE)HEAP_BYTE_ALIGNMENT_MASK) == 0);
    if (xWantedSize != 0 && pvReturn == NULL)
    {
        printf("\nFATAL ERROR: HEAP: FAILED TO ALLOCATE %u!", xWantedSize);
        //asm_cli();
        while(10);
    }
    return pvReturn;
}
/*-----------------------------------------------------------*/

void Heap_Free(HeapCfg* p_heap, void* pv)
{
    uint8_t* puc = (uint8_t*)pv;
    HeapBlockLink* pxLink;

    if (pv != NULL)
    {
        /* The memory being freed will have an HeapBlockLink structure immediately
         * before it. */
        puc -= HEAP_STRUCT_SIZE;

        /* This casting is to keep the compiler from issuing warnings. */
        pxLink = (void*)puc;

        heapVALIDATE_BLOCK_POINTER(pxLink);
        HEAP_ASSERT(heapBLOCK_IS_ALLOCATED(pxLink) != 0);
        HEAP_ASSERT(pxLink->pxNextFreeBlock == NULL);

        if (heapBLOCK_IS_ALLOCATED(pxLink) != 0)
        {
            if (pxLink->pxNextFreeBlock == NULL)
            {
                /* The block is being returned to the heap - it is no longer
                 * allocated. */
                heapFREE_BLOCK(pxLink);
                {
                    /* Add this block to the list of free blocks. */
                    p_heap->HeapFreeBytesRemaining += pxLink->xBlockSize;
                    //traceFREE(pv, pxLink->xBlockSize);
                    Heap_InsertBlockIntoFreeList(p_heap, (HeapBlockLink*)pxLink);
                    p_heap->HeapNumberOfSuccessfulFrees++;
                }
            }
        }
    }
}
/*-----------------------------------------------------------*/

size_t Heap_GetFreeHeapSize(HeapCfg* p_heap)
{
    return p_heap->HeapFreeBytesRemaining;
}
/*-----------------------------------------------------------*/

size_t Heap_GetMinimumEverFreeHeapSize(HeapCfg* p_heap)
{
    return p_heap->HeapMinimumEverFreeBytesRemaining;
}
/*-----------------------------------------------------------*/

void* Heap_Calloc(HeapCfg* p_heap, size_t xNum, size_t xSize)
{
    void* pv = NULL;

    if (heapMULTIPLY_WILL_OVERFLOW(xNum, xSize) == 0)
    {
        pv = Heap_Malloc(p_heap, xNum * xSize);

        if (pv != NULL)
        {
            (void)memset(pv, 0, xNum * xSize);
        }
    }

    return pv;
}
/*-----------------------------------------------------------*/

static void Heap_InsertBlockIntoFreeList(HeapCfg* p_heap, HeapBlockLink* pxBlockToInsert) /* PRIVILEGED_FUNCTION */
{
    HeapBlockLink* pxIterator = NULL;
    uint8_t* puc = NULL;

    /* Iterate through the list until a block is found that has a higher address
     * than the block being inserted. */
    for (pxIterator = &p_heap->HeapStart; heapPROTECT_BLOCK_POINTER(pxIterator->pxNextFreeBlock) < pxBlockToInsert; 
        pxIterator = heapPROTECT_BLOCK_POINTER(pxIterator->pxNextFreeBlock))
    {
        /* Nothing to do here, just iterate to the right position. */
    }

    if (pxIterator != &p_heap->HeapStart)
    {
        heapVALIDATE_BLOCK_POINTER(pxIterator);
    }

    /* Do the block being inserted, and the block it is being inserted after
     * make a contiguous block of memory? */
    puc = (uint8_t*)pxIterator;

    if ((puc + pxIterator->xBlockSize) == (uint8_t*)pxBlockToInsert)
    {
        pxIterator->xBlockSize += pxBlockToInsert->xBlockSize;
        pxBlockToInsert = pxIterator;
    }

    /* Do the block being inserted, and the block it is being inserted before
     * make a contiguous block of memory? */
    puc = (uint8_t*)pxBlockToInsert;

    if ((puc + pxBlockToInsert->xBlockSize) == (uint8_t*)heapPROTECT_BLOCK_POINTER(pxIterator->pxNextFreeBlock))
    {
        if (heapPROTECT_BLOCK_POINTER(pxIterator->pxNextFreeBlock) != p_heap->pHeapEnd)
        {
            /* Form one big block from the two blocks. */
            pxBlockToInsert->xBlockSize += heapPROTECT_BLOCK_POINTER(pxIterator->pxNextFreeBlock)->xBlockSize;
            pxBlockToInsert->pxNextFreeBlock = heapPROTECT_BLOCK_POINTER(pxIterator->pxNextFreeBlock)->pxNextFreeBlock;
        }
        else
        {
            pxBlockToInsert->pxNextFreeBlock = heapPROTECT_BLOCK_POINTER(p_heap->pHeapEnd);
        }
    }
    else
    {
        pxBlockToInsert->pxNextFreeBlock = pxIterator->pxNextFreeBlock;
    }

    /* If the block being inserted plugged a gap, so was merged with the block
     * before and the block after, then it's pxNextFreeBlock pointer will have
     * already been set, and should not be set here as that would make it point
     * to itself. */
    if (pxIterator != pxBlockToInsert)
    {
        pxIterator->pxNextFreeBlock = heapPROTECT_BLOCK_POINTER(pxBlockToInsert);
    }
}
/*-----------------------------------------------------------*/

void Heap_DefineHeapRegions(HeapCfg* p_heap, const HeapRegion* const pxHeapRegions)
{
    HeapBlockLink* pxFirstFreeBlockInRegion = NULL;
    HeapBlockLink* pxPreviousFreeBlock;
    portPOINTER_SIZE_TYPE xAlignedHeap;
    size_t xTotalRegionSize, xTotalHeapSize = 0;
    int xDefinedRegions = 0;
    portPOINTER_SIZE_TYPE xAddress;
    const HeapRegion* pxHeapRegion;

    /* Can only call once! */
    HEAP_ASSERT(p_heap->pHeapEnd == NULL);

    pxHeapRegion = &(pxHeapRegions[xDefinedRegions]);

    while (pxHeapRegion->xSizeInBytes > 0)
    {
        xTotalRegionSize = pxHeapRegion->xSizeInBytes;

        /* Ensure the heap region starts on a correctly aligned boundary. */
        xAddress = (portPOINTER_SIZE_TYPE)pxHeapRegion->pucStartAddress;

        if ((xAddress & HEAP_BYTE_ALIGNMENT_MASK) != 0)
        {
            xAddress += (HEAP_BYTE_ALIGNMENT - 1);
            xAddress &= ~(portPOINTER_SIZE_TYPE)HEAP_BYTE_ALIGNMENT_MASK;

            /* Adjust the size for the bytes lost to alignment. */
            xTotalRegionSize -= (size_t)(xAddress - (portPOINTER_SIZE_TYPE)pxHeapRegion->pucStartAddress);
        }

        xAlignedHeap = xAddress;

        /* Set p_heap->HeapStart if it has not already been set. */
        if (xDefinedRegions == 0)
        {
            /* p_heap->HeapStart is used to hold a pointer to the first item in the list of
             *  free blocks.  The void cast is used to prevent compiler warnings. */
            p_heap->HeapStart.pxNextFreeBlock = (HeapBlockLink*)heapPROTECT_BLOCK_POINTER(xAlignedHeap);
            p_heap->HeapStart.xBlockSize = (size_t)0;
        }
        else
        {
            /* Should only get here if one region has already been added to the
             * heap. */
            HEAP_ASSERT(p_heap->pHeapEnd != heapPROTECT_BLOCK_POINTER(NULL));

            /* Check blocks are passed in with increasing start addresses. */
            HEAP_ASSERT((portPOINTER_SIZE_TYPE)xAddress > (portPOINTER_SIZE_TYPE)p_heap->pHeapEnd);
        }

        /* Remember the location of the end marker in the previous region, if
         * any. */
        pxPreviousFreeBlock = p_heap->pHeapEnd;

        /* p_heap->pHeapEnd is used to mark the end of the list of free blocks and is
         * inserted at the end of the region space. */
        xAddress = xAlignedHeap + (portPOINTER_SIZE_TYPE)xTotalRegionSize;
        xAddress -= (portPOINTER_SIZE_TYPE)HEAP_STRUCT_SIZE;
        xAddress &= ~((portPOINTER_SIZE_TYPE)HEAP_BYTE_ALIGNMENT_MASK);
        p_heap->pHeapEnd = (HeapBlockLink*)xAddress;
        p_heap->pHeapEnd->xBlockSize = 0;
        p_heap->pHeapEnd->pxNextFreeBlock = heapPROTECT_BLOCK_POINTER(NULL);

        /* To start with there is a single free block in this region that is
         * sized to take up the entire heap region minus the space taken by the
         * free block structure. */
        pxFirstFreeBlockInRegion = (HeapBlockLink*)xAlignedHeap;
        pxFirstFreeBlockInRegion->xBlockSize = (size_t)(xAddress - (portPOINTER_SIZE_TYPE)pxFirstFreeBlockInRegion);
        pxFirstFreeBlockInRegion->pxNextFreeBlock = heapPROTECT_BLOCK_POINTER(p_heap->pHeapEnd);

        /* If this is not the first region that makes up the entire heap space
         * then link the previous region to this region. */
        if (pxPreviousFreeBlock != NULL)
        {
            pxPreviousFreeBlock->pxNextFreeBlock = heapPROTECT_BLOCK_POINTER(pxFirstFreeBlockInRegion);
        }

        xTotalHeapSize += pxFirstFreeBlockInRegion->xBlockSize;

        /* Move onto the next HeapRegion_t structure. */
        xDefinedRegions++;
        pxHeapRegion = &(pxHeapRegions[xDefinedRegions]);
    }

    p_heap->HeapMinimumEverFreeBytesRemaining = xTotalHeapSize;
    p_heap->HeapFreeBytesRemaining = xTotalHeapSize;

    /* Check something was actually defined before it is accessed. */
    HEAP_ASSERT(xTotalHeapSize);
}

/*
 * Reset the state in this file. This state is normally initialized at start up.
 * This function must be called by the application before restarting the
 * scheduler.
 */
void Heap_ResetState(HeapCfg* p_heap)
{
    p_heap->pHeapEnd = NULL;

    p_heap->HeapFreeBytesRemaining = (size_t)0U;
    p_heap->HeapMinimumEverFreeBytesRemaining = (size_t)0U;
    p_heap->HeapNumberOfSuccessfulAllocations = (size_t)0U;
    p_heap->HeapNumberOfSuccessfulFrees = (size_t)0U;
}

HeapCfg g_Heap;
void EnvHeapSetup()
{
    const HeapRegion g_xHeapRegions[] =
    {
    { ( uint8_t * ) g_DoomHeapAddress, g_DoomHeapSize }, 
    { NULL, 0 }                
    };
    Heap_ResetState(&g_Heap);
    Heap_DefineHeapRegions(&g_Heap,  (const HeapRegion* const)&g_xHeapRegions);
}

void EnvHeapFree(void* pv)
{
    Heap_Free(&g_Heap, pv);
}

void* EnvHeapMalloc(size_t xWantedSize)
{
    return Heap_Malloc(&g_Heap, xWantedSize);
}
