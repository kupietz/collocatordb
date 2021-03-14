cmake_minimum_required(VERSION 3.9)
project(collocatordb VERSION 1.2.0 DESCRIPTION "Storing and retrieving collocation counts based on RocksDB")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -fno-rtti -std=c++11 -std=gnu++11")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g")

include(GNUInstallDirs)
include_directories(/usr/local/include;/usr/local/kl/include)
link_directories(/usr/local/lib;/usr/local/kl/lib)
find_library(ROCKSDB NAMES librocksdb.so.5.11 librocksdb.so.5.10 librocksdb.so.5)
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

configure_file(collocatordb.pc.in collocatordb.pc @ONLY)
target_include_directories(collocatordb PRIVATE .)
install(TARGETS collocatordb collocatordb_static
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES ${CMAKE_BINARY_DIR}/collocatordb.pc
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/pkgconfig)

# Install library
install(TARGETS ${PROJECT_NAME} DESTINATION lib/${PROJECT_NAME})
