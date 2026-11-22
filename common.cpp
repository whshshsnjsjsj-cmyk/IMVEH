#include "common.h"
#include <time.h>
#include <unistd.h>
#include <fcntl.h>

int64_t NowMs() { timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (int64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000; }

bool SafeRead(const void* src, void* dst, size_t n) {
    static thread_local int p[2] = {-1, -1};
    if (n == 0 || n > 512) return false;
    if (p[0] < 0 && pipe2(p, O_NONBLOCK) != 0) { p[0] = p[1] = -1; return false; }
    ssize_t w = write(p[1], src, n);
    if (w != (ssize_t)n) { if (w > 0) { char tmp[512]; (void)!read(p[0], tmp, (size_t)w); } return false; }
    return read(p[0], dst, n) == (ssize_t)n;
}
