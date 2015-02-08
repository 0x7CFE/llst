set (DOC_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set (POD_LLST_EN ${DOC_DIR}/llst.1.en.pod)
set (MAN_LLST_EN ${CMAKE_CURRENT_BINARY_DIR}/llst.1.en)
set (MAN_LLST_EN_GZ ${CMAKE_CURRENT_BINARY_DIR}/llst.1.en.gz)

get_pod2man_cmd(MAN_LLST_EN_CMD FROM ${POD_LLST_EN} TO ${MAN_LLST_EN}
    SECTION 1 CENTER "LLST Documentation"
    RELEASE 0.2.1
    NAME llst
)
add_custom_command(
    OUTPUT ${MAN_LLST_EN}
    COMMAND ${MAN_LLST_EN_CMD}
    DEPENDS ${POD_LLST_EN}
)

add_custom_target(doc ALL DEPENDS ${MAN_LLST_EN})
gzip_compress("compress_man_llst_en" ${MAN_LLST_EN} ${MAN_LLST_EN_GZ} doc)

install(FILES ${MAN_LLST_EN_GZ} DESTINATION share/man/man1/ RENAME llst.1.gz)
