# SIMULATION ONLY. Not production firmware. Not reviewed for safety. Must not be linked into any board image.
# Negative test of the STM32 post-link guard, injected from the command line only:
#   cmake --preset Debug -DCMAKE_PROJECT_OpenAMRobot_INCLUDE=<this file>
# At the end of the board's top-level CMakeLists.txt it adds poison.c to the board executable
# and keeps its symbol through --gc-sections. The board build must then FAIL in the guard.
# No board file references this file.
set(SIMREF_POISON_SOURCE "${CMAKE_CURRENT_LIST_DIR}/poison.c")
cmake_language(DEFER DIRECTORY ${CMAKE_SOURCE_DIR} CALL target_sources OpenAMRobot PRIVATE ${SIMREF_POISON_SOURCE})
cmake_language(DEFER DIRECTORY ${CMAKE_SOURCE_DIR} CALL target_link_options OpenAMRobot PRIVATE -Wl,--undefined=simref_poison_marker)
