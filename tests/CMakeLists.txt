cmake_minimum_required(VERSION 3.20)

# simple header-only test harness — no external dependency
set(TEST_SRC_DIR ${CMAKE_SOURCE_DIR}/src)

add_executable(test_resp
    test_resp.cpp
    ${TEST_SRC_DIR}/resp_codec.cpp
)

target_include_directories(test_resp PUBLIC ${TEST_SRC_DIR})

target_compile_options(test_resp PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra>
)

add_test(NAME resp_codec COMMAND test_resp)

add_executable(test_string_cmd
    test_string_cmd.cpp
    ${TEST_SRC_DIR}/command/string_cmd.cpp
    ${TEST_SRC_DIR}/command/router.cpp
    ${TEST_SRC_DIR}/expire.cpp
    ${TEST_SRC_DIR}/resp_codec.cpp
)

target_include_directories(test_string_cmd PUBLIC ${TEST_SRC_DIR})

target_compile_options(test_string_cmd PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra>
)

add_test(NAME string_cmd COMMAND test_string_cmd)

add_executable(test_hash_cmd
    test_hash_cmd.cpp
    ${TEST_SRC_DIR}/command/hash_cmd.cpp
    ${TEST_SRC_DIR}/command/string_cmd.cpp
    ${TEST_SRC_DIR}/command/router.cpp
    ${TEST_SRC_DIR}/expire.cpp
    ${TEST_SRC_DIR}/resp_codec.cpp
)

target_include_directories(test_hash_cmd PUBLIC ${TEST_SRC_DIR})

target_compile_options(test_hash_cmd PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra>
)

add_test(NAME hash_cmd COMMAND test_hash_cmd)

add_executable(test_list_cmd
    test_list_cmd.cpp
    ${TEST_SRC_DIR}/command/list_cmd.cpp
    ${TEST_SRC_DIR}/command/string_cmd.cpp
    ${TEST_SRC_DIR}/command/router.cpp
    ${TEST_SRC_DIR}/expire.cpp
    ${TEST_SRC_DIR}/resp_codec.cpp
    ${TEST_SRC_DIR}/ds/list.cpp
)

target_include_directories(test_list_cmd PUBLIC ${TEST_SRC_DIR})

target_compile_options(test_list_cmd PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra>
)

add_test(NAME list_cmd COMMAND test_list_cmd)

add_executable(test_set_cmd
    test_set_cmd.cpp
    ${TEST_SRC_DIR}/command/set_cmd.cpp
    ${TEST_SRC_DIR}/command/string_cmd.cpp
    ${TEST_SRC_DIR}/command/router.cpp
    ${TEST_SRC_DIR}/expire.cpp
    ${TEST_SRC_DIR}/resp_codec.cpp
)

target_include_directories(test_set_cmd PUBLIC ${TEST_SRC_DIR})

target_compile_options(test_set_cmd PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra>
)

add_test(NAME set_cmd COMMAND test_set_cmd)

add_executable(test_zset_cmd
    test_zset_cmd.cpp
    ${TEST_SRC_DIR}/command/zset_cmd.cpp
    ${TEST_SRC_DIR}/command/string_cmd.cpp
    ${TEST_SRC_DIR}/command/router.cpp
    ${TEST_SRC_DIR}/expire.cpp
    ${TEST_SRC_DIR}/resp_codec.cpp
    ${TEST_SRC_DIR}/ds/skiplist.cpp
)

target_include_directories(test_zset_cmd PUBLIC ${TEST_SRC_DIR})

target_compile_options(test_zset_cmd PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra>
)

add_test(NAME zset_cmd COMMAND test_zset_cmd)

add_executable(test_expire
    test_expire.cpp
    ${TEST_SRC_DIR}/expire.cpp
)

target_include_directories(test_expire PUBLIC ${TEST_SRC_DIR})

target_compile_options(test_expire PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra>
)

add_test(NAME expire COMMAND test_expire)

add_executable(test_persistence
    test_persistence.cpp
    ${TEST_SRC_DIR}/storage/rdb.cpp
    ${TEST_SRC_DIR}/storage/aof.cpp
    ${TEST_SRC_DIR}/ds/skiplist.cpp
    ${TEST_SRC_DIR}/command/string_cmd.cpp
    ${TEST_SRC_DIR}/command/hash_cmd.cpp
    ${TEST_SRC_DIR}/command/router.cpp
    ${TEST_SRC_DIR}/expire.cpp
    ${TEST_SRC_DIR}/resp_codec.cpp
)

target_include_directories(test_persistence PUBLIC ${TEST_SRC_DIR})

target_compile_options(test_persistence PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra>
)

add_test(NAME persistence COMMAND test_persistence)
