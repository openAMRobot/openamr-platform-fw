# PR 4 sensor driver verification and evidence limits

**Scope:** MB7060 and TF-Luna communication, parsing, scheduling and buffering. IMU is PR 12.

## Source basis

- Design §3.1–3.2: four MB7060s on inverted 9600 8N1 UART RX, sequential acquisition; two TF-Lunas on USART2/USART6 115200 8N1 circular DMA with independent parser state, checksum validation and stale-data tracking.
- MaxBotix MB7060/MB7070 datasheet, copy retrieved from https://vctec.co.kr/web/product/maxbotix/documents/MB7060-MB7070_Datasheet.pdf (vendor-document mirror): trigger high for at least 20 µs and serial `R` plus range digits plus carriage return. The original MaxBotix PDF URL redirected to a product page; exact board-level timing still requires confirmation against the selected sensor/datasheet revision.
- Benewake TF-Luna SJ-PM-TF-Luna A05 product-manual frame definition: nine bytes `0x59 0x59 Dist_L Dist_H Amp_L Amp_H Temp_L Temp_H Check_sum`, low-eight-bit sum of bytes 0–7. Official product information also confirms 115200 8N1 and 3.3 V LVTTL; source: https://ai.benewake.com/en/resources/technical-documents/tf-luna-datasheet and https://en.benewake.com/uploadfiles/2025/04/20250430174501874.pdf.

These sources establish byte framing, not actual delivered-unit configuration or measurement correctness.

## Implementation policy

1. MB7060 parser accepts only `R` followed by one to three ASCII digits and CR. Raw numeric range 0–999 is syntactically accepted without classifying no-target, minimum range, obstacle presence or safety. One well-formed frame is accepted per armed trigger; unsolicited and duplicate frames are not new samples.
2. Scheduler permits one outstanding sensor. Reading timeout and inter-sensor gap are explicit caller settings, not hidden defaults. A late/wrong-sensor frame cannot satisfy a different trigger. Clock regression is not elapsed time.
3. TF-Luna requires explicit CM or MM unit configuration. The parser validates frame length/header/checksum and exposes raw distance, converted millimetres, strength and raw temperature representation. No signal-strength, no-return, blind-zone or overexposure threshold is turned into an approved measurement-health/safety policy here.
4. Freshness remains a separate caller-limit operation using PR 3. No default stale timeout is selected.
5. USART2/USART6 DMA is circular and independently owned. DMA IRQ handlers notify only; parsing and application processing occur outside IRQ. Buffers are in DMA-accessible SRAM, never DTCM. No dynamic allocation.
6. No sensor output controls motion, charging, lift, brakes, contactors, watchdog or safety state.

## Required evidence

- MB7060 valid/split/malformed/incomplete/extra-digit/non-digit frames, partial-frame flushing, duplicate/late/unsolicited rejection and independent counters.
- Scheduler invalid configuration, one-active-sensor proof, exact timeout, gap, wrong sensor, late data, clock regression and 64-bit time above 32 bits.
- TF-Luna golden CM/MM frames, little-endian fields, checksum failure, noise, truncation, overlapping headers and independent parser states.
- DMA cursor half/full/idle/wrap boundaries, no duplicate delivery, invalid/backward cursor and overrun/gap recovery.
- cppcheck, strict host warnings, ASan/UBSan and separate no-sanitizer run.
- Fresh Debug and Release builds, map inspection proving DMA buffers outside DTCM and no overlap with descriptors/SRAM2 reservation.

## Outstanding configuration and bench work

- Approved MB7060 response timeout, acoustic gap, trigger pulse implementation and final pin map.
- Delivered TF-Luna CM/MM setting, output rate, measurement-strength/validity policy and stale timeout.
- Actual UART signal levels/inversion, 3.3 V MB7060 supply and PWR-032 margin, TF-Luna supply and 3.3 V logic, acoustic cross-talk.
- Sustained DMA reception with half/full/idle events, induced UART errors/overruns, stale recovery and sensor values checked against physical references.

Host/static results cannot close these Level 4 items.
