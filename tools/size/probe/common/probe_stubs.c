/* SPDX-License-Identifier: MIT */
/*
 * Size probe support. NOT FIRMWARE: these images are linked only to measure
 * flash and RAM against the board linker script. They are never flashed.
 * No HAL, no peripheral access. Every function that would touch hardware is
 * an empty stub so that the linker keeps the code that a real build keeps.
 */
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>

#if defined(PROBE_UPSTREAM_TIME)
uint32_t SystemCoreClock = 520000000u; /* HAL defines it in a real build */
#endif
void SystemInit(void) {}
void ExitRun0Mode(void) {}

/* Heap arena for newlib malloc, sized by the probe (PROBE_HEAP_BYTES). The
 * arena is a .bss array so it shows in the DTCM figure. */
#ifndef PROBE_HEAP_BYTES
#define PROBE_HEAP_BYTES 0
#endif
#if PROBE_HEAP_BYTES > 0
static uint8_t s_arena[PROBE_HEAP_BYTES] __attribute__((aligned(8)));
static size_t s_brk;
void *_sbrk(ptrdiff_t incr)
{
    if (incr < 0 || (size_t)incr > sizeof s_arena - s_brk) {
        errno = ENOMEM;
        return (void *)-1;
    }
    void *p = &s_arena[s_brk];
    s_brk += (size_t)incr;
    return p;
}
#endif

/* Monotonic time for micro-ROS. A real port reads a hardware timer. The
 * FreeRTOS probe uses the upstream microros_time.c instead. */
#if !defined(PROBE_UPSTREAM_TIME)
volatile uint64_t g_probe_us;
int clock_gettime(clockid_t id, struct timespec *tp)
{
    (void)id;
    uint64_t us = g_probe_us;
    tp->tv_sec = (time_t)(us / 1000000u);
    tp->tv_nsec = (long)((us % 1000000u) * 1000u);
    return 0;
}
#endif

int _close(int fd) { (void)fd; return -1; }
int _fstat(int fd, struct stat *st) { (void)fd; st->st_mode = S_IFCHR; return 0; }
int _isatty(int fd) { (void)fd; return 1; }
int _lseek(int fd, int p, int d) { (void)fd; (void)p; (void)d; return 0; }
int _read(int fd, char *b, int n) { (void)fd; (void)b; (void)n; return 0; }
int _write(int fd, const char *b, int n) { (void)fd; (void)b; return n; }
int _getpid(void) { return 1; }
int _kill(int pid, int sig) { (void)pid; (void)sig; errno = EINVAL; return -1; }
void _exit(int status) { (void)status; for (;;) { } }
int _gettimeofday(void *tv, void *tz) { (void)tv; (void)tz; return 0; }
