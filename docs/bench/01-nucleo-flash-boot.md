# 01 NUCLEO-H723ZG: flash, boot, LOCKED, IWDG, reset cause

**Proves:** the board runs the image we built (digest), boots into the LOCKED state with no motion
possible, the independent watchdog resets a stalled control loop, and the reset cause is read
correctly for each kind of reset.
**Moves anything?** No. Nothing but the NUCLEO is connected: no CAN transceiver wiring to other
nodes, no drive, no motor supply.

> **Firmware status.** The application is the draft stack platform-fw #22 (CubeMX project, builds
> only) plus #37 and #40 (base core, board port, commissioning record, safety core). None of it
> has run on hardware. Where a step needs something the draft does not do, it is marked
> **[firmware gap]** and the expected result of that step is a visible FAIL, which is a valid
> bench result.

## Preconditions

- Two people not needed (nothing moves). One person is enough; write the name in the record.
- NUCLEO-H723ZG alone on the bench, powered from the ST-LINK USB cable only. No other board
  connected to the Zio headers. Board revision: `TODO(Arka)` (read it from the board label).
- Image built from the tip of the stack (#40), commit recorded. Build with the presets of #22:
  **[pending #22]**
  ```
  cd boards/stm32/OpenAMRobot
  cmake --preset Debug   && cmake --build --preset Debug
  cmake --preset Release && cmake --build --preset Release
  ```
  Outputs are in `build/Debug/` and `build/Release/` (`OpenAMRobot.elf`). Reference sizes of the
  bare #22 project (before #37/#40): Debug 38,712 and Release 20,560 flash bytes. With #37/#40
  the sizes differ; record the sizes you get.
- Host tools: `st-flash`, `openocd` (or STM32CubeProgrammer), `gdb-multiarch` or `arm-none-eabi-gdb`.
- A serial terminal on the ST-LINK virtual COM port (USART3, PD8/PD9, 115200 8N1 in the draft):
  `python3 -m serial.tools.miniterm /dev/serial/by-id/<persistent path> 115200`.

## Step 1. Identify the probe and the board

```
st-info --probe
ls -l /dev/serial/by-id/
```

Expected: one ST-LINK, chip id for STM32H72x/H73x (`STM32H723ZG` after `openocd` below), flash 1024 KiB.
Record the probe serial and the persistent serial path. FAIL if no probe or two probes.

## Step 2. Build digest

```
sha256sum boards/stm32/OpenAMRobot/build/Release/OpenAMRobot.elf
sha256sum boards/stm32/OpenAMRobot/build/Debug/OpenAMRobot.elf
```

Record both digests, the compiler (`arm-none-eabi-gcc --version`; the #22 verification used
13.3.1) and the commit. The digest of the file flashed in step 3 must be one of these.

## Step 3. Flash

Debug first (the Debug build freezes the watchdog while the core is halted, see step 6).

```
openocd -f interface/stlink.cfg -f target/stm32h7x.cfg \
  -c "program boards/stm32/OpenAMRobot/build/Debug/OpenAMRobot.elf verify reset exit"
```

Expected: `** Programming Finished **`, `** Verified OK **`, exit 0. FAIL otherwise.
With the HIL scaffold **[pending #29]** the same step with an image digest check is
`python3 tools/hil/flash.py --target stm32 --image <hex> --sha256 <digest>` (needs `HIL_BENCH=1`, an
ihex file from `arm-none-eabi-objcopy -O ihex`, and uses `st-flash` or STM32CubeProgrammer).
Do not mix the two methods in one record: use one and name it.

Option bytes (BOR level, IWDG hardware mode) are left as shipped; read and record them:
`TODO(Evans)` BOR level, and whether IWDG1 must be started by hardware (option byte) or by software
as in the draft (`port_iwdg_start`).

## Step 4. Boot and LOCKED

Open the terminal on the virtual COM port, press the black RESET button on the NUCLEO.

Expected from the draft: the board starts the control tick (TIM6, 50 Hz) with the safety core in
LOCKED, because the commissioning record is incomplete: every field is unset in
`Port/Inc/port_commissioning.h`, so the safety core stays LOCKED (Software Stop asserted), the
ZLAC adapter is not started (nothing is sent on CAN1) and wheel velocity is written as zero.

**[firmware gap]** The draft prints no boot banner and has no console output. The HIL scaffold **[pending #29]**
expects these lines at 115200 baud after reset:
```
OPENAMR BOOT fw=<git sha> board=NUCLEO-H723ZG
OPENAMR RESET reason=<flags>
OPENAMR IMU whoami=0x47
```
Until the firmware prints them, the check is done with the debugger (step 5). Evans decides
whether the banner stays in release builds (it must not carry anything that moves hardware).

## Step 5. Read the state with the debugger

Terminal 1:
```
openocd -f interface/stlink.cfg -f target/stm32h7x.cfg
```
Terminal 2:
```
gdb-multiarch boards/stm32/OpenAMRobot/build/Debug/OpenAMRobot.elf
(gdb) target extended-remote :3333
(gdb) monitor reset halt
(gdb) continue
(gdb) interrupt
(gdb) print/x s_safety.out
(gdb) print/x s_safety.out.reasons
(gdb) print s_commissioning_missing
(gdb) print s_app_ready
```

The variables are file-static in `Port/Src/port_app.c` of #40; use the symbol names of the image
you flashed (`info variables s_safety`). Expected:

| Variable | Expected | Meaning |
|---|---|---|
| `s_app_ready` | 1 | `port_app_init()` finished |
| `s_safety.out.state` | 0 (`OAMR_SC_LOCKED`) | boot state |
| `s_safety.out.motion_allowed` | 0 | no motion |
| `s_safety.out.reset_lamp` | 1 | lamp on in LOCKED (lamp pin unassigned, see procedure 07) |
| `s_commissioning_missing` | non-zero | record incomplete, all fields unset |
| `s_zlac_enabled` | 0 | ZLAC adapter not started |

PASS: `motion_allowed` is 0 and state is LOCKED. FAIL if `motion_allowed` is ever 1 in this
procedure. Record the raw output.

## Step 6. IWDG: a stalled control loop resets the MCU

The IWDG1 is fed only from the control tick (`port_iwdg_feed()` in `control_tick`). Timeout in the
draft: `PORT_IWDG_TIMEOUT_MS` = 100 ms, `TODO(Evans)`. The Debug build freezes the watchdog while
the core is halted (`DBGMCU_APB4FZ1_DBG_IWDG1`), so use the **Release** build for this step.

1. Flash Release as in step 3 (path `build/Release`). Do not attach the debugger.
2. Stall the loop. The draft has no test hook. Options, for Evans to pick:
   - (a) Build a one-off test image with the TIM6 interrupt disabled after `port_app_init()`
     (`NVIC_DisableIRQ(TIM6_DAC_IRQn)`), flash it, and watch the reset.
   - (b) With the Release image running and no debugger attached, there is no way to stall the
     loop without a hook. Do not use the Debug build for this step: it freezes the watchdog
     while the core is halted, so the watchdog would never fire under gdb.
   The TIM6 interrupt number is from `stm32h723xx.h` of #22 (`TIM6_DAC_IRQn = 54`); check it.
3. Expected: the MCU resets within the timeout plus margin (about 100 ms, `TODO(Evans)`), then runs
   normally again (the disabled IRQ is re-enabled by the reset). Measure the time with a logic
   analyser or scope on any output pin toggled at boot (`TODO(Evans)`: which pin) or by the gap in
   the debugger-free SWO/serial output if the banner exists.
4. Read the cause after the reset (step 7): `IWDG` must be set.

PASS: reset occurs, cause is IWDG, the board is LOCKED again. FAIL: no reset within 1 s.
Record the measured time and the timeout setting. **Stop condition:** none (nothing connected).

## Step 7. Reset cause for each reset type

The cause is captured in `port_capture_reset_cause()` from `RCC_RSR` (address
`0x580244D0`, computed from `RCC_BASE` and the 0xD0 offset in `stm32h723xx.h`; verify on the chip) and cleared with RMVF. Several flags can be set at once; a pin reset
accompanies most others. The draft keeps the value in `s_cause` and the raw register in
`s_raw_rsr` (file-static in `port_watchdog.c`).

For each row: cause the reset, then read with the debugger (the debugger reset itself adds
flags: use a `monitor reset run` (which does a system reset) and `detach` before reading, or read the
raw value `s_raw_rsr` and judge the flag set).

| Cause the reset by | Expect in `s_cause` bits (see `oamr/reset_cause.h`) |
|---|---|
| unplug and replug the USB cable (power cycle) | POWER_ON, usually with PIN |
| black RESET button | PIN only |
| step 6 (IWDG) | INDEPENDENT_WATCHDOG, usually with PIN |
| `NVIC_SystemReset()` (software), from gdb: `call NVIC_SystemReset()` | SOFTWARE, usually with PIN |
| brown-out: **not tested**, needs a controlled supply, option byte BOR level `TODO(Evans)` | BROWNOUT |

Print: `print/x s_cause` and `print/x s_raw_rsr`. Bit values from the header: UNKNOWN 0x001,
POWER_ON 0x002, PIN 0x004, BROWNOUT 0x008, IWDG 0x010, WWDG 0x020, SOFTWARE 0x040,
LOW_POWER 0x080, CPU 0x100, DOMAIN 0x200.

PASS: each row shows the named bit. A cause that shows only UNKNOWN is a FAIL (the draft maps
unidentified flags to UNKNOWN, never to power-on). Record the raw RSR for each row, since the
final enum is Evans's decision (I8 rev 3 decision 6).

## Record

Probe serial, board revision, image digests and sizes, the raw output of step 5, the measured
IWDG time, the table of step 7 with raw RSR values, the photo of the setup.

## Open values (TODO)

- `TODO(Arka)` NUCLEO board revision, probe serial.
- `TODO(Evans)` IWDG timeout (draft 100 ms), BOR level, hardware or software IWDG start.
- `TODO(Evans)` how to stall the control loop for the test (test hook or gdb), and which pin shows the reset.
- `TODO(Evans)` whether a boot banner exists in the 2.0 image and its text.
