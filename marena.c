/* marena.h - Arena allocator.
 *
 * Arena allocator is a special type of memory allocator wich allows allocation
 * of multiply objects and freing them all at once.
 *
 * This implementation uses hybrid approach where are two types of memory
 * allocation exist. First is normal arena allocation where blocks of memory
 * allocated once and then freed altogether. Second is returnable allocation
 * where blocks of memory can be returned, resized or freed before freeing all
 * blocks altohether.
 *
 * In this implementation memory arena has fixed size which is set on creation.
 * This can be considered as benefit or as drawback. In case when user wish to
 * control memory consumption for early error detection or denial of service
 * attack mitigation it is a benefit.
 *
 *
 * (c) 2019 Oleg Alexeev <oleg.alexeev@inbox.ru> (https://github.com/exgit)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */


#include "marena.h"
#include <stdlib.h>
#include <string.h>


// magic number for free block header
#define MAGIC	0xFFFF7575


// allocation granularity
#define GRANULARITY (sizeof(void*))


// rounding up to GRANULARITY bytes
#define ROUNDUP(x) x += (-x & (GRANULARITY-1))


// returnable memory block header (size aligned go GRANULARITY)
typedef struct _marena_rt_hdr_t marena_rt_hdr_t;
struct _marena_rt_hdr_t {
	marena_rt_hdr_t *next;	// ptr to next free block
	size_t size;			// size of this block (including header size)
	size_t magic;			// magic number for correctness test
};


// memory arena object (size aligned go GRANULARITY)
struct _marena_t {
	char *start;			// start of memory arena
	size_t sizet;			// total size of memory arena
	size_t sizec;			// current size of allocated memory
	marena_rt_hdr_t *free;	// ptr to first free returnable block
};


/* Create memory arena object.
 *
 */
marena_t *marena_create(size_t size) {
	ROUNDUP(size);
	marena_t *ret = malloc(sizeof(marena_t)+size);
	if (ret) {
		memset(ret, 0, sizeof(marena_t)+size);
		ret->start = (char*)(ret+1);
		ret->sizet = size;
	}
	return ret;
}


/* Destroy memory arena object.
 *
 */
void marena_destroy(marena_t *marena) {
	free(marena);
}


void *marena_alloc(marena_t *marena, size_t size) {
	char *ret = NULL;

	if (marena == NULL)
		goto exit;

	ROUNDUP(size);
	if (marena->sizec + size > marena->sizet)
		goto exit;

	ret = marena->start + marena->sizec;
	marena->sizec += size;

exit:
	return ret;
}


/* Free all memory arena.
 *
 */
void marena_free(marena_t *marena) {
	if (marena == NULL)
		return;

	marena->sizec = 0;
	marena->free = NULL;
	memset(marena->start, 0, marena->sizet);
}


/* Allocate returnable block from memory arena.
 *
 */
void *marena_alloc_rt(marena_t *marena, size_t size) {
	char *ret = NULL;

	// add header size
	ROUNDUP(size);
	size += sizeof(marena_rt_hdr_t);

	// first try to allocate returnable block from prevously freed
	marena_rt_hdr_t **prev = &marena->free;
	marena_rt_hdr_t *cur;
	for (cur = *prev; cur; prev=&cur->next, cur=cur->next)
		if (cur->size >= size) {
			if (cur->size >= 2*size) {  // split block in two
				cur->size -= size;
				marena_rt_hdr_t *spl = (void*)((char*)cur + cur->size);
				spl->next = 0;
				spl->size = size;
				spl->magic = MAGIC;
				ret = (char*)(spl+1);
			} else {  // allocate all block
				*prev = cur->next;
				ret = (char*)(cur+1);
			}
			goto exit;
		}

	// check available arena memory
	if (marena->sizec + size > marena->sizet)
		goto exit;

	// allocate from arena
	cur = (void*)(marena->start + marena->sizec);
	cur->next = 0;
	cur->size = size;
	cur->magic = MAGIC;
	ret = (char*)(cur+1);
	marena->sizec += size;

exit:
	return ret;
}


/* Resize returnable block.
 *
 */
void *marena_realloc_rt(marena_t *marena, void *ptr, size_t size) {
	char *ret = NULL;

	if (marena == NULL)
		goto exit;

	// add header size
	ROUNDUP(size);
	size += sizeof(marena_rt_hdr_t);

	// check if ptr really points to returnable block
	marena_rt_hdr_t *cur = (marena_rt_hdr_t*)ptr - 1;
	if (cur->magic != MAGIC)
		goto exit;

	// check if block already has requred size
	if (cur->size >= size) {
		ret = ptr;
		goto exit;
	}

	// allocate new returnable block
	ret = marena_alloc_rt(marena, size);
	if (ret == NULL)
		goto exit;

	// copy data to new block
	memcpy(ret, ptr, cur->size);

	// free old block
	cur->next = marena->free;
	marena->free = cur;

exit:
	return ret;
}


/* Free returnable block.
 *
 */
void marena_free_rt(marena_t *marena, void *ptr) {
	if (marena == NULL)
		goto exit;

	// check if ptr really points to returnable block
	marena_rt_hdr_t *cur = (marena_rt_hdr_t*)ptr - 1;
	if (cur->magic != MAGIC)
		goto exit;

	// free block
	cur->next = marena->free;
	marena->free = cur;

exit:
	return;
}
