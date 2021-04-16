/**
 * introsort is comprised of:
 * - Quicksort with explicit stack instead of recursion, and ignoring small
 *   partitions
 * - Binary heapsort with Floyd's optimization, for stack depth > 2log2(n)
 * - Final shellsort pass on skipped small partitions (small gaps only)
 */
#include <linux/limits.h>
#include <linux/slab.h>
#include <linux/types.h>

#include "sort_impl.h"

typedef int (*cmp_func_t)(const void *, const void *);

typedef struct {
    char *low, *high;
} stack_node_t;

#define idx(x) (x) * size               /* manual indexing */
#define STACK_SIZE (sizeof(size_t) * 8) /* size constant */

static inline int __log2(size_t x)
{
    return 63 - __builtin_clzll(x);
}

/**
 * swap_words_32 - swap two elements in 32-bit chunks
 * @a: pointer to the first element to swap
 * @b: pointer to the second element to swap
 * @n: element size (must be a multiple of 4)
 *
 * Exchange the two objects in memory.  This exploits base+index addressing,
 * which basically all CPUs have, to minimize loop overhead computations.
 *
 * For some reason, on x86 gcc 7.3.0 adds a redundant test of n at the
 * bottom of the loop, even though the zero flag is stil valid from the
 * subtract (since the intervening mov instructions don't alter the flags).
 * Gcc 8.1.0 doesn't have that problem.
 */
static void swap_words_32(void *_a, void *_b, size_t n)
{
    char *a = _a, *b = _b;
    do {
        u32 t = *(u32 *) (a + (n -= 4));
        *(u32 *) (a + n) = *(u32 *) (b + n);
        *(u32 *) (b + n) = t;
    } while (n);
}

/**
 * swap_words_64 - swap two elements in 64-bit chunks
 * @a: pointer to the first element to swap
 * @b: pointer to the second element to swap
 * @n: element size (must be a multiple of 8)
 *
 * Exchange the two objects in memory.  This exploits base+index
 * addressing, which basically all CPUs have, to minimize loop overhead
 * computations.
 *
 * We'd like to use 64-bit loads if possible.  If they're not, emulating
 * one requires base+index+4 addressing which x86 has but most other
 * processors do not.  If CONFIG_64BIT, we definitely have 64-bit loads,
 * but it's possible to have 64-bit loads without 64-bit pointers (e.g.
 * x32 ABI).  Are there any cases the kernel needs to worry about?
 */
static void swap_words_64(void *_a, void *_b, size_t n)
{
    char *a = _a, *b = _b;
    do {
#ifdef CONFIG_64BIT
        u64 t = *(u64 *) (a + (n -= 8));
        *(u64 *) (a + n) = *(u64 *) (b + n);
        *(u64 *) (b + n) = t;
#else
        /* Use two 32-bit transfers to avoid base+index+4 addressing */
        u32 t = *(u32 *) (a + (n -= 4));
        *(u32 *) (a + n) = *(u32 *) (b + n);
        *(u32 *) (b + n) = t;

        t = *(u32 *) (a + (n -= 4));
        *(u32 *) (a + n) = *(u32 *) (b + n);
        *(u32 *) (b + n) = t;
#endif
    } while (n);
}

/**
 * swap_bytes - swap two elements a byte at a time
 * @a: pointer to the first element to swap
 * @b: pointer to the second element to swap
 * @n: element size
 *
 * This is the fallback if alignment doesn't allow using larger chunks.
 */
static void swap_bytes(void *a, void *b, size_t n)
{
    do {
        char t = ((char *) a)[--n];
        ((char *) a)[n] = ((char *) b)[n];
        ((char *) b)[n] = t;
    } while (n);
}

/*
 * The values are arbitrary as long as they can't be confused with
 * a pointer, but small integers make for the smallest compare
 * instructions.
 */
#define SWAP_WORDS_64 (swap_func_t) 0
#define SWAP_WORDS_32 (swap_func_t) 1
#define SWAP_BYTES (swap_func_t) 2

/*
 * The function pointer is last to make tail calls most efficient if the
 * compiler decides not to inline this function.
 */
static void do_swap(void *a, void *b, size_t size, swap_func_t swap_func)
{
    if (swap_func == SWAP_WORDS_64)
        swap_words_64(a, b, size);
    else if (swap_func == SWAP_WORDS_32)
        swap_words_32(a, b, size);
    else if (swap_func == SWAP_BYTES)
        swap_bytes(a, b, size);
    else
        swap_func(a, b, (int) size);
}

