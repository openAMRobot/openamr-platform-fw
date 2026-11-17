# SIMULATION ONLY. Not production firmware. Not reviewed for safety. Must not be linked into any board image.
# Negative test of the map check (SAF-MCU-001), injected from the command line only:
#   cmake --preset Release -DCMAKE_PROJECT_OpenAMRobot_INCLUDE=<this file>
# Adds poison_unreferenced.c to the board executable with no reference to it, so the linker
# discards its sections. The ELF symbol guard may pass; the map check must fail.
# No board file references this file.
set(SIMREF_UNREFERENCED_SOURCE "${CMAKE_CURRENT_LIST_DIR}/poison_unreferenced.c")
cmake_language(DEFER DIRECTORY ${CMAKE_SOURCE_DIR} CALL target_sources OpenAMRobot PRIVATE ${SIMREF_UNREFERENCED_SOURCE})
