# Telemetry image for the STM32H723ZG (proposal)

Status: **proposal, builds and runs in Renode on the H743 model, never run on a board.**

A second firmware target next to [`../OpenAMRobot`](../OpenAMRobot/README.md). It only observes:
it reads the drive on CAN1 with SDO upload requests, listens to the drive heartbeat, feeds the
six sensor UARTs to the parsers, and prints one JSON status line every 100 ms on USART3.

## What the image never does

- No motion decision, no enable, no lift permit, no brake, contactor, lamp or charge output.
- No watchdog of any kind (command, host or hardware). A fatal error prints one line and halts in
  a loop with interrupts disabled (`tlm_fatal`, `src/tlm_main.c`). The halt is a property of this
  diagnostic image, so that the record stays readable at the bench. It is not watchdog behaviour:
  no watchdog is configured or fed, nothing resets the MCU, no output is switched. The console wait
  of that record is bounded (`TLM_FATAL_TX_SPIN_MAX` polls for the whole record).
- No CANopen write: no controlword, target velocity, NMT command or configuration object. The only
  frame it sends is an SDO initiate-upload request (`601h`, byte 0 = `40h`). `tlm_can1_tx()` in
  `src/tlm_can1_tx.c`, the only caller of `HAL_FDCAN_AddMessageToTxFifoQ`, refuses every other
  frame; a host test build (`tests/host/telemetry_image/test_can1_tx.c`) offers it an SDO download,
  NMT commands, RPDOs, SYNC and arbitrary identifiers and proves none reaches the HAL. The post-link guard (`cmake/check_image.cmake`) fails
  the build if the SDO download encoder is linked or a restricted object number appears in this
  directory, and the Renode stub records every frame that is not an upload request.
- No MB7060 trigger pulse. After `MX_GPIO_Init()` the four trigger lines PE2, PE3, PG2 and PG3 are
  push-pull outputs at the low level (`GPIO_PIN_RESET`, generated `main.c`), and this image never
  writes them again: no pulse is produced. PE11 (candidate IMU chip select, `../OpenAMRobot`
  README) is a push-pull output at the high level, inactive, and is not written either. These levels
  are read from the generated source; they were not measured on a board. `oamr_mb_arm()` is never
  called, so no MB7060 range is ever reported.
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
| `can1_tick` | 4 | 20 ms, `vTaskDelayUntil` | starts FDCAN1 (normal mode, filter `581h` and `701h`); then only the periodic tick: `oamr_co_upl_tick` over 606Ch:01, 606Ch:02, 6041h, 603Fh, 2026h:05 and the heartbeat classification. A deadline found already passed is counted (`tick_miss`, `src/tlm_sched.c`), the tick runs once and the timing restarts from now: no catch-up burst |
| `can1_rx` | 3 | frames as they arrive | frame handling only: feeds each received frame to the upload scheduler and the heartbeat consumer |
| `sensors` | 2 | 5 ms | restarts stopped UARTs, drains the UART rings: UART4, 5, 7, 8 to the MB7060 parser, USART2 and USART6 to the TF-Luna parser |
| `reporter` | 1 | 100 ms | prints the status line and, every 5th period, a slow line |

FDCAN1 interrupt: one entry takes at most `TLM_CAN1_RX_MAX_PER_IRQ` (4) frames from RX FIFO 0. Frames
left in the FIFO are taken on the next entry, which the wrapper of the generated handler
(`src/cubemx_it_reuse.c`) requests by pending the interrupt again (`rx_defer` counts such entries).
Hardware FIFO loss (RF0L, `rx_hw_lost`) and software queue overflow (`rx_q_ovf`) are counted
separately.

Sensor UARTs (`src/tlm_uart_rx.c`): every `HAL_UART_Receive_IT` call is checked. A call that does
not return `HAL_OK` is counted (`rx_start_fail`) and stops the port; the `sensors` task restarts it,
and after `TLM_UART_RX_RESTART_MAX` (3) failed attempts in a row the port is `failed`: the TF-Luna
object of the status line carries `"rx":"failed"` and the slow line lists the port. A full ring
discards the bytes buffered for that port (`ring_lost`, with the bytes dropped while it was full),
counts the event (`ring_ovf`), resets the port's parser and resynchronises on the next frame header.

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

### Boot id

`boot` counts boots in a record at `0x38000000` (RAM_D3) that survives a reset **only while the RAM
stays powered**. `boot_record` in the banner is `retained` or `cold`. After a power cycle, or if the
RAM content is lost for any other reason, the count restarts at 1. A boot id is therefore unique
only while RAM is kept, and `(boot, seq)` repeats across power cycles. A host must not rely on the
boot id alone: it must also invalidate everything it holds from this image when it reconnects to
the console (port reopened, cable replugged, host restarted), and whenever it sees a banner.

### Line budget

115200 baud 8N1 carries 1152 characters per 100 ms period. The status line is capped at 60 % (691
characters with the newline). The rarely changing fields are in a slow line that follows the
status line of every 5th period and is capped at 35 % (403); a period that carries both stays at or
below 95 % of the wire time. The slow line has two parts that alternate, so each is refreshed once
per second: `cfg` (seq 0, 10, 20, ...) and `diag` (seq 5, 15, ...). Lines captured in Renode: status
lines 560 to 575 characters, slow lines 330 to 370 (host test `test_format.c` builds a day-long
bad-bus line of 687). A line that does not fit its cap is never cut: it is replaced by
`{"t":"err",...,"e":"line_overflow"}` (or `"slow_overflow"` with `"of":"sl"`) and counted
(`too_long`). A line that cannot leave within one period is dropped and counted (`dropped`), a late
period is counted (`overrun`).

