cmake_minimum_required(VERSION 3.9)
project(collocatordb VERSION 1.3.2 DESCRIPTION "Storing and retrieving collocation counts based on RocksDB")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -fno-rtti -std=c++11 -std=gnu++11")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g")

include(GNUInstallDirs)

# the RPATH to be used when installing, but only if it's not a system directory
list(FIND CMAKE_PLATFORM_IMPLICIT_LINK_DIRECTORIES "${CMAKE_INSTALL_PREFIX}/lib" isSystemDir)
if("${isSystemDir}" STREQUAL "-1")
    set(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib")
endif("${isSystemDir}" STREQUAL "-1")

configure_file(src/config.h.in ${CMAKE_CURRENT_BINARY_DIR}/config.h @ONLY)
include_directories(/usr/local/include /opt/homebrew/include build)
link_directories(/usr/local/lib64 /usr/local/lib /usr/lib64 /usr/lib /opt/homebrew/lib)
if (1 AND APPLE)
       message("MacOS deteted")
       set(LIBRT "")
       find_library(ROCKSDB NAMES librocksdb.a)
       set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -flat_namespace")
       set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -flat_namespace")
else()
       set(LIBRT "rt")
       find_library(ROCKSDB NAMES librocksdb.so.5.11)
endif()
find_library(ROCKSDB_STATIC librocksdb.a)

get_cmake_property(_variableNames VARIABLES)
list (SORT _variableNames)
foreach (_variableName ${_variableNames})
    message(STATUS "${_variableName}=${${_variableName}}")
endforeach()

enable_testing()
add_library(collocatordb SHARED src/collocatordb.cc)

target_link_libraries(collocatordb pthread ${LIBRT} snappy z bz2 lz4 zstd ${ROCKSDB} dl)

set_target_properties(collocatordb PROPERTIES
        VERSION ${PROJECT_VERSION}
        SOVERSION 1
        PUBLIC_HEADER src/collocatordb.h)

add_library(collocatordb_static STATIC src/collocatordb.cc)
target_link_libraries(collocatordb_static ${ROCKSDB_STATIC} pthread ${LIBRT} snappy z bz2 lz4 zstd dl)

add_executable(basic_test tests/basic_test.c tests/acutest.h)
TARGET_LINK_LIBRARIES(basic_test ${ROCKSDB} collocatordb)
add_test(BASIC_TEST basic_test)

add_executable(static_library_test tests/basic_test.c tests/acutest.h)
TARGET_LINK_LIBRARIES(static_library_test collocatordb_static)
add_test(STATIC_LIBRARY_TEST static_library_test)

# Install library
install(TARGETS ${PROJECT_NAME} collocatordb_static
                LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
                ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
                PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

if (1 AND APPLE)
    add_custom_command(TARGET collocatordb POST_BUILD
        COMMAND install_name_tool -id "/usr/local/lib/libcollocatordb.1.dylib" $<TARGET_FILE:collocatordb>)
endif()

file(GLOB TOOLS_SOURCES tools/*.c tools/*.cc)
add_custom_target(tools)
foreach(TOOL_SOURCE ${TOOLS_SOURCES})
    get_filename_component(TOOL_NAME ${TOOL_SOURCE} NAME_WE)
    add_executable(${TOOL_NAME} ${TOOL_SOURCE})
    target_link_libraries(${TOOL_NAME} ${ROCKSDB} collocatordb)
    set_target_properties(${TOOL_NAME} PROPERTIES
            INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib64"
            BUILD_WITH_INSTALL_RPATH TRUE)
    add_dependencies(tools ${TOOL_NAME})
    install(TARGETS ${TOOL_NAME} DESTINATION ${CMAKE_INSTALL_BINDIR})
endforeach()

file(GLOB EXAMPLES_SOURCES examples/*.c examples/*.cc)
add_custom_target(examples)
foreach(EXAMPLE_SOURCE ${EXAMPLES_SOURCES})
    get_filename_component(EXAMPLE_NAME ${EXAMPLE_SOURCE} NAME_WE)
    add_executable(${EXAMPLE_NAME} EXCLUDE_FROM_ALL ${EXAMPLE_SOURCE})
    target_link_libraries(${EXAMPLE_NAME} ${ROCKSDB} collocatordb)
    add_dependencies(examples ${EXAMPLE_NAME})
endforeach()
