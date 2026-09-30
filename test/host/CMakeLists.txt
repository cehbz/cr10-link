cmake_minimum_required(VERSION 3.22)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Pure C++ logic lives in the app's components/ and is tested here.
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/../../components")
    list(APPEND EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../components")
endif()
set(COMPONENTS main)
project(cr10_link_host_test)
