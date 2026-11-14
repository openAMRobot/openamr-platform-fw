# tools/size

`map_report.py` prints used bytes per linker region (FLASH, DTCMRAM, RAM_D1,
RAM_D2, RAM_D3, ITCMRAM) and the output sections in each, from any GNU ld
`.map` file. It also prints `_Min_Heap_Size` and `_Min_Stack_Size` when the
map defines them, and with `--top N` the largest objects or archives per
region. Standard library only.

```bash
cd boards/stm32/OpenAMRobot
cmake --preset Release && cmake --build --preset Release
python3 ../../../tools/size/map_report.py --top 10 build/Release/OpenAMRobot.map
python3 ../../../tools/size/map_report.py --format json build/Release/OpenAMRobot.map
```

Counting rule: a section counts in the region of its run address; a section
with contents and a different load address (`.data`) also counts in the load
region. `.bss`, `.tbss` and the heap and stack reservation have no contents
and do not count in FLASH. This matches `ld --print-memory-usage`.

Tests: `python3 -m pytest -q tools/size/tests`. The oracle test links a small
image against the board linker script with arm-none-eabi-gcc and compares
every region with the linker's own table; it is skipped when
arm-none-eabi-gcc is not installed.
