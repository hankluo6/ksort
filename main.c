#include <linux/device.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#include "sort_impl.h"

#define DEVICE_NAME "xoroshiro128p"
#define CLASS_NAME "xoro"

#define SORT_NAME ksort
#define SORT_TYPE uint64_t
#include "sort.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("National Cheng Kung University, Taiwan");
MODULE_DESCRIPTION("sorting implementation");
MODULE_VERSION("0.1");

extern void seed(uint64_t, uint64_t);
extern void jump(void);
extern uint64_t next(void);

static int major_number;
static struct class *dev_class = NULL;
static struct device *dev_device = NULL;

/* Count the number of times device is opened */
static int n_opens = 0;

/* Mutex to allow only one userspace program to read at once */
static DEFINE_MUTEX(xoroshiro128p_mutex);

/**
 * Devices are represented as file structure in the kernel.
 */
static int dev_open(struct inode *, struct file *);
static int dev_release(struct inode *, struct file *);
static ssize_t dev_read(struct file *, char *, size_t, loff_t *);
static struct file_operations fops = {
    .open = dev_open,
    .read = dev_read,
    .release = dev_release,
};

#define TEST_LEN 10

static int cmpint(const void *a, const void *b)
{
    return *(int *) a - *(int *) b;
}

static int cmpint64(const void *a, const void *b)
{
    uint64_t a_val = *(uint64_t *) a;
    uint64_t b_val = *(uint64_t *) b;
    if (a_val > b_val)
        return 1;
    if (a_val == b_val)
        return 0;
    return -1;
}

static int cmpuint64(const void *a, const void *b)
{
    return *(uint64_t *) a < *(uint64_t *) b;
}

/** @brief Initialize /dev/xoroshiro128p.
 *  @return Returns 0 if successful.
 */
static int __init xoro_init(void)
{
    int *a, i, r = 1, err = -ENOMEM;
    major_number = register_chrdev(0, DEVICE_NAME, &fops);
    if (0 > major_number) {
        printk(KERN_ALERT "XORO: Failed to register major_number\n");
        return major_number;
    }

    dev_class = class_create(THIS_MODULE, CLASS_NAME);
    if (IS_ERR(dev_class)) {
        unregister_chrdev(major_number, DEVICE_NAME);
        printk(KERN_ALERT "XORO: Failed to create dev_class\n");
        return PTR_ERR(dev_class);
    }

    dev_device = device_create(dev_class, NULL, MKDEV(major_number, 0), NULL,
                               DEVICE_NAME);
    if (IS_ERR(dev_device)) {
        class_destroy(dev_class);
        unregister_chrdev(major_number, DEVICE_NAME);
        printk(KERN_ALERT "XORO: Failed to create dev_device\n");
        return PTR_ERR(dev_device);
    }

    mutex_init(&xoroshiro128p_mutex);

    seed(314159265, 1618033989);  // Initialize PRNG with pi and phi.

    a = kmalloc_array(TEST_LEN, sizeof(*a), GFP_KERNEL);
    if (!a)
        return err;

    for (i = 0; i < TEST_LEN; i++) {
        r = (r * 725861) % 6599;
        a[i] = r;
    }

    sort_heap(a, TEST_LEN, sizeof(*a), cmpint, NULL);

    err = -EINVAL;
    for (i = 0; i < TEST_LEN - 1; i++)
        if (a[i] > a[i + 1]) {
            pr_err("test has failed\n");
            goto exit;
        }
    err = 0;
    pr_info("test passed\n");
exit:
    kfree(a);
    return err;
}

/** @brief Free all module resources.
 *         Not used if part of a built-in driver rather than a LKM.
 */
static void __exit xoro_exit(void)
{
    mutex_destroy(&xoroshiro128p_mutex);

    device_destroy(dev_class, MKDEV(major_number, 0));

    class_unregister(dev_class);
    class_destroy(dev_class);

    unregister_chrdev(major_number, DEVICE_NAME);
}

/** @brief open() syscall.
 *         Increment counter, perform another jump to effectively give each
 *         reader a separate PRNG.
 *  @param inodep Pointer to an inode object (defined in linux/fs.h)
 *  @param filep Pointer to a file object (defined in linux/fs.h)
 */
