# SIMULATION ONLY. Not production firmware. Not reviewed for safety. Must not be linked into any board image.
# Negative test of the compile-time guard (SAF-MCU-001), injected from the command line only:
#   cmake --preset Debug -DCMAKE_PROJECT_OpenAMRobot_INCLUDE=<this file>
# Adds a real reference source (src/simref_sm.c) and its include directory to the board
# executable. The board build must FAIL at compile time (include/simref/simref_host_only.h).
# No board file references this file.
get_filename_component(SIMREF_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
cmake_language(DEFER DIRECTORY ${CMAKE_SOURCE_DIR} CALL target_sources OpenAMRobot PRIVATE ${SIMREF_ROOT}/src/simref_sm.c)
cmake_language(DEFER DIRECTORY ${CMAKE_SOURCE_DIR} CALL target_include_directories OpenAMRobot PRIVATE ${SIMREF_ROOT}/include)
