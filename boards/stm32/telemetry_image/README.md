# Telemetry image for the STM32H723ZG (proposal)

Status: **proposal, builds and runs in Renode on the H743 model, never run on a board.**

A second firmware target next to [`../OpenAMRobot`](../OpenAMRobot/README.md). It only observes:
it reads the drive on CAN1 with SDO upload requests, listens to the drive heartbeat, feeds the
six sensor UARTs to the parsers, and prints one JSON status line every 100 ms on USART3.

## What the image never does

- No motion decision, no enable, no lift permit, no brake, contactor, lamp or charge output.
- No watchdog of any kind (command, host or hardware). A fatal error prints one line and stops in
  a loop; the image has no restart mechanism.
- No CANopen write: no controlword, target velocity, NMT command or configuration object. The only
  frame it sends is an SDO initiate-upload request (`601h`, byte 0 = `40h`). `can1_send()` in
  `src/tlm_can1.c` refuses every other frame, the post-link guard (`cmake/check_image.cmake`) fails
  the build if the SDO download encoder is linked or a restricted object number appears in this
  directory, and the Renode stub records every frame that is not an upload request.
- No MB7060 trigger pulse. The four trigger lines keep the static low level of the generated GPIO
  initialisation. `oamr_mb_arm()` is never called, so no MB7060 range is ever reported.
- PE1 is not turned into the LD2 lamp output (the generated `main()` does that; this image only
  releases the pin from UART8_TX).

Timeouts and the heartbeat consumer change what the status line says and nothing else.

## What is reused, unchanged

| From | Used how |
|---|---|
| `../OpenAMRobot/Drivers`, `Core/Inc`, `stm32h7xx_hal_msp.c`, `system_stm32h7xx.c`, `startup_stm32h723xx.s`, `sysmem.c`, `syscalls.c`, `STM32H723xG_flash.ld`, `cmake/stm32cubemx/CMakeLists.txt`, `cmake/gcc-arm-none-eabi.cmake` | compiled and linked as they are |
| `../OpenAMRobot/Core/Src/main.c` | included verbatim by `src/cubemx_main_reuse.c` with `main` renamed; the image calls the generated `MPU_Config`, `SystemClock_Config`, `MX_GPIO_Init`, `MX_FDCAN1_Init` and the six sensor `MX_*UART*_Init` |
| `../OpenAMRobot/Core/Src/stm32h7xx_it.c` | included verbatim by `src/cubemx_it_reuse.c`; `SVC_Handler`, `PendSV_Handler` and `SysTick_Handler` are renamed because FreeRTOS owns those vectors |
| `firmware/telemetry` (reset cause, freshness), `firmware/sensor_bridge` (MB7060, TF-Luna), `firmware/canopen`, `firmware/drive_telemetry` | compiled as they are |

Not initialised here: FDCAN2, FDCAN3, SPI4, Ethernet. No file under `../OpenAMRobot` and no
`.ioc` is edited. USART3 is not in the CubeMX project; `src/tlm_console.c` sets it up.

FreeRTOS-Kernel `V11.1.0` (MIT) is fetched at configure time and checked against commit
`dbf70559b27d39c1fdb68dfb9a32140b6a6777a0`; it is not vendored. Port `GCC/ARM_CM7/r0p1`,
`tasks.c`, `queue.c`, `list.c`. Static allocation only (`configSUPPORT_DYNAMIC_ALLOCATION 0`, no
`heap_*.c`); the post-link guard fails the build if `malloc`, `_sbrk` or `pvPortMalloc` is linked.

## Tasks

| Task | Priority | Period | Does |
|---|---|---|---|
| `can1_reader` | 3 | 20 ms tick, frames as they arrive | starts FDCAN1 (normal mode, filter `581h` and `701h`), runs `oamr_co_upl_tick` over 606Ch:01, 606Ch:02, 6041h, 603Fh, 2026h:05 and the heartbeat consumer |
| `sensors` | 2 | 5 ms | drains the UART rings: UART4, 5, 7, 8 to the MB7060 parser, USART2 and USART6 to the TF-Luna parser |
| `reporter` | 1 | 100 ms | prints the status line |

## Parameters