void sort_intro(void *base,
                size_t num,
                size_t size,
                cmp_func_t cmp_func,
                swap_func_t swap_func)
{
    if (num == 0)
        return;

    char *array = (char *) base;
    const size_t max_thresh = size << 4;
    const int max_depth = __log2(num) << 1;

    /* Temporary storage used by both heapsort and shellsort */
    char *tmp = kmalloc_array(num, sizeof(*tmp), GFP_KERNEL);

    if (num > 16) {
        char *low = array, *high = array + idx(num - 1);
        stack_node_t *stack = kmalloc_array(num, sizeof(*stack), GFP_KERNEL);
        stack_node_t *top = stack + 1;

        int depth = 0;
        while (stack < top) {
            /* Exceeded max depth: do heapsort on this partition */
            if (depth > max_depth) {
                size_t part_length = (size_t)((high - low) / size) - 1;
                if (part_length > 0) {
                    size_t i, j, k = part_length >> 1;

                    /* heapification */
                    do {
                        i = k;
                        j = (i << 1) + 2;
                        memcpy(tmp, low + idx(i), size);

                        while (j <= part_length) {
                            if (j < part_length)
                                j += (cmp_func(low + idx(j), low + idx(j + 1)) <
                                      0);
                            if (cmp_func(low + idx(j), tmp) <= 0)
                                break;
                            memcpy(low + idx(i), low + idx(j), size);
                            i = j;
                            j = (i << 1) + 2;
                        }

                        memcpy(low + idx(i), tmp, size);
                    } while (k-- > 0);

                    /* heapsort */
                    do {
                        i = part_length;
                        j = 0;
                        memcpy(tmp, low + idx(part_length), size);

                        /* Floyd's optimization:
                         * Not checking low[j] <= tmp saves nlog2(n) comparisons
                         */
                        while (j < part_length) {
                            if (j < part_length - 1)
                                j += (cmp_func(low + idx(j), low + idx(j + 1)) <
                                      0);
                            memcpy(low + idx(i), low + idx(j), size);
                            i = j;
                            j = (i << 1) + 2;
                        }

                        /* Compensate for Floyd's optimization by sifting down
                         * tmp. This adds O(n) comparisons and moves.
                         */
                        while (i > 1) {
                            j = (i - 2) >> 1;
                            if (cmp_func(tmp, low + idx(j)) <= 0)
                                break;
                            memcpy(low + idx(i), low + idx(j), size);
                            i = j;
                        }

                        memcpy(low + idx(i), tmp, size);
                    } while (part_length-- > 0);
                }

                /* pop next partition from stack */
                --top;
                --depth;
                low = top->low;
                high = top->high;
                continue;
            }

            /* 3-way "Dutch national flag" partition */
            char *mid = low + size * ((high - low) / size >> 1);
            if (cmp_func(mid, low) < 0)
                do_swap(mid, low, size, 0);
            if (cmp_func(mid, high) > 0)
                do_swap(mid, high, size, 0);
            else
                goto skip;
            if (cmp_func(mid, low) < 0)
                do_swap(mid, low, size, 0);

        skip:;
            char *left = low + size, *right = high - size;

            /* sort this partition */
            do {
                while (cmp_func(left, mid) < 0)
                    left += size;
                while (cmp_func(mid, right) < 0)
                    right -= size;

                if (left < right) {
                    do_swap(left, right, size, 0);
                    if (mid == left)
                        mid = right;
                    else if (mid == right)
                        mid = left;
                    left += size, right -= size;
                } else if (left == right) {
                    left += size, right -= size;
                    break;
                }
            } while (left <= right);

            /* Prepare the next iteration
             * Push larger partition and sort the other; unless one or both
             * smaller than threshold, then leave it to the final shellsort.
             * Both below threshold
             */
            if ((size_t)(right - low) <= max_thresh &&
                (size_t)(high - left) <= max_thresh) {
                --top;
                --depth;
                low = top->low;
                high = top->high;
            }
            /* Left below threshold */
            else if ((size_t)(right - low) <= max_thresh &&
                     (size_t)(high - left) > max_thresh) {
                low = left;
            }
            /* Right below threshold */
            else if ((size_t)(right - low) > max_thresh &&
                     (size_t)(high - left) <= max_thresh) {
                high = right;
            } else {
                /* Push big left, sort smaller right */
                if ((right - low) > (high - left)) {
                    top->low = low, top->high = right;
                    low = left;
                    ++top;
                    ++depth;
                } else { /* Push big right, sort smaller left */
                    top->low = left, top->high = high;
                    high = right;
                    ++top;
                    ++depth;
                }
            }
        }
        kfree(stack);
    }

    /* Clean up the leftovers with shellsort.
     * Already mostly sorted; use only small gaps.
     */
    const size_t gaps[2] = {1ul, 4ul};

    int i = 0;
    do {
        if (num < 4)
            continue;

        for (size_t j = gaps[i], k = j; j < num; k = ++j) {
            // memcpy(tmp, array + idx(k), size);

            while (k >= gaps[i] &&
                   cmp_func(array + idx(k - gaps[i]), array + idx(k)) > 0) {
                // memcpy(array + idx(k), array + idx(k - gaps[i]), size);
                do_swap(array + idx(k), array + idx(k - gaps[i]), size, 0);
                k -= gaps[i];
            }

            // memcpy(array + idx(k), tmp, size);
        }
    } while (i-- > 0);
    kfree(tmp);
}