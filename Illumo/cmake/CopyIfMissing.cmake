if(NOT DEFINED SOURCE OR NOT DEFINED DESTINATION OR NOT DEFINED LOCK_FILE)
    message(FATAL_ERROR "CopyIfMissing.cmake requires SOURCE, DESTINATION and LOCK_FILE")
endif()

# Products can share a runtime directory. Keep the existence check and copy
# atomic with respect to other seed stages without overwriting user settings.
get_filename_component(lockDirectory "${LOCK_FILE}" DIRECTORY)
file(MAKE_DIRECTORY "${lockDirectory}")
file(LOCK "${LOCK_FILE}" GUARD PROCESS TIMEOUT 60)

if(NOT EXISTS "${DESTINATION}")
    get_filename_component(destinationDirectory "${DESTINATION}" DIRECTORY)
    file(MAKE_DIRECTORY "${destinationDirectory}")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E copy "${SOURCE}" "${DESTINATION}"
        COMMAND_ERROR_IS_FATAL ANY
    )
endif()
