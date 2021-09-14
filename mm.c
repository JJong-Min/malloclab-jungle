#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "memlib.h"
#include "mm.h"

team_t team = {
	"Malloc lab",
	"Seung    ",
	"20210120",
	"Huh ",
	"20210120"
	};

#define WSIZE 4                     // word size 4로 지정
#define DSIZE (2 * WSIZE)           // double word size 8로 지정
#define CHUNKSIZE (1 << 12)         // heap size를 늘릴 때 사용할 chunksize 지정
#define MAX(x, y) ((x) > (y) ? (x) : (y))
#define PACK(size, alloc) ((size) | (alloc))
#define GET(p) (*(unsigned int *)(p))
#define PUT(p, val) (*(unsigned int *)(p) = (val))
#define GET_SIZE(p)  (GET(p) & ~0x7)
#define GET_ALLOC(p) (GET(p) & 0x1)
#define HDRP(bp) ((void *)(bp) - WSIZE)
#define FTRP(bp) ((void *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)
#define NEXT_BLK(bp) ((void *)(bp) + GET_SIZE(HDRP(bp)))
#define PREV_BLK(bp) ((void *)(bp) - GET_SIZE((void *)(bp)-DSIZE))

#define GET_NEXT_PTR(bp) (*(char **)(bp + WSIZE))
#define GET_PREV_PTR(bp) (*(char **)(bp))
#define SET_NEXT_PTR(bp, qp) (GET_NEXT_PTR(bp) = qp)
#define SET_PREV_PTR(bp, qp) (GET_PREV_PTR(bp) = qp)


static char *heap_listp = 0;
static char *free_list_start = 0;
static void *coalesce(void *bp);
static void *extend_heap(size_t words);
static void *find_fit(size_t asize);
static void place(void *bp, size_t asize);
static void insert_in_free_list(void *bp);
static void remove_from_free_list(void *bp);
static void checkblock(void *bp);
static void checkheap(bool verbose);
static void printblock(void *bp);

int mm_init(void)
{
	/* Create the initial empty heap. */
	if ((heap_listp = mem_sbrk(6 * WSIZE)) == NULL) 
		return -1;

	PUT(heap_listp, 0);							   /* padding */
	PUT(heap_listp + (1 * WSIZE), PACK(2*DSIZE, 1)); /* Prologue header */
	PUT(heap_listp + (2 * WSIZE), 0);
	PUT(heap_listp + (3 * WSIZE), 0);
	PUT(heap_listp + (4 * WSIZE), PACK(2*DSIZE, 1)); /* Prologue footer */
	PUT(heap_listp + (5 * WSIZE), PACK(0, 1));	   /* Epilogue header */
	free_list_start = heap_listp + 2 * WSIZE;
	/* Extend the empty heap with a free block of minimum possible block size */
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
	// minimum block을 16사이즈로 정함
	if (size < 16)
	{
		size = 16;
	}
	if ((int)(bp = mem_sbrk(size)) == -1)
	{
		return NULL;
	}

	PUT(HDRP(bp), PACK(size, 0));		 /* free block header */
	PUT(FTRP(bp), PACK(size, 0));		 /* free block footer */
	PUT(HDRP(NEXT_BLK(bp)), PACK(0, 1)); /* new epilogue header */
	return coalesce(bp);
}

static void *coalesce(void *bp) 
{
	size_t NEXT_ALLOC = GET_ALLOC(HDRP(NEXT_BLK(bp)));
	size_t PREV_ALLOC = GET_ALLOC(FTRP(PREV_BLK(bp)));  
	//PREV_BLK(bp) == bp: epilogue block을 만났을 떄. Extend했을 때 epilogue를 만나는 유일한 경우
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
		bp = PREV_BLK(bp);
		remove_from_free_list(bp);
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
	size_t asize;	   /* 조정 block size */
	size_t extendsize; /* fit 안 됐으면 늘려야 할 사이즈 */
	void *bp;

	if (size == 0)
		return (NULL);

	/* Adjust block size 하는거 */
	if (size <= DSIZE)
		asize = 2 * DSIZE;
	else
		asize = DSIZE * ((size + DSIZE + (DSIZE - 1)) / DSIZE);

	// explicit은 first fit으로
	if ((bp = find_fit(asize)) != NULL)
	{
		place(bp, asize);
		return (bp);
	}

	/* No fit found.  Get more memory and place the block. */
	extendsize = MAX(asize, CHUNKSIZE);
	if ((bp = extend_heap(extendsize / WSIZE)) == NULL)
		return (NULL);
	place(bp, asize);
	return (bp);
}

