# SIMULATION ONLY. Not production firmware. Not reviewed for safety. Must not be linked into any board image.
cmake_minimum_required(VERSION 3.22)

# Refuse every configuration that could produce code for the microcontroller. This runs
# before project() enables a language and again after it, so a toolchain file that sets
# CMAKE_SYSTEM_PROCESSOR or CMAKE_CROSSCOMPILING is caught at configure time.
function(simref_refuse_target)
  if(CMAKE_CROSSCOMPILING)
    message(FATAL_ERROR "SIMULATION ONLY: tests/sim_reference refuses to configure when cross-compiling "
                        "(CMAKE_SYSTEM_NAME=${CMAKE_SYSTEM_NAME}).")
  endif()
  foreach(p IN ITEMS "${CMAKE_SYSTEM_PROCESSOR}" "${CMAKE_HOST_SYSTEM_PROCESSOR}")
    string(TOLOWER "${p}" lp)
    if(lp MATCHES "^(arm|aarch64|armv[0-9].*|cortex.*|thumb.*)")
      message(FATAL_ERROR "SIMULATION ONLY: tests/sim_reference refuses to configure for an ARM processor (${p}).")
    endif()
  endforeach()
  if(CMAKE_C_COMPILER MATCHES "arm-none-eabi|aarch64|arm-linux")
    message(FATAL_ERROR "SIMULATION ONLY: tests/sim_reference refuses the ARM compiler ${CMAKE_C_COMPILER}.")
  endif()
endfunction()
if(CMAKE_TOOLCHAIN_FILE)
  message(FATAL_ERROR "SIMULATION ONLY: tests/sim_reference refuses a toolchain file (${CMAKE_TOOLCHAIN_FILE}).")
endif()
simref_refuse_target()

project(simref_simulation_reference C)
simref_refuse_target()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(SIMREF_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" ON)
set(SIMREF_OPTION_DEFINES "" CACHE STRING "Extra -D options for the interpretation options (compile check of non-default readings)")

set(SIMREF_WARN -Wall -Wextra -Werror -Wpedantic -Wconversion -Wshadow -Wstrict-prototypes
    -Wmissing-prototypes -Wformat=2 -Wundef)
set(SIMREF_SAN "")
if(SIMREF_SANITIZE)
  set(SIMREF_SAN -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
endif()

set(SIMREF_LIB_SOURCES src/simref_sm.c src/simref_permit.c src/simref_drive.c src/simref_ctrl.c)

add_library(simref STATIC ${SIMREF_LIB_SOURCES})
target_include_directories(simref PUBLIC include)
target_compile_options(simref PRIVATE ${SIMREF_WARN} ${SIMREF_SAN})
target_compile_definitions(simref PUBLIC ${SIMREF_OPTION_DEFINES})
target_link_options(simref PUBLIC ${SIMREF_SAN})
target_link_libraries(simref PUBLIC m)

# Shared library for the ctypes adapter (no sanitizers: loaded into the Python process).
add_library(simref_adapter SHARED ${SIMREF_LIB_SOURCES} adapter/simref_adapter_shim.c)
target_include_directories(simref_adapter PUBLIC include)
target_compile_options(simref_adapter PRIVATE ${SIMREF_WARN} -Wno-missing-prototypes)
target_compile_definitions(simref_adapter PUBLIC ${SIMREF_OPTION_DEFINES})
target_link_libraries(simref_adapter PUBLIC m)

add_executable(simref_process src/simref_main.c)
target_compile_options(simref_process PRIVATE ${SIMREF_WARN} ${SIMREF_SAN})
target_compile_definitions(simref_process PRIVATE _GNU_SOURCE)
target_link_libraries(simref_process PRIVATE simref)

enable_testing()
foreach(t IN ITEMS test_sm test_watchdog test_drive test_permit test_ctrl)
  add_executable(${t} test/${t}.c test/tharness.c)
  target_compile_options(${t} PRIVATE ${SIMREF_WARN} ${SIMREF_SAN})
  target_link_libraries(${t} PRIVATE simref)
  add_test(NAME ${t} COMMAND ${t})
endforeach()

# The process refuses any interface that is not vcan, before it opens a socket.
add_test(NAME process_refuses_non_vcan
         COMMAND ${CMAKE_COMMAND} -DPROC=$<TARGET_FILE:simref_process> -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/test_refuse_iface.cmake)
# The project refuses to configure for ARM or when cross-compiling.
add_test(NAME configure_refuses_arm
         COMMAND ${CMAKE_COMMAND} -DSRC=${CMAKE_CURRENT_SOURCE_DIR} -DWORK=${CMAKE_CURRENT_BINARY_DIR}/refuse_test
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/test_refuse_configure.cmake)

# Adapter (docs/safety/spec/adapter.md): every vector on the Python model and the C library.
find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_test(NAME adapter_vectors
         COMMAND ${Python3_EXECUTABLE} -m pytest -q -p no:cacheprovider ${CMAKE_CURRENT_SOURCE_DIR}/adapter
                 --junitxml=${CMAKE_CURRENT_BINARY_DIR}/adapter.xml)
set_tests_properties(adapter_vectors PROPERTIES ENVIRONMENT "SIMREF_ADAPTER_LIB=$<TARGET_FILE:simref_adapter>")
# No file under firmware/ or boards/ includes or links the reference.
add_test(NAME boards_do_not_use_simref
         COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_no_simref_use.py ${CMAKE_CURRENT_SOURCE_DIR}/../..)
# Post-link guards of the board targets, on host ELFs (the board builds run in CI).
add_test(NAME board_symbol_guards
         COMMAND ${Python3_EXECUTABLE} -m pytest -q -p no:cacheprovider ${CMAKE_CURRENT_SOURCE_DIR}/guard_test
                 --junitxml=${CMAKE_CURRENT_BINARY_DIR}/guards.xml)