Every timing constant is in [`include/tlm_params.h`](include/tlm_params.h) with its source.
A value that is still open (`TODO(Evans)` or "no default" in the module that needs it) is **not
active**: the feature is disabled and the banner and the status line say so. With the default
parameters that is: heartbeat consumer, SDO uploads (so the image sends nothing on CAN1), drive
sample age limit, speed resolution check, TF-Luna decoding and both sensor stale limits.

The preset `RenodeTest` defines `TLM_TEST_PARAMS`, which activates a block of **unapproved test
values** in the same header (for example heartbeat timeout 300 ms, SDO timeout 40 ms). Such an
image prints `"params":"test"`. It exists for the Renode tests and must not be flashed to a board
that is connected to a drive.

## Build

```bash
cmake --preset Debug
```

```bash
cmake --build --preset Debug
```

`Release` and `RenodeTest` work the same way. Needs `arm-none-eabi-gcc`, CMake 3.22 or later,
Ninja, git and network access to github.com at configure time (FreeRTOS fetch).

## Console

USART3, PD8 (TX) and PD9 (RX), 115200 8N1. One JSON object per line.

Banner after every reset: `"t":"boot"`, `project`, `commit` (git hash, `-dirty` if the tree had
changes), `build`, `params`, `reset_cause` (decoded by `oamr_reset_cause_decode`), `rcc_rsr`,
`boot`, `boot_record`, and `disabled` with the reason for every disabled feature.

Status line, `"t":"st"`:

| Field | Meaning |
|---|---|
| `boot` | boot id: count kept in a record at `0x38000000` (RAM_D3) that survives a reset while the RAM stays powered. `boot_record` in the banner is `retained` or `cold`. After a power cycle the count restarts at 1, so the id is unique between power cycles only |
| `seq` | line number since the reset, from 0, +1 per line |
| `up_us` | uptime, 64-bit microseconds |
| `drive.hb` | `st`: `disabled`, `waiting`, `alive`, `lost`; `age_us` of the last heartbeat; `nmt`: last state seen |
| `drive.wheel.ch1`, `ch2` | 606Ch:01 and :02 in drive units. Which wheel is which, and the sign, are not known (`TODO(Evans)` in `zlac_telemetry.h`) |
| `drive.sw`, `fault`, `res` | 6041h (with CiA 402 `state`), 603Fh, 2026h:05 |
| each drive object | `v`: `valid`, `unconfirmed`, `not_read`, `timeout`, `aborted` (with `abort` code), `protocol`, `tx_failed`, `bad_size`, `stale`, `unset`; `raw`; `age_us` of the last good sample; `n` good samples |
| `sens.uart4` ... `uart8` | MB7060: `v` is always `"unknown"` (no trigger); `age_us` of the last well-formed frame; `n` frames |
| `sens.usart2`, `usart6` | TF-Luna: `st`, `v` (true, false or `"unknown"`), `mm`, `age_us`, `n` |
| `cnt` | counters. **Only counters that are not zero are printed; an absent counter is zero** |
| `dis` | names of the disabled features |

A value that is not known is the string `"unknown"`, never `0`.

Line length: 115200 baud carries 1152 characters per 100 ms. Lines captured in Renode are 890 to
960 characters, so the console is busy for about 80 % of the time. A line above 1100 characters is
replaced by `{"t":"err",...,"e":"line_overflow"}`; a line that cannot leave within one period is
dropped and counted (`dropped`), a late period is counted (`overrun`). This margin is a design
point for the owner (fewer fields or a faster console).

## Tests

```bash
python3 tools/ci/renode_telemetry.py --renode <renode> --elf-test build/RenodeTest/telemetry_image.elf --elf-default build/Debug/telemetry_image.elf
```

```bash
python3 -m pytest tests/host/telemetry_log -q
```

The Renode run uses Renode's `stm32h743.repl` with `tests/renode/stm32h723-additions.repl`, a CAN
hub, and the drive stub `tools/ci/renode_drive_stub.py`. It prints the peripheral accesses Renode
did not model. The stub is a test fixture with made-up values, not a model of the ZLAC8015D.

## Not verified

Nothing here ran on a NUCLEO-H723ZG or on the robot. Not verified at all: clock tree and baud
rates, CAN bit timing and acknowledge, bus-off behaviour, UART RX inversion, reset-cause flags,
the RAM_D3 boot record on real RAM (ECC, power-up content), stack depths under real interrupt
load, and the console wire time. No real drive or sensor was connected.
