cmake_minimum_required(VERSION 3.15)
project(dbms VERSION 1.0.0 LANGUAGES CXX)

# ---- C++ 标准 ----
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---- 默认构建类型 ----
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif()

# ---- 数据目录配置（可通过 -DDBMS_DATA_DIR=<path> 覆盖）----
if(NOT DEFINED DBMS_DATA_DIR)
    set(DBMS_DATA_DIR "${CMAKE_SOURCE_DIR}/data")
endif()

include_directories(${CMAKE_SOURCE_DIR}/src)

# ---- 主可执行文件 ----
file(GLOB_RECURSE MAIN_SOURCES "src/*.cpp")
add_executable(dbms ${MAIN_SOURCES})

target_compile_options(dbms PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /utf-8 /wd4996>
)
target_compile_definitions(dbms PRIVATE
    DBMS_VERSION="${PROJECT_VERSION}"
    $<$<BOOL:${WIN32}>:DBMS_WINDOWS>
    $<$<BOOL:${UNIX}>:DBMS_UNIX>
)

# ---- 安装目标 ----
install(TARGETS dbms RUNTIME DESTINATION bin)
install(DIRECTORY data/ DESTINATION data OPTIONAL)

# ---- 单元测试 ----
option(DBMS_BUILD_TESTS "Build unit tests" ON)

if(DBMS_BUILD_TESTS)
    enable_testing()

    # 引擎源文件（不含 main.cpp）
    file(GLOB_RECURSE ENGINE_SOURCES "src/engine/*.cpp" "src/cli/*.cpp")

    function(add_unit_test name file)
        add_executable(${name} ${file} ${ENGINE_SOURCES})
        target_include_directories(${name} PRIVATE ${CMAKE_SOURCE_DIR}/src)
        target_compile_options(${name} PRIVATE
            $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra>
            $<$<CXX_COMPILER_ID:MSVC>:/W4 /utf-8 /wd4996>
        )
        target_compile_definitions(${name} PRIVATE
            $<$<BOOL:${WIN32}>:DBMS_WINDOWS>
            $<$<BOOL:${UNIX}>:DBMS_UNIX>
        )
        add_test(NAME ${name} COMMAND ${name})
    endfunction()

    add_unit_test(test_lexer         tests/test_lexer.cpp)
    add_unit_test(test_integration   tests/test_integration.cpp)
    add_unit_test(test_security      tests/test_security.cpp)
    add_unit_test(test_comprehensive tests/test_comprehensive.cpp)
    add_unit_test(test_cli           tests/test_cli.cpp)
endif()
