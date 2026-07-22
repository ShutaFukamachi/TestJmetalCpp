cmake_minimum_required(VERSION 3.26)
project(TestJmetalCpp)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /execution-charset:SHIFT_JIS /source-charset:UTF-8")


set(GUROBI_DIR "C:/gurobi1300/win64")


include_directories("${GUROBI_DIR}/include")


link_directories("${GUROBI_DIR}/lib")



add_library(jmetalcpp STATIC)

file(GLOB_RECURSE ALL_CPP CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

set(JMETALCPP_SOURCES "")
foreach (f IN LISTS ALL_CPP)
    file(TO_CMAKE_PATH "${f}" fwd)
    list(APPEND JMETALCPP_SOURCES "${f}")
endforeach ()

target_sources(jmetalcpp PRIVATE ${JMETALCPP_SOURCES})


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

        "${GUROBI_DIR}/include"
)


add_executable(TestJmetalCpp main.cpp
)

add_executable(NSGAEncCpp main_NSGAII_RCPSP_EncodingComparison.cpp
)

add_executable(NSGAMaxShiftDurCpp main_NSGAII_RCPSP_MaxShiftDur.cpp
)

add_executable(A3AB main_A3_AB.cpp
)

add_executable(E17 main_E17_PlacementExact.cpp
)



target_link_libraries(TestJmetalCpp PRIVATE
        jmetalcpp


        gurobi130
        $<$<CONFIG:Debug>:gurobi_c++mdd2017>
        $<$<CONFIG:Release>:gurobi_c++md2017>
)

target_link_libraries(NSGAEncCpp PRIVATE
        jmetalcpp

        gurobi130
        $<$<CONFIG:Debug>:gurobi_c++mdd2017>
        $<$<CONFIG:Release>:gurobi_c++md2017>
)

target_link_libraries(A3AB PRIVATE
        jmetalcpp

        gurobi130
        $<$<CONFIG:Debug>:gurobi_c++mdd2017>
        $<$<CONFIG:Release>:gurobi_c++md2017>
)

target_link_libraries(E17 PRIVATE
        jmetalcpp

        gurobi130
        $<$<CONFIG:Debug>:gurobi_c++mdd2017>
        $<$<CONFIG:Release>:gurobi_c++md2017>
)

target_link_libraries(NSGAMaxShiftDurCpp PRIVATE
        jmetalcpp

        gurobi130
        $<$<CONFIG:Debug>:gurobi_c++mdd2017>
        $<$<CONFIG:Release>:gurobi_c++md2017>
)

add_executable(NSGAMaxShiftCpp main_NSGAII_RCPSP_MaxShift.cpp
)

target_link_libraries(NSGAMaxShiftCpp PRIVATE
        jmetalcpp

        gurobi130
        $<$<CONFIG:Debug>:gurobi_c++mdd2017>
        $<$<CONFIG:Release>:gurobi_c++md2017>
)

add_executable(NSGAMaxShiftSegCpp main_NSGAII_RCPSP_MaxShiftSeg.cpp
)

target_link_libraries(NSGAMaxShiftSegCpp PRIVATE
        jmetalcpp

        gurobi130
        $<$<CONFIG:Debug>:gurobi_c++mdd2017>
        $<$<CONFIG:Release>:gurobi_c++md2017>
)

add_executable(RCPSPMIPCpp main_MIP_RCPSP.cpp
)

target_link_libraries(RCPSPMIPCpp PRIVATE
        jmetalcpp

        gurobi130
        $<$<CONFIG:Debug>:gurobi_c++mdd2017>
        $<$<CONFIG:Release>:gurobi_c++md2017>
)
