cmake_minimum_required(VERSION 3.20)
project(redis_like VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------- compile options ----------
if(MSVC)
    add_compile_options(/W4 /utf-8)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# ---------- sources ----------
set(SRC_DIR ${CMAKE_SOURCE_DIR}/src)

set(SOURCES
    ${SRC_DIR}/main.cpp
    ${SRC_DIR}/server.cpp
    ${SRC_DIR}/resp_codec.cpp
    ${SRC_DIR}/event_loop.cpp
    ${SRC_DIR}/command/router.cpp
    ${SRC_DIR}/command/string_cmd.cpp
    ${SRC_DIR}/expire.cpp
    ${SRC_DIR}/ds/skiplist.cpp
    ${SRC_DIR}/ds/list.cpp
    ${SRC_DIR}/storage/rdb.cpp
    ${SRC_DIR}/storage/aof.cpp
)

set(HEADERS
    ${SRC_DIR}/common.h
    ${SRC_DIR}/object.h
    ${SRC_DIR}/dict.h
    ${SRC_DIR}/resp_codec.h
    ${SRC_DIR}/event_loop.h
    ${SRC_DIR}/server.h
    ${SRC_DIR}/command/router.h
    ${SRC_DIR}/expire.h
    ${SRC_DIR}/ds/skiplist.h
    ${SRC_DIR}/ds/list.h
    ${SRC_DIR}/ds/intset.h
    ${SRC_DIR}/storage/rdb.h
    ${SRC_DIR}/storage/aof.h
)

# ---------- executable ----------
add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS})

target_include_directories(${PROJECT_NAME} PUBLIC ${SRC_DIR})

# platform-specific
if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE ws2_32)
endif()

# ---------- tests ----------
option(BUILD_TESTS "Build tests" ON)
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
