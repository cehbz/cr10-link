idf_component_register(
    SRCS "test_main.cpp"
    REQUIRES unity
)

target_compile_options(${COMPONENT_LIB} PRIVATE -std=gnu++2b)
