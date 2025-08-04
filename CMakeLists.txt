cmake_minimum_required(VERSION 3.31)
project(URingWebServer)

set(CMAKE_CXX_STANDARD 23)

add_executable(URingWebServer main.cpp)

set(LIBURING_INCLUDE_DIR /usr/include)
if(NOT EXISTS ${LIBURING_INCLUDE_DIR}/liburing.h)
    message(FATAL_ERROR "liburing.h not found in ${LIBURING_INCLUDE_DIR}. Please install liburing-dev.")
endif()

set(LIBURING_LIBRARY /usr/lib/x86_64-linux-gnu/liburing.so)
if(NOT EXISTS ${LIBURING_LIBRARY})
    message(FATAL_ERROR "liburing not found at ${LIBURING_LIBRARY}. Please install liburing-dev.")
endif()

target_link_libraries(URingWebServer PRIVATE ${LIBURING_LIBRARY})
