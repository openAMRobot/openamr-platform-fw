# Static guard for the read-only CAN1 package (platform-fw #47 boundary).
# Fails if a production source under firmware/canopen or firmware/drive_telemetry
#  - names a restricted drive object as a hex literal (6040h, 60FFh, 2000h,
#    1017h, 2010h, 6083h to 6085h, 2015h), or
#  - calls the SDO download encoder anywhere except its own definition.
file(GLOB_RECURSE SRCS
  ${REPO_ROOT}/firmware/canopen/*.c ${REPO_ROOT}/firmware/canopen/*.h
  ${REPO_ROOT}/firmware/drive_telemetry/*.c ${REPO_ROOT}/firmware/drive_telemetry/*.h)
set(bad "")
foreach(f ${SRCS})
  file(READ ${f} txt)
  string(TOLOWER "${txt}" low)
  string(REGEX MATCH "0x(6040|60ff|2000|1017|2010|6083|6084|6085|2015)u?[^0-9a-f]" hit "${low}")
  if(hit)
    list(APPEND bad "${f}: restricted object literal ${hit}")
  endif()
  get_filename_component(name ${f} NAME)
  if(NOT name STREQUAL "co_sdo.c" AND NOT name STREQUAL "co_sdo.h")
    string(FIND "${txt}" "oamr_co_sdo_encode_download_expedited" pos)
    if(NOT pos EQUAL -1)
      list(APPEND bad "${f}: calls the SDO download encoder")
    endif()
  endif()
endforeach()
list(LENGTH SRCS n)
if(bad)
  message(FATAL_ERROR "read-only guard failed:\n${bad}")
endif()
message(STATUS "read-only guard: ${n} files clean")
