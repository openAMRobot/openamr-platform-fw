# Post-link guard for board images: the build fails if any symbol whose name starts with
# simref_ (the SIMULATION ONLY reference of tests/sim_reference) is in the ELF, defined or
# undefined. It only reads the ELF. A missing ELF or nm also fails.
# Usage: cmake -DELF=<image.elf> -DNM=<nm> -P simref_symbol_guard.cmake
if(NOT DEFINED ELF OR NOT EXISTS "${ELF}")
  message(FATAL_ERROR "simref symbol guard: ELF not found: '${ELF}'")
endif()
if(NOT DEFINED NM OR "${NM}" STREQUAL "" OR "${NM}" MATCHES "NOTFOUND")
  message(FATAL_ERROR "simref symbol guard: no nm given")
endif()
execute_process(COMMAND "${NM}" -a "${ELF}" RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "simref symbol guard: ${NM} failed on ${ELF}: ${err}")
endif()
string(REGEX MATCHALL "[^\n]*[ \t]simref_[A-Za-z0-9_]*" hits "${out}")
if(hits)
  list(LENGTH hits n)
  string(REPLACE ";" "\n" shown "${hits}")
  message(FATAL_ERROR "simref symbol guard: ${ELF} contains ${n} simulation-reference symbol(s) "
                      "(prefix simref_). The SIMULATION ONLY reference must never be linked into a board image:\n${shown}")
endif()
message(STATUS "simref symbol guard: ${ELF}: no simref_ symbol")
