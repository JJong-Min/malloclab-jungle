#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "memlib.h"
#include "mm.h"

team_t team = {
    /* Team name */
    "jungle_2nd",
    /* First member's full name */
    "Ratel",
    /* First member's email address */
    "ksaul77@gmail.com",
    /* Second member's full name (leave blank if none) */
    "",
    /* Second member's email address (leave blank if none) */
    ""
};

#define WSIZE 4
#define DSIZE 8
#define CHUNKSIZE (1 << 12)	
#define MAX(x, y) ((x) > (y) ? (x) : (y))
#define PACK(size, alloc) ((size) | (alloc))
#define GET(p) (*(unsigned int *)(p))
#define PUT(p, val) (*(unsigned int *)(p) = (val))
#define GET_SIZE(p)  (GET(p) & ~0x7)
#define GET_ALLOC(p) (GET(p) & 0x1)
#define HDRP(bp) ((void *)(bp) - WSIZE) 								// header를 가리키는 pointer
#define FTRP(bp) ((void *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE) 			//footer를 가리키는 pointer
#define NEXT_BLK(bp) ((void *)(bp) + GET_SIZE(HDRP(bp))) 				//다음 블록의 bp
#define PREV_BLK(bp) ((void *)(bp) - GET_SIZE((void *)(bp)-DSIZE))		//이전 블록의 bp
#define GET_NEXT_PTR(bp) (*(char **)(bp + WSIZE))                       // 이중포인터 (char **)인 bp가 가리키는 주소에 접근하여 bp + WSIZE에 위치한 값 읽어오기
#define GET_PREV_PTR(bp) (*(char **)(bp))                               // 이중포인터 (char **)인 bp가 가리키는 주소에 접근하여 값 읽어오기
#define SET_NEXT_PTR(bp, qp) (GET_NEXT_PTR(bp) = qp)                    //이중포인터 (char **)인 bp가 가리키는 주소에 접근하여 bp + WSIZE 위치에 qp값 넣기
#define SET_PREV_PTR(bp, qp) (GET_PREV_PTR(bp) = qp)                    //이중포인터 (char **)인 bp가 가리키는 주소에 접근하여 bp 위치에 qp값 넣기

static void *extend_heap(size_t words);
static void *coalesce(void *bp);
static void place(void *bp, size_t asize);
static void *find_fit(size_t asize);
static void insert_in_free_list(void *bp);
static void remove_from_free_list(void *bp);
static char *heap_listp = NULL;
static char *free_list_start = NULL;

int mm_init(void)
{
	if ((heap_listp = mem_sbrk(6 * WSIZE)) == NULL) 
		return -1;

	PUT(heap_listp, 0);							
	PUT(heap_listp + (1 * WSIZE), PACK(2*DSIZE, 1)); 
	PUT(heap_listp + (2 * WSIZE), 0);
	PUT(heap_listp + (3 * WSIZE), 0);

	PUT(heap_listp + (4 * WSIZE), PACK(2*DSIZE, 1)); 
	PUT(heap_listp + (5 * WSIZE), PACK(0, 1));	 
	free_list_start = heap_listp + 2 * WSIZE;

	if (extend_heap(4) == NULL)
	{
		return -1;
	}
	return 0;
}

static void *extend_heap(size_t words)
{
	char *bp;
	size_t size;

	size = (words % 2) ? (words + 1) * WSIZE : words * WSIZE;
	if (size < 16)
	{
		size = 16;
	}

	if ((int)(bp = mem_sbrk(size)) == -1)
	{
		return NULL;
	}

	PUT(HDRP(bp), PACK(size, 0));		 
	PUT(FTRP(bp), PACK(size, 0));		
	PUT(HDRP(NEXT_BLK(bp)), PACK(0, 1)); 
	return coalesce(bp);
}

