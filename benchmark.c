#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#define XORO_DEV "/dev/xoroshiro128p"

#define TEST_TIME 1000000
#define EXPERIMENT 100

int main()
{
    uint64_t buf[15] = {0};
    uint64_t times[EXPERIMENT][15] = {0};
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
                     "grail_sort_dyn_buffer"};

    int fd = open(XORO_DEV, O_RDWR);
    if (fd < 0) {
        perror("Failed to open character device");
        exit(1);
    }
    for (int e = 0; e < EXPERIMENT; ++e) {
        read(fd, &buf, sizeof(buf));
        for (int t = 0; t < TEST_TIME; ++t) {
            for (int i = 0; i < 15; i++) {
                times[e][i] += buf[i];
            }
        }
        for (int i = 0; i < 15; ++i)
            times[e][i] /= TEST_TIME;
    }
    for (int e = 0; e < EXPERIMENT; ++e) {
        for (int i = 0; i < 15; ++i) {
            printf("%lu ", times[e][i]);
        }
        printf("\n");
    }

    close(fd);
    return 0;
}