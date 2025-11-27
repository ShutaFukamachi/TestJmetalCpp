cmake_minimum_required(VERSION 3.31)
project(TestJmetalCpp)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# MSVC の文字コード警告対策
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /execution-charset:SHIFT_JIS /source-charset:UTF-8")

# --- ライブラリ化 ---
add_library(jmetalcpp STATIC)

file(GLOB_RECURSE ALL_CPP CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

set(JMETALCPP_SOURCES "")
foreach (f IN LISTS ALL_CPP)
    file(TO_CMAKE_PATH "${f}" fwd)
    list(APPEND JMETALCPP_SOURCES "${f}")
endforeach ()

target_sources(jmetalcpp PRIVATE ${JMETALCPP_SOURCES})

# ヘッダのある全サブフォルダを include に追加
file(GLOB_RECURSE JMETALCPP_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h")

set(JMETALCPP_INCLUDE_DIRS "")
foreach (h IN LISTS JMETALCPP_HEADERS)
    get_filename_component(dir "${h}" DIRECTORY)
    list(APPEND JMETALCPP_INCLUDE_DIRS "${dir}")
endforeach ()
list(REMOVE_DUPLICATES JMETALCPP_INCLUDE_DIRS)

if (MSVC)
    target_compile_options(jmetalcpp PRIVATE /wd4716)
endif ()

target_include_directories(jmetalcpp PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        ${JMETALCPP_INCLUDE_DIRS}
)

add_executable(TestJmetalCpp main.cpp)
target_link_libraries(TestJmetalCpp PRIVATE jmetalcpp)