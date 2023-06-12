#include <fcntl.h>
#include <sys/random.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <errno.h>
#include <inttypes.h>
#include <time.h>
#include <linux/fs.h>
#include "FastRand.h"

enum suit
{
    DIRECT_DSYNC = 1,
    DSYNC = 2,
    FDATASYNC = 3,
};

static int do_test(const char* filename, size_t filesize, int mode)
{
    int fd = -1;
    int ret = -1;
    void* data = NULL;

    int flag;
    switch (mode) {
        case DIRECT_DSYNC:
            flag = O_DIRECT | O_DSYNC;
            break;
        case DSYNC:
            flag = O_DSYNC;
            break;
        case FDATASYNC:
            flag = 0;
            break;
        default:
            goto end;
    }

    if ((fd = open(filename, O_CLOEXEC | O_WRONLY | flag)) < 0)
        goto end;

    if (posix_memalign(&data, 4096, 4096))
        goto end;

    fastrand fr;
    uint16_t sptr[16];

    if (getrandom(&sptr, sizeof(sptr), 0) == -1)
        goto end;

    InitFastRand(sptr[0], sptr[1], sptr[2], sptr[3], sptr[4], sptr[5], sptr[6], sptr[7], sptr[8], sptr[9], sptr[10], sptr[11], sptr[12], sptr[13], sptr[14],
        sptr[15], &fr);

    struct timespec start, now;
    if (clock_gettime(CLOCK_MONOTONIC_COARSE, &start) < 0)
        goto end;
    int deadline = start.tv_sec + 10;

    uint64_t writes;
    for (writes = 0;; writes += 4) {
        if (clock_gettime(CLOCK_MONOTONIC_COARSE, &now) < 0)
            goto end;
        if (now.tv_sec > deadline)
            break;
        FastRand_SSE4(&fr);
        for (int i = 0; i < 4; i++) {
            // TODO: since we use less than 32 bit, we may call write more than 4 times per random generation
            memset(data, fr.res[i], 4096);
            if (pwrite(fd, data, 4096, fr.res[i] % (filesize / 4096)) != 4096)
                goto end;
            if (mode == FDATASYNC && fdatasync(fd) == -1)
                goto end;
        }
    }

    uint64_t diff_nsec = (uint64_t) now.tv_sec * 1000000000ull + (uint64_t) now.tv_nsec - (uint64_t) start.tv_sec * 1000000000ull - (uint64_t) start.tv_nsec;

    uint64_t iops = writes * 1000000000ull / diff_nsec;

    printf("%" PRIu64 " IOPS\n", iops);

    ret = 0;
end:
    free(data);
    if (fd != -1 && close(fd) < 0)
        abort();

    return ret;
}

static int preallocate_file(const char* filename)
{
    int fd = -1;
    if (unlink(filename) == -1 && errno != ENOENT)
        goto end;

    // Not using O_DIRECT to prefill. because fsync() may use parallel writes.
    if ((fd = open(filename, O_CLOEXEC | O_CREAT | O_WRONLY, 0600)) < 0)
        goto end;

    int attrs;

    if (ioctl(fd, FS_IOC_GETFLAGS, &attrs) == -1)
        goto end;

    attrs |= FS_NOCOW_FL | FS_NOATIME_FL;

    if (ioctl(fd, FS_IOC_SETFLAGS, &attrs) == -1)
        goto end;

    return fd;

end:
    if (fd != -1 && close(fd) < 0)
        abort();

    return -1;
}

#define MBYTES 1024

int main()
{
    int fd = -1;
    int ret = 1;
    const char* filename = "qwe.dat";

    if ((fd = preallocate_file(filename)) < 0)
        goto end;

    char buf[1024 * 1024];

    // error on short writes.
    for (int i = 0; i < MBYTES; i++) {
        if (write(fd, memset(buf, i + 1, sizeof(buf)), sizeof(buf)) != sizeof(buf))
            goto end;
    }

    if (fsync(fd) < 0)
        goto end;

    if (close(fd) < 0)
        abort();
    fd = -1;
    ////////////////////////

    if (do_test(filename, MBYTES * sizeof(buf), DIRECT_DSYNC) < 0)
        goto end;
    if (do_test(filename, MBYTES * sizeof(buf), DSYNC) < 0)
        goto end;
    if (do_test(filename, MBYTES * sizeof(buf), FDATASYNC) < 0)
        goto end;

    ret = 0;

end:
    if (fd != -1 && close(fd) < 0)
        abort();
    return ret;
}