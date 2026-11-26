file(GLOB NOFRENDO_SRC
    "${CMAKE_CURRENT_LIST_DIR}/*.c"
    "${CMAKE_CURRENT_LIST_DIR}/cpu/*.c"
    "${CMAKE_CURRENT_LIST_DIR}/nes/*.c"
    "${CMAKE_CURRENT_LIST_DIR}/mappers/*.c"
    "${CMAKE_CURRENT_LIST_DIR}/sndhrdw/*.c"
    "${CMAKE_CURRENT_LIST_DIR}/libsnss/*.c"
)
idf_component_register(
    SRCS ${NOFRENDO_SRC}
    INCLUDE_DIRS "." "cpu" "nes" "mappers" "sndhrdw" "libsnss"
    REQUIRES esp_timer
)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-error -Wno-attributes -Wno-stringop-truncation)
