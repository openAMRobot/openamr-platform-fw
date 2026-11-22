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

## Size probes (not firmware)

`probe/build_probes.sh` links four measurement images against the board
linker script with the Release CPU flags and writes one map_report table per
image. They use real micro-ROS, LwIP and FreeRTOS code with stub I/O and are
never flashed. Results and their limits are in
`docs/architecture/runtime/size-probes.md`.

```bash
# micro-ROS static library (jazzy) built with the board CPU flags:
#   micro_ros_stm32cubemx_utils/microros_static_library, Docker image
#   microros/micro_ros_static_library_builder:jazzy
export UROS_LIB=.../microros_static_library/libmicroros
export UROS_UTILS=.../micro_ros_stm32cubemx_utils
export LWIP=.../lwip FREERTOS=.../FreeRTOS-Kernel
tools/size/probe/build_probes.sh      # writes build/size-probes/report.md
```

`tests/test_build_probes.py` checks the script; its full run is skipped unless
those four variables are set.
