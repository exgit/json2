#pragma once


#include <stddef.h>


// opaque type
typedef struct _marena_t marena_t;


marena_t *marena_create(size_t size);
void marena_destroy(marena_t *marena);

void *marena_alloc(marena_t *marena, size_t size);
void marena_free(marena_t *marena);

void *marena_alloc_rt(marena_t *marena, size_t size);
void *marena_realloc_rt(marena_t *marena, void *ptr, size_t size);
void marena_free_rt(marena_t *marena, void *ptr);
