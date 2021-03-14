cmake_minimum_required(VERSION 3.9)
project(collocatordb VERSION 1.2.0 DESCRIPTION "Storing and retrieving collocation counts based on RocksDB")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -fno-rtti")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g")

include(GNUInstallDirs)
include_directories(/usr/local/include)
find_library(ROCKSDB librocksdb.so.5.11)
enable_testing()
add_library(collocatordb SHARED src/collocatordb.cc)
target_link_libraries(collocatordb pthread rt snappy z bz2 lz4 zstd ${ROCKSDB})
set_target_properties(collocatordb PROPERTIES
        VERSION ${PROJECT_VERSION}
        SOVERSION 1
        PUBLIC_HEADER src/collocatordb.h)

add_library(collocatordb_static STATIC src/collocatordb.cc)
target_link_libraries(collocatordb_static pthread rt snappy  bz2 lz4 zstd)

add_executable(basic_test tests/basic_test.c tests/acutest.h)
TARGET_LINK_LIBRARIES(basic_test ${ROCKSDB} collocatordb)
add_test(BASIC_TEST basic_test)

configure_file(collocatordb.pc.in collocatordb.pc @ONLY)
target_include_directories(collocatordb PRIVATE .)
install(TARGETS collocatordb collocatordb_static
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES ${CMAKE_BINARY_DIR}/collocatordb.pc
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/pkgconfig)

# Install library
install(TARGETS ${PROJECT_NAME} DESTINATION lib/${PROJECT_NAME})
