# Board baseline memory map (platform-fw #22)

Supports `docs/architecture/runtime-decision.md`. Status: CROSS-COMPILED on a
Linux runner; nothing ran on a board.

## Build

```bash
sudo apt-get install gcc-arm-none-eabi libnewlib-arm-none-eabi
cd boards/stm32/OpenAMRobot
cmake --preset Debug   && cmake --build --preset Debug
cmake --preset Release && cmake --build --preset Release
python3 ../../../tools/size/map_report.py build/Release/OpenAMRobot.map
```

Toolchain: arm-none-eabi-gcc 13.2.1 20231009 (Ubuntu package
15:13.2.rel1-2), CMake 3.31.6. Source: the CubeMX project as merged on this
branch (last change under `boards/stm32/OpenAMRobot` at fe2c3f3). Both
presets build without errors.

`arm-none-eabi-size`:

| Preset | text | data | bss |
|---|---:|---:|---:|
| Debug | 38,696 | 16 | 3,512 |
| Release | 20,544 | 16 | 3,512 |

The FLASH totals below (text plus the `.data` load image) equal what the
linker prints with `--print-memory-usage` (Debug 38,712, Release 20,560).

## Per region (map_report.py)

| Region | Origin | Size | Debug used | Release used | Contents |
|---|---|---:|---:|---:|---|
| FLASH | 0x08000000 | 1,048,576 | 38,712 (3.69 %) | 20,560 (1.96 %) | `.text`, `.isr_vector` 716, `.rodata` 132, `.data` load image 16 |
| DTCMRAM | 0x20000000 | 131,072 | 3,336 (2.55 %) | 3,336 (2.55 %) | `.bss` 1,780, `._user_heap_stack` 1,540, `.data` 16 |
| RAM_D1 (AXI SRAM) | 0x24000000 | 327,680 | 0 | 0 | nothing |
| RAM_D2 (AHB SRAM) | 0x30000000 | 32,768 | 192 | 192 | `.RxDescripSection` 96, `.TxDescripSection` 96 (4 Rx and 4 Tx Ethernet DMA descriptors) |
| RAM_D3 | 0x38000000 | 16,384 | 0 | 0 | nothing |
| ITCMRAM | 0x00000000 | 65,536 | 0 | 0 | nothing |

## Observations that matter for the runtime decision

1. All data, the heap and the main stack sit in DTCM. `_estack` is the top of
   DTCM, `_Min_Heap_Size` is 0x200 (512 bytes) and `_Min_Stack_Size` is 0x400
   (1,024 bytes). `_sbrk` in `Core/Src/sysmem.c` grows the heap from `_end`
   up to `_estack - _Min_Stack_Size`, so the heap can in fact use all free
   DTCM; the 0x200 is only a link-time minimum. Nothing stops the main stack
   from growing down into the heap or `.bss`.
2. Only the Ethernet DMA descriptors are placed outside DTCM. The Rx data
   buffers (`RxBuffLen` 1536) are not allocated yet: the generated code leaves
   them to the application.
3. `MPU_Config()` sets one region: background region 0, base 0x0, 4 GB, no
   access, subregion mask 0x87, not cacheable. There is no region over
   RAM_D2 yet.
4. `SCB_EnableICache()` and `SCB_EnableDCache()` are not called. The caches
   are off in the baseline.
5. The MAC address is the fixed value 00:80:E1:00:00:00 in `MX_ETH_Init()`.
6. `HardFault_Handler` is the generated endless loop. There is no IWDG in the
   .ioc.
7. The HAL time base is SysTick at priority 0 (`TICK_INT_PRIORITY` 0,
   `USE_RTOS` 0). The FDCAN, UART and ETH interrupts are at priority 5,
   priority group 4.
8. SYSCLK and CPU clock are 192 MHz, HCLK 96 MHz (`.ioc`).
