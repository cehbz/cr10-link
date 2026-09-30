idf_component_register(
    SRCS "test_main.cpp" "test_marlin_line.cpp" "test_marlin_protocol.cpp"
    REQUIRES unity marlin
)

target_compile_options(${COMPONENT_LIB} PRIVATE -std=gnu++2b)
