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

# ---- 核心引擎库（CLI / Qt 共用，不依赖界面层）----
file(GLOB_RECURSE ENGINE_SOURCES "src/engine/*.cpp")
add_library(dbms_core STATIC ${ENGINE_SOURCES})
target_include_directories(dbms_core PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_compile_options(dbms_core PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /utf-8 /wd4996>
)
target_compile_definitions(dbms_core PUBLIC
    DBMS_VERSION="${PROJECT_VERSION}"
    $<$<BOOL:${WIN32}>:DBMS_WINDOWS>
    $<$<BOOL:${UNIX}>:DBMS_UNIX>
)

# ---- CLI 可执行文件 ----
file(GLOB CLI_SOURCES "src/cli/*.cpp")
add_executable(dbms src/main.cpp ${CLI_SOURCES})
target_link_libraries(dbms PRIVATE dbms_core)

target_compile_options(dbms PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /utf-8 /wd4996>
)
target_compile_definitions(dbms PRIVATE
    DBMS_VERSION="${PROJECT_VERSION}"
    $<$<BOOL:${WIN32}>:DBMS_WINDOWS>
    $<$<BOOL:${UNIX}>:DBMS_UNIX>
)

# ---- Qt Widgets 前端（可选）----
option(DBMS_BUILD_QT "Build optional Qt Widgets GUI" OFF)

if(DBMS_BUILD_QT)
    set(CMAKE_AUTOMOC ON)
    set(CMAKE_AUTORCC ON)
    set(CMAKE_AUTOUIC ON)

    find_package(QT NAMES Qt6 Qt5 COMPONENTS Widgets QUIET)
    if(QT_FOUND)
        find_package(Qt${QT_VERSION_MAJOR} COMPONENTS Widgets REQUIRED)

        get_property(_qt_widgets_location TARGET Qt${QT_VERSION_MAJOR}::Widgets PROPERTY IMPORTED_LOCATION_RELEASE)
        if(NOT _qt_widgets_location)
            get_property(_qt_widgets_location TARGET Qt${QT_VERSION_MAJOR}::Widgets PROPERTY IMPORTED_LOCATION)
        endif()

        set(_qt_usable TRUE)
        if(MINGW AND _qt_widgets_location MATCHES "[Aa]naconda|[Cc]onda")
            set(_qt_usable FALSE)
            message(WARNING
                "Found Qt at '${_qt_widgets_location}', which appears to be a Conda/MSVC Qt package. "
                "Skipping dbms_qt for MinGW. Set CMAKE_PREFIX_PATH to a MinGW-built Qt installation.")
        endif()

        if(_qt_usable)
            set(DBMS_QT_SOURCES
                src/ui/AdminPanel.h
                src/ui/AdminPanel.cpp
                src/ui/DatabaseTreePanel.h
                src/ui/DatabaseTreePanel.cpp
                src/ui/CatalogTypes.h
                src/ui/MainWindow.h
                src/ui/MainWindow.cpp
                src/ui/ObjectDetailPanel.h
                src/ui/ObjectDetailPanel.cpp
                src/ui/QtSessionAdapter.h
                src/ui/QtSessionAdapter.cpp
                src/ui/SessionPanel.h
                src/ui/SessionPanel.cpp
                src/ui/SqlEditorPanel.h
                src/ui/SqlEditorPanel.cpp
                src/ui/ResultTablePanel.h
                src/ui/ResultTablePanel.cpp
                src/ui/StatusMetaPanel.h
                src/ui/StatusMetaPanel.cpp
                src/ui/TableEditorPanel.h
                src/ui/TableEditorPanel.cpp
            )

            add_executable(dbms_qt WIN32
                src/ui/main_qt.cpp
                ${DBMS_QT_SOURCES}
            )
            target_link_libraries(dbms_qt PRIVATE dbms_core Qt${QT_VERSION_MAJOR}::Widgets)
            target_include_directories(dbms_qt PRIVATE ${CMAKE_SOURCE_DIR}/src)
            target_compile_options(dbms_qt PRIVATE
                $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
                $<$<CXX_COMPILER_ID:MSVC>:/W4 /utf-8 /wd4996>
            )
            target_compile_definitions(dbms_qt PRIVATE
                DBMS_VERSION="${PROJECT_VERSION}"
                $<$<BOOL:${WIN32}>:DBMS_WINDOWS>
                $<$<BOOL:${UNIX}>:DBMS_UNIX>
            )

            if(DBMS_BUILD_TESTS)
                find_package(Qt${QT_VERSION_MAJOR} COMPONENTS Test REQUIRED)
                add_executable(test_qt_ui
                    tests/test_qt_ui.cpp
                    ${DBMS_QT_SOURCES}
                )
                target_link_libraries(test_qt_ui PRIVATE dbms_core Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Test)
                target_include_directories(test_qt_ui PRIVATE ${CMAKE_SOURCE_DIR}/src)
                target_compile_definitions(test_qt_ui PRIVATE
                    DBMS_VERSION="${PROJECT_VERSION}"
                    $<$<BOOL:${WIN32}>:DBMS_WINDOWS>
                    $<$<BOOL:${UNIX}>:DBMS_UNIX>
                )
                add_test(NAME test_qt_ui COMMAND test_qt_ui)
                set_tests_properties(test_qt_ui PROPERTIES
                    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
                )
            endif()
        endif()
    else()
        message(WARNING "DBMS_BUILD_QT=ON, but Qt Widgets was not found. Skipping dbms_qt target.")
    endif()
endif()

# ---- 安装目标 ----
install(TARGETS dbms RUNTIME DESTINATION bin)
install(DIRECTORY data/ DESTINATION data OPTIONAL)

# ---- 单元测试 ----
option(DBMS_BUILD_TESTS "Build unit tests" ON)

if(DBMS_BUILD_TESTS)
    enable_testing()

    # 引擎源文件（不含 main.cpp）
    file(GLOB TEST_CLI_SOURCES "src/cli/*.cpp")

    function(add_unit_test name file)
        add_executable(${name} ${file} ${TEST_CLI_SOURCES})
        target_include_directories(${name} PRIVATE ${CMAKE_SOURCE_DIR}/src)
        target_link_libraries(${name} PRIVATE dbms_core)
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
    add_unit_test(test_join_parse    tests/test_join_parse.cpp)
endif()
