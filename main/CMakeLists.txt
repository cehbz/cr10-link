if(NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/credentials.h")
    message(FATAL_ERROR "main/credentials.h missing: copy credentials.h.template to main/credentials.h and fill it in")
endif()

idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "."
)

target_compile_options(${COMPONENT_LIB} PRIVATE -std=gnu++2b)
