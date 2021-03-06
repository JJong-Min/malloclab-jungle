#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>
#include "mm.h"
#include "memlib.h"

// sigle word 사이즈 지정(4bytes)
#define WSIZE 4
// double word 사이즈 지정(8bytes)
#define DSIZE 8
// 초기가용블록과 힙 확장을 위한 chuncksize
#define CHUNKSIZE (1 << 12)
#define MAX(x, y) ((x) > (y) ? (x) : (y))
// 블록의 size와 할당여부를 알려주는 alloc bit를 합쳐 header와 footer에 담을 수 있도록 반환
#define PACK(size, alloc) ((size) | (alloc))
// pointer p를 역참조하여 값을 가져옴
// p는 대부분 void( *)일 것이고 void형 pointer는 직접적으로 역참조가 안되므로 형변환을 함.
#define GET(p) (*(unsigned int *)(p))
// pointer p를 역참조하여 val로 값을 바꿈
#define PUT(p, val) (*(unsigned int *)(p) = (val))
// pointer p에 접근하여 블록의 size를 반환
#define GET_SIZE(p) (GET(p) & ~0x7)
// pointer p에 접근하여 블록의 할당bit를 반환
#define GET_ALLOC(p) (GET(p) & 0x1)
// block pointer p를 주면 해당 block의 header를 가리키는 pointer 반환
#define HDRP(bp) ((char *)(bp) - WSIZE)
// block pointer p를 주면 해당 block의 footer를 가리키는 pointer 반환
#define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)
// block pointer p의 다음 블록의 위치를 가리키는 pointer 반환
#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE)))
// block pointer p의 이전 블록의 위치를 가리키는 pointer 반환
#define PREV_BLKP(bp) ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE)))
#define FIRST_FITX
#define NEXT_FIT

static void *extend_heap(size_t words);
static void *coalesce(void *bp);
static void place(void *bp, size_t asize);
// static void *find_fit(size_t asize);
static void *next_fit(size_t asize);
static void *heap_listp = NULL;
static void *last_bp;         // next_fit을 위한 변수 선언

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

// heap에서 edge condition을 없애주기 위해 초기화 작업 진행
int mm_init(void)
{   
    // 4word가 필요하므로 heap 전체 영역이 4워드 미만이면 안됨
    if ((heap_listp = mem_sbrk(4*WSIZE)) == (void *)-1)
        return -1;
    PUT(heap_listp, 0); // alignment padding
    PUT(heap_listp + (1*WSIZE), PACK(DSIZE, 1)); // prologue header
    PUT(heap_listp + (2*WSIZE), PACK(DSIZE, 1)); // prologue footer
    PUT(heap_listp + (3*WSIZE), PACK(0, 1)); // epliogue header
    heap_listp += (2*WSIZE);
    if (extend_heap(CHUNKSIZE/WSIZE) == NULL) {
        return -1;
    }
    last_bp = heap_listp;
    return 0;
}

static void *extend_heap(size_t words)
{
    char *bp;
    size_t size;
    // 가용 리스트의 크기를 8의 배수로 맞추기 위한 작업
    size = (words % 2) ? (words + 1) * WSIZE : words * WSIZE;
    // mem_sbrk함수를 이용하여 늘렸을 때, 늘어날 수 없다면 return NULL
    if ((long)(bp = mem_sbrk(size)) == -1)
        return NULL;
    // header와 footer 업데이트
    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1));
    return coalesce(bp);
}

static void place(void *bp, size_t asize) {
    size_t csize = GET_SIZE(HDRP(bp));
    if((csize - asize) >= (2 * DSIZE)) {
        PUT(HDRP(bp), PACK(asize, 1));
        PUT(FTRP(bp), PACK(asize, 1));
        bp = NEXT_BLKP(bp);
        PUT(HDRP(bp), PACK(csize - asize, 0));
        PUT(FTRP(bp), PACK(csize - asize, 0));
    }
    else {
        PUT(HDRP(bp), PACK(csize, 1));
        PUT(FTRP(bp), PACK(csize, 1));
    }
}

void *mm_malloc(size_t size)
{
    size_t asize;
    size_t extendsize;
    char *bp;
    if (size == 0)
        return NULL;
    if (size <= DSIZE)
        asize = 2*DSIZE;
    else
        asize = DSIZE * ((size + (DSIZE) + (DSIZE - 1)) / DSIZE);
    if ((bp = next_fit(asize)) != NULL) {
        place(bp, asize);
        last_bp = bp;
        return bp;
    }
    extendsize = MAX(asize, CHUNKSIZE);
    if ((bp = extend_heap(extendsize/WSIZE)) == NULL)
        return NULL;
    place(bp, asize);
    return bp;
}

// 해당 블록을 free
void mm_free(void *bp) {
    size_t size = GET_SIZE(HDRP(bp));
    PUT(HDRP(bp), PACK(size, 0)); 
    PUT(FTRP(bp), PACK(size, 0)); 
    coalesce(bp); 
}

// 양쪽의 블록을 확인하여 free된 것은 연결
static void *coalesce(void *bp)
{
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    size_t size = GET_SIZE(HDRP(bp));
    // 앞 뒤 블록 둘 다 allocated일 경우
    if (prev_alloc && next_alloc) {
        last_bp = bp;
        return bp;
    }
    // 뒤 블록만 free일 경우
    else if (prev_alloc && !next_alloc) {
        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));
    }
    //앞 블록만 free일 경우
    else if (!prev_alloc && next_alloc) {
        size += GET_SIZE(HDRP(PREV_BLKP(bp)));
        PUT(FTRP(bp), PACK(size, 0));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
    }
    // 앞 뒤 블록 둘 다 free일 경우
    else {
        size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(FTRP(NEXT_BLKP(bp)));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
    }
    last_bp = bp;
    return bp;
}

// first-fit
#if defined(FIRST_FIT)
static void *find_fit(size_t asize)
{
    void *bp;
    for (bp = heap_listp; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp)) {
        if (!GET_ALLOC(HDRP(bp)) && (asize <= GET_SIZE(HDRP(bp)))) {
            return bp;
        }
    }
    return NULL;
}

#else
// next-fit
static void* next_fit(size_t asize)
{
    char* bp = last_bp;

    for (bp = NEXT_BLKP(bp); GET_SIZE(HDRP(bp))!=0; bp = NEXT_BLKP(bp))
    {
        if (GET_ALLOC(HDRP(bp)) == 0 && GET_SIZE(HDRP(bp)) >= asize)
        {
            last_bp = bp;
            return bp;
        }        
    }
    // 끝까지 갔는데 할당가능한 free block이 없으면 다시 처음부터 last_bp전까지 탐색
    bp = heap_listp;
    while (bp < last_bp)
    {
        bp = NEXT_BLKP(bp);

        if (GET_ALLOC(HDRP(bp)) == 0 && GET_SIZE(HDRP(bp)) >= asize)
        {
            last_bp = bp;
            return bp;
        }
    }
    return NULL;
}

#endif

void *mm_realloc(void *ptr, size_t size)
{
    void *oldptr = ptr;
    void *newptr;
    size_t copySize;
    newptr = mm_malloc(size);
    if (newptr == NULL)
      return NULL;
    copySize = GET_SIZE(HDRP(oldptr));
    if (size < copySize)
      copySize = size;
    memcpy(newptr, oldptr, copySize);
    mm_free(oldptr);
    return newptr;
}