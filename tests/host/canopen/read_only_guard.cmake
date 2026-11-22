# Static guard for the read-only CAN1 package (platform-fw #47 boundary).
# Fails if
#  - a production source under firmware/canopen or firmware/drive_telemetry
#    names a restricted drive object as a hex literal (6040h, 60FFh, 2000h,
#    1017h, 2010h, 6083h to 6085h, 2015h), or
#  - any of those sources other than the download encoder's own files
#    (co_sdo_download.c, co_sdo_download.h) names the SDO download encoder, or
#  - the read-only library's source list (RO_SOURCES) contains
#    co_sdo_download.c, or
#  - the read-only static archive (RO_ARCHIVE) defines or references the
#    symbol oamr_co_sdo_encode_download_expedited (checked with nm).
# RO_ARCHIVE, RO_SOURCES and NM are passed by CMakeLists.txt; the archive
# check is mandatory, a missing archive or nm fails the guard.
set(SYM oamr_co_sdo_encode_download_expedited)
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
  if(NOT name STREQUAL "co_sdo_download.c" AND NOT name STREQUAL "co_sdo_download.h")
    string(FIND "${txt}" "${SYM}" pos)
    if(NOT pos EQUAL -1)
      list(APPEND bad "${f}: names the SDO download encoder")
    endif()
  endif()
endforeach()
list(LENGTH SRCS n)

if(NOT RO_ARCHIVE OR NOT NM)
  list(APPEND bad "RO_ARCHIVE and NM must be given: the archive check cannot be skipped")
elseif(NOT EXISTS "${RO_ARCHIVE}")
  list(APPEND bad "read-only archive ${RO_ARCHIVE} does not exist (build first)")
else()
  string(REPLACE "|" ";" ro_list "${RO_SOURCES}")
  foreach(s ${ro_list})
    get_filename_component(sname "${s}" NAME)
    if(sname STREQUAL "co_sdo_download.c")
      list(APPEND bad "read-only library lists ${s}")
    endif()
  endforeach()
  execute_process(COMMAND ${NM} -A "${RO_ARCHIVE}" OUTPUT_VARIABLE syms RESULT_VARIABLE rc
                  ERROR_VARIABLE nm_err)
  if(NOT rc EQUAL 0)
    list(APPEND bad "nm failed on ${RO_ARCHIVE}: ${nm_err}")
  elseif(syms STREQUAL "")
    list(APPEND bad "nm found no symbols in ${RO_ARCHIVE}: archive check would be empty")
  else()
    string(REGEX MATCHALL "[^\n]*[ \t][A-Za-z][ \t]+${SYM}\n" sym_hits "${syms}\n")
    if(sym_hits)
      list(APPEND bad "read-only archive contains ${SYM}: ${sym_hits}")
    endif()
  endif()
endif()

if(bad)
  message(FATAL_ERROR "read-only guard failed:\n${bad}")
endif()
message(STATUS "read-only guard: ${n} files clean, ${RO_ARCHIVE} has no ${SYM}")
