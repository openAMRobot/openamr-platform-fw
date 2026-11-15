# SIMULATION ONLY. Not production firmware. Not reviewed for safety. Must not be linked into any board image.
# The process must exit with code 3 for every interface name that does not start with vcan.
foreach(pair IN ITEMS "can0;vcan_aux" "vcan_drive;can1" "slcan0;vcan_x" "VCAN0;vcan_x" "xvcan;vcan_x" "eth0;vcan_x")
  list(GET pair 0 a)
  list(GET pair 1 b)
  execute_process(COMMAND ${PROC} --can-drive ${a} --can-aux ${b} --sock /tmp/simref_refuse_${a}.sock
                  RESULT_VARIABLE rc ERROR_VARIABLE err OUTPUT_QUIET)
  if(NOT rc EQUAL 3)
    message(FATAL_ERROR "interface pair ${a} ${b}: expected exit 3 (refused), got ${rc}: ${err}")
  endif()
  if(NOT err MATCHES "refused interface")
    message(FATAL_ERROR "interface pair ${a} ${b}: no refusal message: ${err}")
  endif()
  message(STATUS "refused ${a} ${b}: ok")
endforeach()
# Positive control: vcan names pass the name check (opening may then fail with code 4 when the
# interface does not exist; it must not be 3).
execute_process(COMMAND ${PROC} --can-drive vcan_none_a --can-aux vcan_none_b --sock /tmp/simref_refuse_ok.sock
                RESULT_VARIABLE rc ERROR_VARIABLE err OUTPUT_QUIET TIMEOUT 2)
if(rc EQUAL 3)
  message(FATAL_ERROR "vcan names were refused: ${err}")
endif()
