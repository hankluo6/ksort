#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#define XORO_DEV "/dev/xoroshiro128p"

#define TEST_TIME 1
#define EXPERIMENT 100

int main()
{
    uint64_t buf[16] = {0};
    uint64_t times[EXPERIMENT][16] = {0};
    char *names[] = {"kernel_heap_sort",
                     "merge_sort",
                     "shell_sort",
                     "binary_insertion_sort",
                     "heap_sort",
                     "quick_sort",
                     "selection_sort",
                     "tim_sort",
                     "bubble_sort",
                     "bitonic_sort",
                     "merge_sort_in_place",
                     "grail_sort",
                     "sqrt_sort",
                     "rec_stable_sort",
                     "grail_sort_dyn_buffer",
                     "intro_sort"};

    int fd = open(XORO_DEV, O_RDWR);
    if (fd < 0) {
        perror("Failed to open character device");
        exit(1);
    }
    for (int e = 0; e < EXPERIMENT; ++e) {
        for (int t = 0; t < TEST_TIME; ++t) {
            read(fd, &buf, sizeof(buf));
            for (int i = 0; i < 16; i++) {
                times[e][i] += buf[i];
            }
        }
        for (int i = 0; i < 16; ++i)
            times[e][i] /= TEST_TIME;
    }
    for (int e = 0; e < EXPERIMENT; ++e) {
        for (int i = 0; i < 16; ++i) {
            printf("%lu ", times[e][i]);
        }
        printf("\n");
    }

    close(fd);
    return 0;
}