cmake_minimum_required(VERSION 3.31)
project(URingWebServer)

set(CMAKE_CXX_STANDARD 23)

set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

if(UNIX)
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fsanitize=undefined")
    # address sanitizer and thread sanitizer are incompatible and can't be enabled together
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fsanitize=address")
    # set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fsanitize=thread")
endif()

set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g3 -D_DEBUG")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")

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
