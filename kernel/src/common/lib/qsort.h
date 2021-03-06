#ifndef __QSORT_H_INCLUDED__
#define __QSORT_H_INCLUDED__

#include <common/misc/utils.h>

typedef bool (*qsort_Comparator)(const void *left, const void *right, const void *ctx);

void qsort(void *array, size_t size, size_t count, qsort_Comparator cmp, const void *ctx);

#endif