static void *find_fit(size_t asize)
{
	void *bp;
	static int last_malloced_size = 0;
	static int repeat_counter = 0;
	// 계속 똑같은 사이즈만 요청했을 때 요청 횟수가 60회가 넘어가면 아예 힙사이즈 확늘린다.(성능을 높이기 위한 방법) 
	if (last_malloced_size == (int)asize)
	{
		if (repeat_counter > 60)
		{
			int extendsize = MAX(asize, 4 * WSIZE);
			bp = extend_heap(extendsize / 4);
			return bp;
		}
		else
			repeat_counter++;
	}
	else
		repeat_counter = 0;
	// free block list에서 first fit으로 찾기
	for (bp = free_list_start; GET_ALLOC(HDRP(bp)) == 0; bp = GET_NEXT_PTR(bp))
	{
		if (asize <= (size_t)GET_SIZE(HDRP(bp)))
		{
			last_malloced_size = asize;
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
	//내 앞에 누구 있으면
	if (GET_PREV_PTR(bp))
		SET_NEXT_PTR(GET_PREV_PTR(bp), GET_NEXT_PTR(bp)); //내 앞 노드의 주소에다가, 내 뒤 노드의 주소를 넣어준다.
	else												  // 내 앞에 아무도 없으면 == 내가 젤 앞 노드이면
		free_list_start = GET_NEXT_PTR(bp);				  //나를 없애면서, 내 뒷 노드에다가 가장 앞자리의 왕관을 물려주고 간다!
	SET_PREV_PTR(GET_NEXT_PTR(bp), GET_PREV_PTR(bp));
}

void mm_free(void *bp)
{
	size_t size;
	if (bp == NULL)
		return;
	/* Free and coalesce the block. */
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
		size_t newsize = size + (2 * WSIZE); // 2 words for header and footer
		/*if newsize가 oldsize보다 작거나 같으면 그냥 그대로 써도 됨. just return bp */
		if (newsize <= oldsize)
		{
			return bp;
		}
		//oldsize 보다 new size가 크면 바꿔야 함.*/
		else
		{
			size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLK(bp)));
			size_t csize;
			/* next block is free and the size of the two blocks is greater than or equal the new size  */
			/* next block이 가용상태이고 old, next block의 사이즈 합이 new size보다 크면 그냥 그거 바로 합쳐서 쓰기  */
			if (!next_alloc && ((csize = oldsize + GET_SIZE(HDRP(NEXT_BLK(bp))))) >= newsize)
			{
				remove_from_free_list(NEXT_BLK(bp));
				PUT(HDRP(bp), PACK(csize, 1));
				PUT(FTRP(bp), PACK(csize, 1));
				return bp;
			}
			// 아니면 새로 block 만들어서 거기로 옮기기
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

static void checkblock(void *bp)
{

	if ((uintptr_t)bp % DSIZE)
		printf("Error: %p is not doubleword aligned\n", bp);
	if (GET(HDRP(bp)) != GET(FTRP(bp)))
		printf("Error: header does not match footer\n");
}

void checkheap(bool verbose)
{
	void *bp;

	if (verbose)
		printf("Heap (%p):\n", heap_listp);

	if (GET_SIZE(HDRP(heap_listp)) != DSIZE ||
		!GET_ALLOC(HDRP(heap_listp)))
		printf("Bad prologue header\n");
	checkblock(heap_listp);

	for (bp = heap_listp; GET_SIZE(HDRP(bp)) > 0; bp = (void *)NEXT_BLK(bp))
	{
		if (verbose)
			printblock(bp);
		checkblock(bp);
	}

	if (verbose)
		printblock(bp);
	if (GET_SIZE(HDRP(bp)) != 0 || !GET_ALLOC(HDRP(bp)))
		printf("Bad epilogue header\n");
}


static void printblock(void *bp)
{
	bool halloc, falloc;
	size_t hsize, fsize;

	checkheap(false);
	hsize = GET_SIZE(HDRP(bp));
	halloc = GET_ALLOC(HDRP(bp));
	fsize = GET_SIZE(FTRP(bp));
	falloc = GET_ALLOC(FTRP(bp));

	if (hsize == 0)
	{
		printf("%p: end of heap\n", bp);
		return;
	}

	printf("%p: header: [%zu:%c] footer: [%zu:%c]\n", bp,
		   hsize, (halloc ? 'a' : 'f'),
		   fsize, (falloc ? 'a' : 'f'));
}