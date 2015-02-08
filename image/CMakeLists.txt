
set (IMAGE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
add_custom_command(
    OUTPUT  ${IMAGE_DIR}/LittleSmalltalk.image
    COMMAND ${IMAGE_DIR}/imageBuilder
    DEPENDS ${IMAGE_DIR}/imageSource.st
    WORKING_DIRECTORY ${IMAGE_DIR}
    COMMENT "Building image"
)
add_custom_target(image ALL DEPENDS ${IMAGE_DIR}/LittleSmalltalk.image)
