idf_component_register(
    SRCS "src/marlin_line.cpp" "src/marlin_protocol.cpp"
    INCLUDE_DIRS "include"
)

target_compile_options(${COMPONENT_LIB} PRIVATE -std=gnu++2b)
