# SIMULATION ONLY. Not production firmware. Not reviewed for safety. Must not be linked into any board image.
# Configure the project three ways that would target the microcontroller; each must fail.
file(REMOVE_RECURSE ${WORK})
file(MAKE_DIRECTORY ${WORK})
file(WRITE ${WORK}/arm_toolchain.cmake "set(CMAKE_SYSTEM_NAME Generic)\nset(CMAKE_SYSTEM_PROCESSOR arm)\nset(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)\n")
set(cases
  "toolchain|-DCMAKE_TOOLCHAIN_FILE=${WORK}/arm_toolchain.cmake"
  "processor|-DCMAKE_SYSTEM_PROCESSOR=cortex-m7"
  "crosscompiling|-DCMAKE_SYSTEM_NAME=Generic")
foreach(c IN LISTS cases)
  string(REPLACE "|" ";" parts "${c}")
  list(GET parts 0 name)
  list(GET parts 1 arg)
  execute_process(COMMAND ${CMAKE_COMMAND} -S ${SRC} -B ${WORK}/${name} ${arg}
                  RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
  if(rc EQUAL 0)
    message(FATAL_ERROR "configure with ${arg} succeeded; it must fail")
  endif()
  if(NOT "${out}${err}" MATCHES "SIMULATION ONLY: tests/sim_reference refuses")
    message(FATAL_ERROR "configure with ${arg} failed for another reason:\n${out}\n${err}")
  endif()
  message(STATUS "${name}: refused as required")
endforeach()
