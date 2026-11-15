# SPDX-License-Identifier: MIT
# Post-link guard of the telemetry image. Fails the build when the linked ELF
# contains a symbol this image must not have:
#  - a heap entry point (the image is static-allocation only);
#  - the SDO download (write) encoder of firmware/canopen (the image is read-only);
# and when a source of this directory names a restricted drive object or an
# NMT/SDO-download encoder.
execute_process(COMMAND "${NM}" "${ELF}" OUTPUT_VARIABLE syms RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "nm failed on ${ELF}")
endif()
set(bad "")
foreach(s malloc calloc realloc free _malloc_r _calloc_r _realloc_r _free_r _sbrk _sbrk_r
          pvPortMalloc vPortFree oamr_co_sdo_encode_download_expedited)
  if(syms MATCHES "[ \t]${s}\n")
    list(APPEND bad "symbol ${s} is linked")
  endif()
endforeach()
file(GLOB SRCS "${SRC_DIR}/src/*.c" "${SRC_DIR}/src/*.h" "${SRC_DIR}/include/*.h")
foreach(f ${SRCS})
  file(READ "${f}" txt)
  string(TOLOWER "${txt}" low)
  string(REGEX MATCH "0x(6040|60ff|2000|1017|2010|6083|6084|6085|2015)u?[^0-9a-f]" hit "${low}")
  if(hit)
    list(APPEND bad "${f}: restricted object literal ${hit}")
  endif()
  if(low MATCHES "encode_download|nmt_command_encode|iwdg|wwdg")
    list(APPEND bad "${f}: names a write encoder or a watchdog peripheral")
  endif()
endforeach()
if(bad)
  string(REPLACE ";" "\n  " bad "${bad}")
  message(FATAL_ERROR "telemetry image guard failed:\n  ${bad}")
endif()
message(STATUS "telemetry image guard: no heap symbol, no SDO download encoder, no restricted object")
