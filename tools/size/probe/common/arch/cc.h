/* SPDX-License-Identifier: MIT */
/* Size probe, NOT FIRMWARE: LwIP compiler port for arm-none-eabi-gcc. */
#ifndef PROBE_ARCH_CC_H
#define PROBE_ARCH_CC_H
#include <stdint.h>
#include <stdlib.h>
#define LWIP_PLATFORM_DIAG(x) do { } while (0)
#define LWIP_PLATFORM_ASSERT(x) do { for (;;) { } } while (0)
#define BYTE_ORDER LITTLE_ENDIAN
#define LWIP_RAND() ((u32_t)rand())
#define LWIP_TIMEVAL_PRIVATE 0
#include <sys/time.h>
#endif
