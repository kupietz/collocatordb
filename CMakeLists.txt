cmake_minimum_required(VERSION 3.9)
project(collocatordb VERSION 1.3.0 DESCRIPTION "Storing and retrieving collocation counts based on RocksDB")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -fno-rtti -std=c++11 -std=gnu++11")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g")

include(GNUInstallDirs)

# the RPATH to be used when installing, but only if it's not a system directory
list(FIND CMAKE_PLATFORM_IMPLICIT_LINK_DIRECTORIES "${CMAKE_INSTALL_PREFIX}/lib" isSystemDir)
if("${isSystemDir}" STREQUAL "-1")
    set(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib")
endif("${isSystemDir}" STREQUAL "-1")

include_directories(/usr/local/include;/usr/local/kl/include)
link_directories(/usr/local/kl/lib /usr/local/lib)
find_library(ROCKSDB NAMES librocksdb.so.5.11)
find_library(ROCKSDB_STATIC librocksdb.a)

enable_testing()
add_library(collocatordb SHARED src/collocatordb.cc)
target_link_libraries(collocatordb pthread rt snappy z bz2 lz4 zstd ${ROCKSDB} dl)
set_target_properties(collocatordb PROPERTIES
        VERSION ${PROJECT_VERSION}
        SOVERSION 1
        PUBLIC_HEADER src/collocatordb.h)

add_library(collocatordb_static STATIC src/collocatordb.cc)
target_link_libraries(collocatordb_static ${ROCKSDB_STATIC} pthread rt snappy z bz2 lz4 zstd dl)

add_executable(basic_test tests/basic_test.c tests/acutest.h)
TARGET_LINK_LIBRARIES(basic_test ${ROCKSDB} collocatordb)
add_test(BASIC_TEST basic_test)

add_executable(static_library_test tests/basic_test.c tests/acutest.h)
TARGET_LINK_LIBRARIES(static_library_test collocatordb_static)
add_test(STATIC_LIBRARY_TEST static_library_test)

# Install library
install(TARGETS ${PROJECT_NAME} collocatordb_static
                LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
                PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