Status line, `"t":"st"`, every period:

| Field | Meaning |
|---|---|
| `boot` | boot id (above) |
| `seq` | line number since the reset, from 0, +1 per line |
| `up_us` | uptime, 64-bit microseconds |
| `drive.hb` | `st`: `disabled`, `waiting`, `alive`, `lost`; `age_us` of the last heartbeat |
| `drive.wheel.ch1`, `ch2` | 606Ch:01 and :02 in drive units. Which wheel is which, and the sign, are not known (`TODO(Evans)` in `zlac_telemetry.h`) |
| `drive.sw`, `fault` | 6041h (with CiA 402 `state`), 603Fh |
| each drive object | `v`: `valid`, `unconfirmed`, `not_read`, `timeout`, `aborted` (with `abort` code), `protocol`, `tx_failed`, `bad_size`, `stale`, `unset`; `raw`; `age_us` of the last good sample; `n` good samples |
| `sens.usart2`, `usart6` | TF-Luna: `st`, `v` (true, false or `"unknown"`), `mm`, `age_us`, `n`; `"rx":"failed"` only when the port's reception failed |
| `cnt` | counters. **Only counters that are not zero are printed.** Without `cnt_more`, an absent counter is zero. Counters are added only while they fit under the cap; `"cnt_more":N` says N non-zero counters were left out, and then an absent counter is unknown for that line |

Slow line, `"t":"sl"`, `part` `cfg`:

| Field | Meaning |
|---|---|
| `params` | `default` or `test` |
| `sdo` | `on` or `disabled`; `health`: SDO health of `co_upload_sched` (`disabled`, `starting`, `available`, `unavailable`), reporting only |
| `nmt` | last NMT state seen in a heartbeat, or `unknown` |
| `bus_off` | FDCAN1 bus-off as the controller reports it, or `unknown` |
| `res` | 2026h:05, same fields as a drive object |
| `dis` | names of the disabled features |

Slow line, `part` `diag`:

| Field | Meaning |
|---|---|
| `tick` | `[runs, late_max_ms]` of `can1_tick` |
| `mb` | MB7060 per UART (`uart4` ... `uart8`): `[age_us, frames]` of the last well-formed frame. No range: this image sends no trigger |
| `rx` | sensor ports that are not receiving (`stopped`, `failed`); empty when all are |
| `stk` | stack high-water mark of each task (`can1_tick`, `can1_rx`, `sensors`, `reporter`, `idle`): words never used since start |

A value that is not known is the string `"unknown"`, never `0`.

## Tests

```bash
python3 tools/ci/renode_telemetry.py --renode <renode> --elf-test build/RenodeTest/telemetry_image.elf --elf-default build/Debug/telemetry_image.elf
```

```bash
python3 -m pytest tests/host/telemetry_log -q
```

```bash
cmake -S tests/host/telemetry_image -B build/host-telemetry-image
```

```bash
cmake --build build/host-telemetry-image
```

```bash
ctest --test-dir build/host-telemetry-image --output-on-failure --no-tests=error
```

Scenarios: `nominal`, `drive_silent`, `heartbeat_stops`, `wheel_abort`, `duplicate_late`,
`reset_mid_run`, `default_build`, `sustained_traffic` (bursts of 12 accepted frames every 5 ms: at
most 4 frames per interrupt entry, tick deadline misses zero or reported, no frame lost without a
count), `uart_overflow_tfluna` (300 bytes at once on USART2: overflow and loss counted, the next
sample valid), `reset_keep_ram` (boot id 1, 2, 3 over two resets, sequence from zero each time),
`fresh_start` (a fresh Renode start after a run that reached boot id 3: boot id 1, cold record,
sequence from zero). The workflow `.github/workflows/telemetry-image.yml` builds the three presets,
runs the host tests and the scenarios, and fails on a skipped or blocked scenario.

The Renode run uses Renode's `stm32h743.repl` with `tests/renode/stm32h723-additions.repl`, a CAN
hub, and the drive stub `tools/ci/renode_drive_stub.py`. It prints the peripheral accesses Renode
did not model. The stub is a test fixture with made-up values, not a model of the ZLAC8015D.

## Not verified

Nothing here ran on a NUCLEO-H723ZG or on the robot. Not verified at all: clock tree and baud
rates, CAN bit timing and acknowledge, bus-off behaviour, UART RX inversion, reset-cause flags,
the RAM_D3 boot record on real RAM (ECC, power-up content), stack depths under real interrupt
load, and the console wire time. No real drive or sensor was connected. Also not verified: the
self-pended FDCAN interrupt on the device (Renode's NVIC accepts the pending write), RF0L on the
device (Renode's MCAN model did not report a FIFO loss in any scenario), a `HAL_UART_Receive_IT`
failure on the device (the restart policy is host-tested only), the GPIO levels above, and the
stack marks under real interrupt load (Renode values are reported in the slow line).