static int dev_open(struct inode *inodep, struct file *filep)
{
    /* Try to acquire the mutex (returns 0 on fail) */
    if (!mutex_trylock(&xoroshiro128p_mutex)) {
        printk(KERN_INFO "XORO: %s busy\n", DEVICE_NAME);
        return -EBUSY;
    }

    jump(); /* in xoroshiro128plus.c */

    printk(KERN_INFO "XORO: %s opened. n_opens=%d\n", DEVICE_NAME, n_opens++);

    return 0;
}

/** @brief Called whenever device is read from user space.
 *  @param filep Pointer to a file object (defined in linux/fs.h).
 *  @param buffer Pointer to the buffer to which this function may write data.
 *  @param len Number of bytes requested.
 *  @param offset Unused.
 *  @return Returns number of bytes successfully read. Negative on error.
 */
static ssize_t dev_read(struct file *filep,
                        char *buffer,
                        size_t len,
                        loff_t *offset)
{
    preempt_disable();
    /* Give at most 8 bytes per read */
    ktime_t kt;
    uint64_t *arr, *arr_copy;
    uint64_t times[17];

    arr = kmalloc_array(TEST_LEN, sizeof(*arr), GFP_KERNEL);
    arr_copy = kmalloc_array(TEST_LEN, sizeof(*arr_copy), GFP_KERNEL);
    for (size_t i = 0; i < TEST_LEN; ++i) {
        uint64_t val = next();
        arr[i] = val;
    }

    /* kernel heap sort */
    memcpy(arr_copy, arr, sizeof(uint64_t) * TEST_LEN);
    kt = ktime_get();
    sort_heap(arr_copy, TEST_LEN, sizeof(*arr_copy), cmpint64, NULL);
    kt = ktime_sub(ktime_get(), kt);
    times[0] = ktime_to_ns(kt);
    for (int i = 0; i < TEST_LEN - 1; i++)
        if (arr_copy[i] > arr_copy[i + 1]) {
            pr_err("test has failed in kernel heap sort\n");
            break;
        }
    printk(KERN_INFO "%llu\n", ktime_to_ns(kt));

    memcpy(arr_copy, arr, sizeof(uint64_t) * TEST_LEN);
    kt = ktime_get();
    ksort_merge_sort(arr_copy, TEST_LEN);
    kt = ktime_sub(ktime_get(), kt);
    times[1] = ktime_to_ns(kt);
    for (int i = 0; i < TEST_LEN - 1; i++)
        if (arr_copy[i] > arr_copy[i + 1]) {
            pr_err("test has failed in merge sort\n");
            break;
        }
    printk(KERN_INFO "%llu\n", ktime_to_ns(kt));

    memcpy(arr_copy, arr, sizeof(uint64_t) * TEST_LEN);
    kt = ktime_get();
    ksort_shell_sort(arr_copy, TEST_LEN);
    kt = ktime_sub(ktime_get(), kt);
    times[2] = ktime_to_ns(kt);
    for (int i = 0; i < TEST_LEN - 1; i++)
        if (arr_copy[i] > arr_copy[i + 1]) {
            pr_err("test has failed in shell sort\n");
            break;
        }
    printk(KERN_INFO "%llu\n", ktime_to_ns(kt));

    memcpy(arr_copy, arr, sizeof(uint64_t) * TEST_LEN);
    kt = ktime_get();
    ksort_binary_insertion_sort(arr_copy, TEST_LEN);
    kt = ktime_sub(ktime_get(), kt);
    times[3] = ktime_to_ns(kt);
    for (int i = 0; i < TEST_LEN - 1; i++)
        if (arr_copy[i] > arr_copy[i + 1]) {
            pr_err("test has failed in binary insertion sort\n");
            break;
        }
    printk(KERN_INFO "%llu\n", ktime_to_ns(kt));

    memcpy(arr_copy, arr, sizeof(uint64_t) * TEST_LEN);
    kt = ktime_get();
    ksort_heap_sort(arr_copy, TEST_LEN);
    kt = ktime_sub(ktime_get(), kt);
    times[4] = ktime_to_ns(kt);
    for (int i = 0; i < TEST_LEN - 1; i++)
        if (arr_copy[i] > arr_copy[i + 1]) {
            pr_err("test has failed in heap sort\n");
            break;
        }
    printk(KERN_INFO "%llu\n", ktime_to_ns(kt));

    memcpy(arr_copy, arr, sizeof(uint64_t) * TEST_LEN);
    kt = ktime_get();
    ksort_quick_sort(arr_copy, TEST_LEN);
    kt = ktime_sub(ktime_get(), kt);
    times[5] = ktime_to_ns(kt);
    for (int i = 0; i < TEST_LEN - 1; i++)
        if (arr_copy[i] > arr_copy[i + 1]) {
            pr_err("test has failed in quick sort\n");
            break;
        }
    printk(KERN_INFO "%llu\n", ktime_to_ns(kt));

    memcpy(arr_copy, arr, sizeof(uint64_t) * TEST_LEN);
    kt = ktime_get();
    ksort_selection_sort(arr_copy, TEST_LEN);
    kt = ktime_sub(ktime_get(), kt);
    times[6] = ktime_to_ns(kt);
    for (int i = 0; i < TEST_LEN - 1; i++)
        if (arr_copy[i] > arr_copy[i + 1]) {
            pr_err("test has failed in selection sort\n");
            break;
        }
    printk(KERN_INFO "%llu\n", ktime_to_ns(kt));

    memcpy(arr_copy, arr, sizeof(uint64_t) * TEST_LEN);
    kt = ktime_get();
    ksort_tim_sort(arr_copy, TEST_LEN);
    kt = ktime_sub(ktime_get(), kt);
    times[7] = ktime_to_ns(kt);
    for (int i = 0; i < TEST_LEN - 1; i++)
        if (arr_copy[i] > arr_copy[i + 1]) {
            pr_err("test has failed in tim sort\n");
            break;
        }
    printk(KERN_INFO "%llu\n", ktime_to_ns(kt));

    memcpy(arr_copy, arr, sizeof(uint64_t) * TEST_LEN);
    kt = ktime_get();
    ksort_bubble_sort(arr_copy, TEST_LEN);
    kt = ktime_sub(ktime_get(), kt);
    times[8] = ktime_to_ns(kt);
    for (int i = 0; i < TEST_LEN - 1; i++)
        if (arr_copy[i] > arr_copy[i + 1]) {
            pr_err("test has failed in bubble sort\n");
            break;
        }
    printk(KERN_INFO "%llu\n", ktime_to_ns(kt));

    memcpy(arr_copy, arr, sizeof(uint64_t) * TEST_LEN);
    kt = ktime_get();
    ksort_bitonic_sort(arr_copy, TEST_LEN);
    kt = ktime_sub(ktime_get(), kt);
    times[9] = ktime_to_ns(kt);
    for (int i = 0; i < TEST_LEN - 1; i++)
        if (arr_copy[i] > arr_copy[i + 1]) {
            pr_err("test has failed in bitonic sort\n");
            break;
        }
    printk(KERN_INFO "%llu\n", ktime_to_ns(kt));

    memcpy(arr_copy, arr, sizeof(uint64_t) * TEST_LEN);
    kt = ktime_get();
    ksort_merge_sort_in_place(arr_copy, TEST_LEN);
    kt = ktime_sub(ktime_get(), kt);
    times[10] = ktime_to_ns(kt);
    for (int i = 0; i < TEST_LEN - 1; i++)
        if (arr_copy[i] > arr_copy[i + 1]) {
            pr_err("test has failed in merge sort in place\n");
            break;
        }
    printk(KERN_INFO "%llu\n", ktime_to_ns(kt));

    memcpy(arr_copy, arr, sizeof(uint64_t) * TEST_LEN);
    kt = ktime_get();
    ksort_grail_sort(arr_copy, TEST_LEN);
    kt = ktime_sub(ktime_get(), kt);
    times[11] = ktime_to_ns(kt);
    for (int i = 0; i < TEST_LEN - 1; i++)
        if (arr_copy[i] > arr_copy[i + 1]) {
            pr_err("test has failed in grail sort\n");
            break;
        }
    printk(KERN_INFO "%llu\n", ktime_to_ns(kt));

    memcpy(arr_copy, arr, sizeof(uint64_t) * TEST_LEN);
    kt = ktime_get();
    ksort_sqrt_sort(arr_copy, TEST_LEN);
    kt = ktime_sub(ktime_get(), kt);
    times[12] = ktime_to_ns(kt);
    for (int i = 0; i < TEST_LEN - 1; i++)
        if (arr_copy[i] > arr_copy[i + 1]) {
            pr_err("test has failed in sqrt sort\n");
            break;
        }
    printk(KERN_INFO "%llu\n", ktime_to_ns(kt));

    memcpy(arr_copy, arr, sizeof(uint64_t) * TEST_LEN);
    kt = ktime_get();
    ksort_rec_stable_sort(arr_copy, TEST_LEN);
    kt = ktime_sub(ktime_get(), kt);
    times[13] = ktime_to_ns(kt);
    for (int i = 0; i < TEST_LEN - 1; i++)
        if (arr_copy[i] > arr_copy[i + 1]) {
            pr_err("test has failed in rec stable sort\n");
            break;
        }
    printk(KERN_INFO "%llu\n", ktime_to_ns(kt));

    memcpy(arr_copy, arr, sizeof(uint64_t) * TEST_LEN);
    kt = ktime_get();
    ksort_grail_sort_dyn_buffer(arr_copy, TEST_LEN);
    kt = ktime_sub(ktime_get(), kt);
    times[14] = ktime_to_ns(kt);
    for (int i = 0; i < TEST_LEN - 1; i++)
        if (arr_copy[i] > arr_copy[i + 1]) {
            pr_err("test has failed in grail sort dyn buffer\n");
            break;
        }
    printk(KERN_INFO "%llu\n", ktime_to_ns(kt));

    memcpy(arr_copy, arr, sizeof(uint64_t) * TEST_LEN);
    kt = ktime_get();
    sort_intro(arr_copy, TEST_LEN, sizeof(*arr_copy), cmpint64, 0);
    kt = ktime_sub(ktime_get(), kt);
    times[15] = ktime_to_ns(kt);
    for (int i = 0; i < TEST_LEN - 1; i++)
        if (arr_copy[i] > arr_copy[i + 1]) {
            pr_err("test has failed in intro sort\n");
            break;
        }
    printk(KERN_INFO "%llu\n", ktime_to_ns(kt));

    memcpy(arr_copy, arr, sizeof(uint64_t) * TEST_LEN);
    kt = ktime_get();
    sort_pdqsort(arr_copy, TEST_LEN, sizeof(*arr_copy), cmpuint64, 0);
    kt = ktime_sub(ktime_get(), kt);
    times[16] = ktime_to_ns(kt);
    for (int i = 0; i < TEST_LEN - 1; i++)
        if (arr_copy[i] > arr_copy[i + 1]) {
            pr_err("test has failed in pdqsort\n");
            break;
        }
    printk(KERN_INFO "%llu\n", ktime_to_ns(kt));

    /* copy_to_user has the format ( * to, *from, size) and ret 0 on success */
    int n_notcopied = copy_to_user(buffer, times, len);
    kfree(arr);
    kfree(arr_copy);
    if (0 != n_notcopied) {
        printk(KERN_ALERT "XORO: Failed to read %d/%ld bytes\n", n_notcopied,
               len);
        return -EFAULT;
    }
    printk(KERN_INFO "XORO: read %ld bytes\n", len);
    preempt_enable();
    return len;
}

/** @brief Called when the userspace program calls close().
 *  @param inodep A pointer to an inode object (defined in linux/fs.h)
 *  @param filep A pointer to a file object (defined in linux/fs.h)
 */
static int dev_release(struct inode *inodep, struct file *filep)
{
    mutex_unlock(&xoroshiro128p_mutex);
    return 0;
}

module_init(xoro_init);
module_exit(xoro_exit);
