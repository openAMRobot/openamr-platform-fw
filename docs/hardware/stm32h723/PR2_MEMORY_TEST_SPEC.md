# PR 2 Memory and Initialization Test Specification

**Status:** implementation and verification specification; executed evidence is recorded separately in `IMPLEMENTATION_PROGRESS.md`  
**Scope:** non-actuating STM32H723 memory/initialization corrections only  
**Normative inputs:** `STM32H723_FIRMWARE_DESIGN.md` §6.4 and §8.1–8.6; `PR_IMPLEMENTATION_ROADMAP.md` PR 2; `REQUIREMENTS_TRACEABILITY.md` RT-25–RT-28

## 1. Required end state

PR 2 shall establish all of the following. The source anchors in §2 describe the pre-PR-#22 condition and the `49d6ea0` (PR #22) baseline that PR 2 branches from; they must not be mistaken for passing results.

1. `HAL_ETH_MspInit()` explicitly enables the SRAM1 interface clock using `__HAL_RCC_D2SRAM1_CLK_ENABLE()` before `HAL_ETH_Init()` can initialize either descriptor table. The enable shall be inside the `heth->Instance == ETH` path, not merely somewhere later in application initialization. This enable, and the `0x1000` stack values of item 2, are **inherited from `49d6ea0` (PR #22)**: PR 2 neither adds nor changes them and verifies them (M-P01, M-P03). An enable only in `main.c`, in another function, or in `HAL_ETH_MspInit` outside that branch does not satisfy item 1.
2. Both the linker reservation and CubeMX `.ioc` setting specify at least 4 KiB (`0x1000`) of stack and agree numerically. The linked image shall retain that reservation. Both values are inherited from `49d6ea0` (PR #22); PR 2 re-orders the `_estack`/`_sstack` definitions after `_Min_Stack_Size` in the linker script but does not change the value.
3. The GNU linker places the complete Ethernet descriptor tables at these exact half-open ranges:

| Object | Size | Start, inclusive | End, exclusive | Last byte |
|---|---:|---:|---:|---:|
| `DMARxDscrTab` | 96 bytes (`0x60`) | `0x30000000` | `0x30000060` | `0x3000005F` |
| `DMATxDscrTab` | 96 bytes (`0x60`) | `0x30000060` | `0x300000C0` | `0x300000BF` |

   Both starts shall be 32-byte aligned. The two 96-byte extents shall be adjacent, non-overlapping, and wholly within SRAM1. Their section VMAs, ELF symbol addresses, and symbol sizes shall agree.
4. SRAM1 is exactly `0x30000000`–`0x30003FFF` (16 KiB; end-exclusive `0x30004000`). SRAM2 is declared as a separate future region at `0x30004000`–`0x30007FFF` (16 KiB; end-exclusive `0x30008000`). No packet buffer, section, or other object is allocated to SRAM2 in PR 2, and no SRAM2 enable or packet-buffer functionality is claimed.
5. Descriptor sections remain `NOLOAD`. D-cache remains disabled. PR 2 does not add ECC behavior, option-byte writes, clock-tree migration, actuator/safety behavior, or packet transmission.
6. The pre-PR-#22 working tree also contained a second `__HAL_RCC_D2SRAM1_CLK_ENABLE()` in `Core/Src/main.c` (USER CODE Init). No code that runs before `MX_ETH_Init()` accesses SRAM1 (the sensor DMA buffers are first used by `sensor_dma_init()`, after `MX_ETH_Init()`, in a later PR), so it was **removed as redundant**. The authoritative enable is the one in `HAL_ETH_MspInit()`; PR 2 shall not add a `main.c` enable.

The 96-byte requirement follows from the current four descriptors per direction and six 32-bit words per descriptor. The implementation should make that assumption explicit with build-time type/count checks; an ELF/map check remains authoritative for the final placement.

## 2. Exact current-source anchors

Paths below are relative to `boards/stm32/OpenAMRobot/` unless noted. Line numbers identify the pre-PR-#22 condition at local commit `5d09d7dc77e465d299d05248f378f440459ab1aa`; the branch point for PR 2 is `49d6ea010d9d72be8055710aff3c2000ce65b5f3` (PR #22), which already contains the corrections marked *inherited* below. Use symbols as well as lines after edits. This is a specification for checks to implement, not a claim that every listed check already exists or has passed.

| Anchor | Current fact relevant to review |
|---|---|
| `Core/Src/stm32h7xx_hal_msp.c:84–149`, `HAL_ETH_MspInit` | **Pre-PR-#22 condition:** ETH clocks are enabled at lines 93–95, but there is no explicit SRAM1 clock enable. *Inherited:* `49d6ea0` (PR #22) adds `__HAL_RCC_D2SRAM1_CLK_ENABLE()` in the ETH instance path before the ETH clock enables; PR 2 does not modify this file. |
| `Core/Src/main.c:249–281`, `MX_ETH_Init` | `heth.Init.TxDesc`/`RxDesc` are assigned at lines 270–271 and `HAL_ETH_Init(&heth)` is called at line 278. |
| `Core/Src/main.c:44–59`, descriptor declarations | The GNU path uses `.RxDescripSection` and `.TxDescripSection` at lines 57–58. Legacy non-GNU TX examples at lines 47–53 use `0x30000080`; they are not GNU ELF placement evidence. |
| `Core/Src/main.c:136–148` | `MX_ETH_Init()` is called at line 145 during peripheral initialization. |
| `Core/Inc/stm32h7xx_hal_conf.h:223–224` | `ETH_TX_DESC_CNT` and `ETH_RX_DESC_CNT` are each `4U`. |
| `Drivers/STM32H7xx_HAL_Driver/Inc/stm32h7xx_hal_eth.h:69–77` | `ETH_DMADescTypeDef` contains six 32-bit fields (24 bytes). |
| `Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_eth.c:314–411` | `HAL_ETH_Init` calls MSP initialization at lines 326–341, then TX/RX descriptor initialization at lines 407–411. |
| Same HAL source, `3064–3092` and `3102–3136` | TX initialization writes descriptors at lines 3070–3077; RX initialization writes them at lines 3107–3116. These are the first descriptor-write events the instrumented test must order against SRAM1 enable. |
| `Drivers/STM32H7xx_HAL_Driver/Inc/stm32h7xx_hal_rcc.h:1225–1241` | The target macro is `__HAL_RCC_D2SRAM1_CLK_ENABLE()`; its read-back delay is part of the macro. |
| `STM32H723xG_flash.ld:56–71` | One combined `RAM_D2` region is currently 32 KiB at line 60; `_Min_Stack_Size` is `0x400` at line 71 (pre-PR-#22; `49d6ea0` sets `0x1000`, inherited). |
| `STM32H723xG_flash.ld:158–159` | Both descriptor output sections are `NOLOAD`, 32-byte aligned, and currently target `RAM_D2`. Explicit extents/symbols and separate SRAM1/SRAM2 regions are still required. |
| `STM32H723xG_flash.ld:236–245` | Heap and stack reservation is emitted in `._user_heap_stack` in DTCM. Do not infer stack size from that combined section's size alone. |
| `OpenAMRobot.ioc:267,279` | Heap is `0x200`; `ProjectManager.StackSize=0x400` mismatched the required minimum before PR #22; `49d6ea0` sets `0x1000` (inherited). |
| `CMakeLists.txt:40–42` | The linker script is already a `LINK_DEPENDS` entry and must remain so. |
| `tools/ci/check_dma_placement.py:8–15` (repository-relative) | The existing checker checks only whether each start is somewhere in `0x30000000`–`0x30007FFF`; it does not check exact starts, extents, alignment, overlap, split regions, stack, `.ioc`, or clock order. |

## 3. Positive checks

Implement focused checks with stable IDs and diagnostics that name the observed and expected values.

| ID | Check | Pass condition |
|---|---|---|
| M-P01 | SRAM1 source presence and location | Exactly the SRAM1 enable macro (or an explicitly reviewed equivalent for this target) occurs in the ETH instance path of `HAL_ETH_MspInit`. ETH1MAC/TX/RX or SRAM2 enables do not satisfy this check. The enable is inherited from `49d6ea0` (PR #22); this check verifies it and does not require this PR to add it. It must be inside the `if (heth->Instance == ETH)` branch: an enable outside that branch, only in `main.c`, in another function, or only in a comment fails. |
| M-P02 | Instrumented initialization order | A fake/instrumented HAL run records `SRAM1_ENABLE` before the first TX or RX descriptor write. Also exercise the callback-enabled path if `USE_HAL_ETH_REGISTER_CALLBACKS=1` is supported. Include both descriptor tables and record the complete event trace. |
| M-P03 | Stack configuration consistency | Parsed linker `_Min_Stack_Size` and `.ioc` `ProjectManager.StackSize` are equal and each `>= 0x1000`; the linked `_estack - _sstack >= 0x1000` and the stack remains in DTCM. |
| M-P04 | Exact ELF/map placement | RX and TX symbol addresses and section VMAs equal the table in §1 in both Debug and Release. |
| M-P05 | Full extents | Each ELF symbol and corresponding output section is exactly `0x60` bytes. Checking only `ADDR(section)` or only the first descriptor is insufficient. |
| M-P06 | Alignment and containment | `start % 32 == 0` for each table and `0x30000000 <= start` and `end <= 0x30004000` for each complete extent. Use half-open intervals to avoid end-address off-by-one errors. |
| M-P07 | Non-overlap and exclusivity | RX/TX extents do not overlap each other or any other allocated output section; no unrelated section is silently placed in the descriptor ranges. Expected result is RX immediately followed by TX. |
| M-P08 | Split memory map | SRAM1 has origin `0x30000000`, length `0x4000`; SRAM2 has origin `0x30004000`, length `0x4000`. Descriptor sections target SRAM1, not a combined 32-KiB `RAM_D2` or an overlapping alias. |
| M-P09 | SRAM2 remains unallocated | Debug and Release maps contain no allocated section/object in `[0x30004000,0x30008000)`. Merely declaring the MEMORY region is not an allocation. |
| M-P10 | Configuration regeneration and link dependency | CubeMX review confirms the `.ioc` and linker values agree and user code retains the SRAM1 enable. Touching the linker script causes relinking; no unrelated peripheral timing change appears in the reviewed diff. |
| M-P11 | Both embedded configurations | Fresh Debug and Release builds complete with no new unexplained warnings and all M-P03–M-P09 checks run against each configuration's own ELF/map, not a reused artifact. |

A source-text check alone must not be reported as M-P02. The instrumented fake establishes order only for the modeled call path; see §6.

## 4. Intended-fail fixtures

Every fixture must make the checker/test exit nonzero and emit the expected diagnostic. A fixture that passes, crashes without an assertion, or fails only for an unrelated parse reason is not valid negative evidence. Keep a corresponding valid control so each mutation is known to be the cause.

| ID | Mutation | Required detection |
|---|---|---|
| M-F01 | Remove `__HAL_RCC_D2SRAM1_CLK_ENABLE()` | M-P01 fails with “missing SRAM1 enable.” |
| M-F02 | Enable SRAM2, enable only ETH1MAC/TX/RX, or enable SRAM1 only after `HAL_ETH_Init()` returns | Wrong/late clock is rejected; another peripheral clock cannot substitute. |
| M-F03 | Fake HAL performs a TX or RX descriptor write, then records `SRAM1_ENABLE` | M-P02 fails and prints the out-of-order trace. Repeat with the enable after MSP/descriptor initialization rather than merely moving unrelated source lines. |
| M-F04 | RX starts at `0x30000020`, or TX starts at legacy `0x30000080` | Exact-address check fails even though both addresses may be aligned and in D2 SRAM. |
| M-F05 | Symbol or section extent is 95 or 97 bytes instead of 96 | Exact full-extent check fails; start-only acceptance is prohibited. |
| M-F06 | RX/TX extent ends beyond `0x30004000`, or a table is moved wholly into SRAM2 | SRAM1 containment fails for the complete interval. |
| M-F07 | RX or TX starts at `0x30000004` (or another non-32-byte boundary) | Alignment fails independently of exact-address validation in an arithmetic unit fixture. |
| M-F08 | TX begins at `0x30000030`/`0x30000050`, or two synthetic ranges partially overlap | Non-overlap fails using full extents, not just unequal start addresses. |
| M-F09 | Linker `_Min_Stack_Size=0x400`, or linked `_estack-_sstack` is below `0x1000` | Linker and linked-image stack checks fail. |
| M-F10 | `.ioc` stack is `0x400` while linker is `0x1000`, or values differ while both exceed 4 KiB | `.ioc` minimum and linker/`.ioc` equality checks fail independently. |
| M-F11 | Retain combined `RAM_D2` length 32 KiB, route descriptors to `RAM_D2`, or create an overlapping 32-KiB alias | Split-region and descriptor-region checks fail; the old broad D2 check is not accepted. |
| M-F12 | SRAM1 origin/length is wrong; SRAM2 starts at `0x30003FFF`, has length 32 KiB, overlaps SRAM1, or is omitted | Exact origins, lengths, adjacency, and non-overlap checks fail. |
| M-F13 | Add a packet-buffer or other allocated section in SRAM2 | M-P09 fails in both map/ELF fixtures even though the future region declaration itself is valid. |
| M-F14 | Map reports the right start but ELF symbol size/VMA differs, or RX/TX symbols are missing | Cross-artifact consistency and missing-symbol checks fail. |
| M-F15 | Remove linker `LINK_DEPENDS` or use a stale ELF/map from the other configuration | Dependency/configuration freshness test fails. |
| M-F16 | SRAM1 enable only in `main.c`; in another function; in `HAL_ETH_MspInit` outside `if (heth->Instance == ETH)` (before it, after it, or in a different instance branch); comment-only; or the ETH instance branch is missing | M-P01 fails with a diagnostic naming the missing or misplaced enable. |

Where an exact-address rule masks an arithmetic rule (for example, M-F07), test the underlying extent validator directly with synthetic intervals in addition to the integration fixture. Do not weaken the exact production expectations to make negative fixtures pass.

## 5. Acceptance evidence required

Attach or archive the following with the PR 2 result; this document itself supplies none of these results.

- Source inspection showing the SRAM1 enable in `HAL_ETH_MspInit` as inherited from `49d6ea0` (PR #22). It is **not** part of this PR's diff, so no focused MSP diff is requested; the stack values are likewise inherited. Provide a focused diff of the linker split and descriptor symbols/sections; confirm there is no redundant `main.c` SRAM1 enable and no unrelated timing, ECC, cache, option-byte, actuator, or SRAM2 packet-buffer change.
- Positive-check output for M-P01–M-P11 and a fixture matrix for M-F01–M-F15 showing expected nonzero outcomes and diagnostics.
- Instrumented event trace demonstrating SRAM1 enable before the first TX and first RX descriptor writes for each supported HAL callback configuration.
- Fresh Debug and Release configure/build logs with tool versions, source revision, warning review, text/data/bss sizes, region usage, and generated `.su` files. State output paths and ensure artifacts correspond to their named configuration.
- For each configuration, `readelf`/`nm`/map evidence giving both descriptor addresses, exact 96-byte sizes, section VMAs, alignment, non-overlap, SRAM1 bounds, `_estack`, `_sstack`, derived stack reservation, and an empty SRAM2 allocation range.
- Linker-dependency rebuild evidence and CubeMX-output review showing regeneration will not silently restore the 1-KiB stack or remove the user-code SRAM1 enable.
- A separate hardware result record if §7 is performed. Static/build success and bench success are different evidence levels and shall not be combined into one unqualified “memory verified” claim.

## 6. Verification limits

**Static source, `.ioc`, ELF, and map tests cannot prove actual runtime execution order or hardware operation.** They cannot show that reset/default clock state was benign, that the intended callback ran on target, that SRAM was electrically accessible, that descriptors were written without a bus fault, or that Ethernet DMA/PHY communication works. An instrumented fake proves the modeled sequence, not target runtime order. Conversely, a successful bench run does not replace negative fixtures, exact map checks, or Debug/Release builds.

The M-P01 check, including its branch-placement rule, is source inspection of `stm32h7xx_hal_msp.c` only; it is neither M-P02 runtime-order evidence nor hardware evidence.

PR 2 therefore closes only the source/configuration and measured items for which evidence is actually supplied. SRAM2 buffers, link/ICMP/UDP behavior, cache coherency, ECC/option-byte claims, and robot-level safety remain open.

## 7. Small non-actuating bench checklist

Use only an approved board logic/debug supply and debugger. Do not connect or enable motors, lift/actuator power, contactors, charger/charging hardware, or other actuated loads; do not issue actuator or charging commands. This is a memory/initialization smoke test, not a power, charging, PHY, or network test.

- [ ] Record board revision, debugger, firmware revision/configuration, and the physically disconnected/no-actuator setup. Confirm D-cache remains disabled.
- [ ] Reset from a known state. Use a debugger watchpoint/trace on the first descriptor write (TX starts at `0x30000000` + `0x60`; RX at `0x30000000`) and halt at that event. Confirm `RCC->AHB2ENR` SRAM1 enable (`D2SRAM1EN`, bit 29) is already set; save the trace, call stack, and register value. A breakpoint placed after descriptor initialization is not order evidence.
- [ ] Halt after `MX_ETH_Init` and read back both complete 96-byte descriptor ranges at the exact addresses. Confirm mapped access does not fault and record initialization results; do not infer DMA/link operation from readable memory.
- [ ] Inspect the map against the target addresses and confirm no SRAM2 object is accessed. Do not write test buffers into reserved SRAM2.
- [ ] In Debug, inspect the initialized stack-watermark bounds and record the observed high-water mark after initialization/idle. Do not treat one idle observation as proof of worst-case stack sufficiency.
- [ ] Repeat reset/initialization enough times to catch an intermittent obvious failure, recording reset/fault status and any HardFault precisely. Report all failures; do not convert a PHY/clock-board uncertainty into a memory-test pass.

Any target runtime result shall identify what was observed and what remains unverified. No bench step permits actuator motion, actuator-power validation, charging, or a robot-level safety claim.
