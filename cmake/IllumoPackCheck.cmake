# Packs a staged application with IllumoPack and verifies the archive.
#   cmake -DPACK=<IllumoPack> -DAPP=<apps/name> -DOUT=<file.ilpk> -P IllumoPackCheck.cmake
file(REMOVE "${OUT}")
execute_process(COMMAND "${PACK}" "${APP}" "${OUT}" RESULT_VARIABLE _packed)
if(NOT _packed EQUAL 0)
  message(FATAL_ERROR "IllumoPack could not pack ${APP}")
endif()
execute_process(COMMAND "${PACK}" --verify "${OUT}" RESULT_VARIABLE _verified)
if(NOT _verified EQUAL 0)
  message(FATAL_ERROR "IllumoPack could not verify ${OUT}")
endif()
