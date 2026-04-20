cmake_minimum_required(VERSION 3.15)
project(dbms CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include_directories(${CMAKE_SOURCE_DIR}/src)

# ---- 主可执行文件 ----
file(GLOB_RECURSE MAIN_SOURCES "src/*.cpp")
add_executable(dbms ${MAIN_SOURCES})

target_compile_options(dbms PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /utf-8>
)

if(WIN32)
    target_compile_definitions(dbms PRIVATE DBMS_WINDOWS)
endif()

# ---- 单元测试 ----
enable_testing()

# 引擎源文件（不含 main.cpp）
file(GLOB_RECURSE ENGINE_SOURCES "src/engine/*.cpp" "src/cli/*.cpp")

function(add_unit_test name file)
    add_executable(${name} ${file} ${ENGINE_SOURCES})
    target_compile_options(${name} PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra>
        $<$<CXX_COMPILER_ID:MSVC>:/W4 /utf-8>
    )
    if(WIN32)
        target_compile_definitions(${name} PRIVATE DBMS_WINDOWS)
    endif()
    add_test(NAME ${name} COMMAND ${name})
endfunction()

add_unit_test(test_lexer         tests/test_lexer.cpp)
add_unit_test(test_integration   tests/test_integration.cpp)
add_unit_test(test_security      tests/test_security.cpp)
add_unit_test(test_comprehensive tests/test_comprehensive.cpp)
