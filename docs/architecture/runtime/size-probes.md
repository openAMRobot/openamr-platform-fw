# Size probes: flash and RAM of the runtime options on the STM32H723

Supports `docs/architecture/runtime-decision.md`. Status: measured on link
maps only. Nothing here ran on a board.

## What was measured

`tools/size/probe/build_probes.sh` links four images against the board linker
script (`boards/stm32/OpenAMRobot/STM32H723xG_flash.ld`) with the Release CPU
flags (`-mcpu=cortex-m7 -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -Os`,
`--gc-sections`, newlib-nano). Each image links real third-party code with stub
I/O. They are measurement images, not firmware. `tools/size/map_report.py`
reads every number below from the link maps. The script and its tests are in
`tools/size/`.

| Input | Version used |
|---|---|
| Toolchain | arm-none-eabi-gcc 13.2.1 20231009 (Ubuntu package 15:13.2.rel1-2) |
| micro-ROS | micro_ros_stm32cubemx_utils jazzy at a5b2127 (2025-09-15), static library built in its Docker builder image with the default `colcon.meta` (1 node, 10 publishers, 5 subscriptions, 1 service, 1 client, history 4, custom transport, MTU 512) |
| LwIP | lwip-tcpip/lwip master at 053a259 (2026-11-08) |
| FreeRTOS | FreeRTOS-Kernel main at ca75944 (2026-10-29), port `GCC/ARM_CM7/r0p1`, heap_4 |

The micro-ROS part of every probe is the same application: 1 node, 10
best-effort publishers (status, aux status, IMU, IMU temperature, 6 ranges for
4 MB7060 and 2 TF-Luna), 2 subscriptions (stand-ins for the two inbound I8
topics), 1 timer, 1 executor, ping, time sync and teardown. The message types
are standard-library stand-ins because the I8 messages (interfaces #38) are
not in the library yet.

| Probe | Contents |
|---|---|
| `empty_baseline` | startup file, libc stubs, empty `main` |
| `superloop_uros_serial` | super-loop + micro-ROS over a byte stream (framed custom transport), 32 KiB newlib heap arena |
| `superloop_lwip_uros_udp` | super-loop + LwIP raw API (`NO_SYS 1`, UDP, ARP, ICMP, IPv4, no TCP, no DHCP) + micro-ROS over UDP, 32 KiB heap arena |
| `freertos_lwip_uros_udp` | FreeRTOS + LwIP sockets (`NO_SYS 0`, upstream micro-ROS UDP options: `MEMP_NUM_UDP_PCB 15`, `LWIP_SO_RCVTIMEO 1`) + micro-ROS over UDP with the upstream allocator, time and memory-manager files; control task (2 KiB stack), micro-ROS task (16 KiB stack), LwIP tcpip thread, 8 KiB kernel heap, 32 KiB micro-ROS heap |

LwIP pool sizes are the same in both LwIP probes (`MEM_SIZE` 8 KiB, 8 pbufs of
1536 bytes) so that the difference is the execution model, not the buffers.

## Results

Bytes, from the link maps. "Over baseline" subtracts `empty_baseline`.

| Probe | FLASH used | FLASH over baseline | DTCM used | DTCM over baseline | DTCM over baseline without the heap and stack reservations |
|---|---:|---:|---:|---:|---:|
| empty_baseline | 1,056 | 0 | 1,568 | 0 | 0 |
| superloop_uros_serial | 105,244 | 104,188 | 70,804 | 69,236 | 36,468 (32,768 heap arena removed) |
| superloop_lwip_uros_udp | 114,228 | 113,172 | 92,684 | 91,116 | 58,348 (32,768 heap arena removed) |
| freertos_lwip_uros_udp | 126,876 | 125,820 | 123,668 | 122,100 | 60,148 (32,768 micro-ROS heap, 8,192 kernel heap, 16,384 + 2,048 + 512 + 1,024 task stacks, 1,024 arena removed) |

Region sizes from the linker script: FLASH 1,048,576, DTCM 131,072, RAM_D1
327,680, RAM_D2 32,768, RAM_D3 16,384, ITCM 65,536.

Where the bytes go (map_report `--top`, grouped by origin):

| Component | FLASH | DTCM (static) | Source of the figure |
|---|---:|---:|---|
| micro-ROS library (rcl, rclc, rmw_microxrcedds, XRCE client, type support) | 78,331 to 78,391 | 35,368 | all three micro-ROS probes |
| of which type-support descriptors in `.data` (copied from flash at boot) | in the above | about 15,200 | `toplevel_type_raw_source` and `*__FIELDS` symbols |
| of which rmw static pools (`custom_static_buffers` 8,448, `custom_sessions` 5,544, `custom_publishers` 2,160, `custom_subscriptions` 1,080) | | 17,232 | symbol sizes |
| newlib-nano and libgcc | 14,483 to 14,863 | 432 | all probes |
| LwIP core (IPv4, UDP, ARP, ICMP, Ethernet) | 8,508 (raw) / 8,880 (with sockets build) | 21,785 to 22,601 | LwIP probes; mostly `memp_memory_PBUF_POOL_base` 12,419 and `ram_heap` 8,211 |
| LwIP sequential and socket API, FreeRTOS `sys_arch` | 4,045 | 84 | FreeRTOS probe |
| FreeRTOS kernel (tasks, queues, lists, timers, event groups, stream buffers, port, heap_4) | 6,869 | 8,777 (8,192 of it is the kernel heap) | FreeRTOS probe |
| upstream micro-ROS FreeRTOS glue (allocators, time, second heap_4) | 808 | 32,800 (32,768 is its heap) | FreeRTOS probe |

Difference between the two Ethernet options, same micro-ROS application and
same LwIP pools: FreeRTOS + sockets costs 12,648 bytes more flash and, before
heaps and stacks, 1,800 bytes more DTCM. The real RAM cost of FreeRTOS is the
per-task stacks (the upstream micro-ROS README requires more than 10 kB for
the micro-ROS task) and the split into two heaps (kernel heap and the micro-ROS
heap from the upstream `custom_memory_manager.c`, which is a second heap_4
with its own `configTOTAL_HEAP_SIZE` array).

## What these numbers are not

- Not the application. No core, no adapters, no HAL drivers beyond the board
  baseline. Add the board baseline (Release FLASH 20,560, DTCM 3,336) and the
  core and adapter code of platform-fw #44 and #46 for a full image.
- Not a measurement of heap or stack use. The 32 KiB micro-ROS heap and the
  16 KiB micro-ROS stack are probe choices. Only a run on the board (heap
  high-water mark, stack painting) measures them; see the sizing table in the
  decision paper.
- The probes put the LwIP pools in DTCM (default `.bss`). That is wrong for a
  real Ethernet build: the Ethernet DMA cannot reach DTCM (see the decision
  paper, Ethernet section). In a real build the Rx buffers and the memory
  that Tx pbufs come from move to RAM_D2 or RAM_D1, which moves about 20 KiB
  out of DTCM.
- The micro-ROS figures depend on `colcon.meta`. More entities, a longer
  history or a larger MTU grow the rmw static pools.
- Message types are stand-ins. The I8 messages will change the type-support
  `.data` size.

## Reproduce

See `tools/size/README.md`. The full probe run takes about 10 s once the
micro-ROS library is built; building the library in Docker takes about 15
minutes.
