#ifndef SORT_IMPL_H
#define SORT_IMPL_H

typedef void (*swap_func_t)(void *a, void *b, int size);

typedef int (*cmp_r_func_t)(const void *a, const void *b, const void *priv);
typedef int (*cmp_func_t)(const void *a, const void *b);

extern void sort_heap(void *base,
                      size_t num,
                      size_t size,
                      cmp_func_t cmp_func,
                      swap_func_t swap_func);

extern void sort_intro(void *_array,
                       size_t length,
                       size_t data_size,
                       cmp_func_t comparator,
                       swap_func_t swap_func);

extern void sort_pdqsort(void *base,
                         size_t num,
                         size_t size,
                         cmp_func_t cmp_func,
                         swap_func_t swap_func);

#endif