static void *coalesce(void *bp)
{    
	size_t NEXT_ALLOC = GET_ALLOC(HDRP(NEXT_BLK(bp)));
	size_t PREV_ALLOC = GET_ALLOC(FTRP(PREV_BLK(bp))); 
	size_t size = GET_SIZE(HDRP(bp));

	
	if (PREV_ALLOC && !NEXT_ALLOC)
	{
		size += GET_SIZE(HDRP(NEXT_BLK(bp)));
		remove_from_free_list(NEXT_BLK(bp));
		PUT(HDRP(bp), PACK(size, 0));
		PUT(FTRP(bp), PACK(size, 0));
	}
	
	else if (!PREV_ALLOC && NEXT_ALLOC)
	{
		size += GET_SIZE(HDRP(PREV_BLK(bp)));
		remove_from_free_list(PREV_BLK(bp));
		bp = PREV_BLK(bp); 
		PUT(HDRP(bp), PACK(size, 0));
		PUT(FTRP(bp), PACK(size, 0));
	}
	
	else if (!PREV_ALLOC && !NEXT_ALLOC)
	{
		size += GET_SIZE(HDRP(PREV_BLK(bp))) + GET_SIZE(HDRP(NEXT_BLK(bp)));
		remove_from_free_list(PREV_BLK(bp));
		remove_from_free_list(NEXT_BLK(bp));
		bp = PREV_BLK(bp); 
		PUT(HDRP(bp), PACK(size, 0));
		PUT(FTRP(bp), PACK(size, 0));
	}
	insert_in_free_list(bp);
	return bp;
}

void *mm_malloc(size_t size)
{
	size_t asize;	   
	size_t extendsize;
	void *bp;

	if (size == 0)
		return (NULL);

	if (size <= DSIZE)
		asize = 2 * DSIZE;
	else
		asize = DSIZE * ((size + DSIZE + (DSIZE - 1)) / DSIZE);

	if ((bp = find_fit(asize)) != NULL)
	{
		place(bp, asize);
		return (bp);
	}

	extendsize = MAX(asize, CHUNKSIZE);
	if ((bp = extend_heap(extendsize / WSIZE)) == NULL)
		return (NULL);
	place(bp, asize);
	return (bp);
}

static void *find_fit(size_t asize)
{
	void *bp;
	// first fit
	for (bp = free_list_start; GET_ALLOC(HDRP(bp)) == 0; bp = GET_NEXT_PTR(bp))
	{
		if (asize <= (size_t)GET_SIZE(HDRP(bp)))
		{
			return bp;
		}
	}
	return NULL;
}

static void place(void *bp, size_t asize)
{
	size_t csize = GET_SIZE(HDRP(bp));

	if ((csize - asize) >= 4 * WSIZE)
	{
		PUT(HDRP(bp), PACK(asize, 1));
		PUT(FTRP(bp), PACK(asize, 1));
		remove_from_free_list(bp);
		bp = NEXT_BLK(bp);
		PUT(HDRP(bp), PACK(csize - asize, 0));
		PUT(FTRP(bp), PACK(csize - asize, 0));
		coalesce(bp);
	}
	else
	{
		PUT(HDRP(bp), PACK(csize, 1));
		PUT(FTRP(bp), PACK(csize, 1));
		remove_from_free_list(bp);
	}
}

static void insert_in_free_list(void *bp)
{
	SET_NEXT_PTR(bp, free_list_start);
	SET_PREV_PTR(free_list_start, bp);
	SET_PREV_PTR(bp, NULL);
	free_list_start = bp;
}

static void remove_from_free_list(void *bp)
{
	if (GET_PREV_PTR(bp))
		SET_NEXT_PTR(GET_PREV_PTR(bp), GET_NEXT_PTR(bp)); 
	else												  
		free_list_start = GET_NEXT_PTR(bp);				  
	SET_PREV_PTR(GET_NEXT_PTR(bp), GET_PREV_PTR(bp));
}

void mm_free(void *bp)
{
	size_t size;
	if (bp == NULL)
		return;

	size = GET_SIZE(HDRP(bp));
	PUT(HDRP(bp), PACK(size, 0));
	PUT(FTRP(bp), PACK(size, 0));
	coalesce(bp);
}

void *mm_realloc(void *bp, size_t size)
{
	if ((int)size < 0)
		return NULL;
	else if ((int)size == 0)
	{
		mm_free(bp);
		return NULL;
	}
	else if (size > 0)
	{
		size_t oldsize = GET_SIZE(HDRP(bp));
		size_t newsize = size + (2 * WSIZE);
		
		if (newsize <= oldsize)
		{
			return bp;
		}
		
		else
		{
			size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLK(bp)));
			size_t csize;
			
			if (!next_alloc && ((csize = oldsize + GET_SIZE(HDRP(NEXT_BLK(bp))))) >= newsize)
			{
				remove_from_free_list(NEXT_BLK(bp));
				PUT(HDRP(bp), PACK(csize, 1));
				PUT(FTRP(bp), PACK(csize, 1));
				return bp;
			}
			
			else
			{
				void *new_ptr = mm_malloc(newsize);
				place(new_ptr, newsize);
				memcpy(new_ptr, bp, newsize);
				mm_free(bp);
				return new_ptr;
			}
		}
	}
	else
		return NULL;
}