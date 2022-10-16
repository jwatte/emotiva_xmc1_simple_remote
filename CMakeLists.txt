cmake_minimum_required(VERSION 3.0.0)
project(emotiva_xmc1_simple_remote VERSION 0.1.0)

add_executable(emotiva_xmc1_simple_remote WIN32 main.cpp)

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)
