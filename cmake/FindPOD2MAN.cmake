
# Find pod2man (convert POD data to formatted *roff input)
# Export variables:
#  POD2MAN_FOUND
#  POD2MAN_EXE
#  get_pod2man_cmd_wrapper(<RESULT_CMD>
#       FROM <podfile>
#       TO <manfile>
#       [SECTION <section>]
#       [CENTER <center>]
#       [RELEASE <release>]
#       [NAME <name>]
#       [STUB]
#  )
#

include(CMakeParseArguments)
include(FindPackageHandleStandardArgs)

find_program(POD2MAN_EXE pod2man)

FIND_PACKAGE_HANDLE_STANDARD_ARGS( pod2man DEFAULT_MSG POD2MAN_EXE )
mark_as_advanced(POD2MAN_EXE)

function(get_pod2man_cmd_wrapper RESULT_CMD)
    set(options STUB)
    set(oneValueArgs FROM TO SECTION CENTER RELEASE NAME)
    set(multiValueArgs)
    CMAKE_PARSE_ARGUMENTS(POD_OPTION "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})
    if (NOT POD_OPTION_FROM OR NOT POD_OPTION_TO)
        message(FATAL_ERROR "Arguments FROM and TO are required!")
    endif()

    if (POD_OPTION_STUB)
        # create empty man
        set(TMP_CMD "${CMAKE_COMMAND}" "-E" "touch" "${POD_OPTION_TO}")
    else()
        set(TMP_CMD "${POD2MAN_EXE}")
        if (POD_OPTION_SECTION)
            list(APPEND TMP_CMD "--section=${POD_OPTION_SECTION}")
        endif()
        if (POD_OPTION_CENTER)
            list(APPEND TMP_CMD "--center=\"${POD_OPTION_CENTER}\"")
        endif()
        if (POD_OPTION_RELEASE)
            list(APPEND TMP_CMD "--release=\"${POD_OPTION_RELEASE}\"")
        endif()
        if (POD_OPTION_NAME)
            list(APPEND TMP_CMD "--name=\"${POD_OPTION_NAME}\"")
        endif()

        list(APPEND TMP_CMD "${POD_OPTION_FROM}")
        list(APPEND TMP_CMD "${POD_OPTION_TO}")
    endif()
    set("${RESULT_CMD}" ${TMP_CMD} PARENT_SCOPE)
endfunction()
