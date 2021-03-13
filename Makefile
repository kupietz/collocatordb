COLLOCATDB_MAJOR=1
COLLOCATDB_MINOR=2
COLLOCATDB_PATCH=0

INSTALL_PATH ?= /usr/local

PLATFORM_CCFLAGS= -DROCKSDB_PLATFORM_POSIX -DROCKSDB_LIB_IO_POSIX  -DOS_LINUX -fno-builtin-memcmp -DROCKSDB_FALLOCATE_PRESENT -DSNAPPY -DGFLAGS=1 -DZLIB -DBZIP2 -DLZ4 -DZSTD -DROCKSDB_MALLOC_USABLE_SIZE -DROCKSDB_PTHREAD_ADAPTIVE_MUTEX -DROCKSDB_BACKTRACE -DROCKSDB_RANGESYNC_PRESENT -DROCKSDB_SCHED_GETCPU_PRESENT -march=native  -DROCKSDB_SUPPORT_THREAD_LOCAL
PLATFORM_CXXFLAGS=-std=c++11  -DROCKSDB_PLATFORM_POSIX -DROCKSDB_LIB_IO_POSIX  -DOS_LINUX -fno-builtin-memcmp -DROCKSDB_FALLOCATE_PRESENT -DSNAPPY -DGFLAGS=1 -DZLIB -DBZIP2 -DLZ4 -DZSTD -DROCKSDB_MALLOC_USABLE_SIZE -DROCKSDB_PTHREAD_ADAPTIVE_MUTEX -DROCKSDB_BACKTRACE -DROCKSDB_RANGESYNC_PRESENT -DROCKSDB_SCHED_GETCPU_PRESENT -march=native  -DROCKSDB_SUPPORT_THREAD_LOCAL
PLATFORM=OS_LINUX
PLATFORM_LDFLAGS= -L$(INSTALL_PATH)/lib -Wl,-rpath=$(INSTALL_PATH)/lib -lpthread -lrt -lsnappy -lz -lbz2 -llz4 -lzstd

CXXFLAGS = -Wall -Wno-reorder -I/usr/local/include -g -std=c++11
CFLAGS = -Wall -I/usr/local/rocksdb -I$(INSTALL_PATH)/include -g -std=gnu99  -O2

ARFLAGS = ${EXTRA_ARFLAGS} rs

ifneq ($(USE_RTTI), 1)
	CXXFLAGS += -fno-rtti
endif
SOURCE_DIR := src
BUILD_DIR := build
BIN_DIR := bin
LIB_DIR := lib
LIB_SOURCES = src/collocatordb.cc

LIB_OBJECTS := $(subst $(SOURCE_DIR),$(BUILD_DIR),$(LIB_SOURCES:.cc=.o))

.PHONY: static_lib shared_lib install-static install-shared

all: static_lib shared_lib

install: install-static install-shared

bin/hello_world: examples/hello_world.c src/collocatordb.h build/collocatordb.o Makefile
	mkdir -p bin
	$(CC) $(CFLAGS) -L. -L$(INSTALL_PATH)/lib examples/hello_world.c -o$@ build/collocatordb.o --std=gnu99 -lstdc++ -lm $(PLATFORM_LDFLAGS) $(PLATFORM_CCFLAGS) $(EXEC_LDFLAGS) -lrocksdb

bin/testcdb: src/testcdb.cc src/collocatordb.h build/collocatordb.o Makefile
	$(CXX) $(CXXFLAGS) -L. -L$(INSTALL_PATH)/lib $@.cc -o$@ build/collocatordb.o -lrocksdb $(PLATFORM_LDFLAGS) $(PLATFORM_CXXFLAGS) $(EXEC_LDFLAGS)

bin/dumpllr: examples/dumpllr.cc src/collocatordb.h build/collocatordb.o Makefile
	$(CXX) $(CXXFLAGS) -L. -L$(INSTALL_PATH)/lib $@.cc -fopenmp -o$@ build/collocatordb.o $(INSTALL_PATH)/lib/librocksdb.a $(PLATFORM_LDFLAGS) $(PLATFORM_CXXFLAGS) $(EXEC_LDFLAGS)

bin/dumppmicubed: examples/dumppmicubed.cc src/collocatordb.h build/collocatordb.o Makefile
	$(CXX) $(CXXFLAGS) -L. -L$(INSTALL_PATH)/lib $@.cc -fopenmp -o$@ build/collocatordb.o -lrocksdb $(PLATFORM_LDFLAGS) $(PLATFORM_CXXFLAGS) $(EXEC_LDFLAGS)

bin/dumpldafdiff: dumpldafdiff.cc src/collocatordb.h build/collocatordb.o Makefile
	$(CXX) $(CXXFLAGS) -D_GLIBCXX_PARALLEL -L. -L$(INSTALL_PATH)/lib $@.cc -fopenmp -o$@ build/collocatordb.o /vol/work/kupietz/rocksdb/librocksdb.a $(PLATFORM_LDFLAGS) $(PLATFORM_CXXFLAGS) $(EXEC_LDFLAGS)

bin/c_testcdb: examples/c_testcdb.c src/collocatordb.h build/collocatordb.o Makefile
	$(CC) $(CFLAGS) -L. -L$(INSTALL_PATH)/lib $@.c -o$@ build/collocatordb.o -std=gnu99 -lstdc++ -lm -lrocksdb $(PLATFORM_LDFLAGS) $(PLATFORM_CCFLAGS) $(EXEC_LDFLAGS)

bin/collocatordb: src/collocatordb.cc Makefile
	$(CXX) $(CXXFLAGS) -L$(INSTALL_PATH)/lib $@.cc -o$@ -lrocksdb $(PLATFORM_LDFLAGS) $(PLATFORM_CXXFLAGS) $(EXEC_LDFLAGS)

static_lib: lib/libcollocatordb.a

shared_lib: lib/libcollocatordb.so.$(COLLOCATDB_MAJOR).$(COLLOCATDB_MINOR)

lib/libcollocatordb.a: $(LIB_OBJECTS)
	mkdir -p lib
	$(AM_V_AR)rm -f $@
	$(AM_V_at)$(AR) $(ARFLAGS) $@ $(LIB_OBJECTS)

lib/libcollocatordb.so.$(COLLOCATDB_MAJOR).$(COLLOCATDB_MINOR): $(LIB_SOURCES)
	$(CXX) $(CXXFLAGS) -Wall -D_GLIBCXX_PARALLEL -fopenmp -c $< -Wl,-soname=libcollocatordb.so.1 -Wl,--version-script=collocatordb.exmap -shared -fPIC -o $@

build/collocatordb.o : src/collocatordb.cc
	mkdir -p build
	$(CXX) $(CXXFLAGS) -c $< -o$@ $(PLATFORM_CXXFLAGS)

.cc.o:
	$(CXX) $(CXXFLAGS) -c $< -o$@ $(PLATFORM_CXXFLAGS)

install-static: lib/libcollocatordb.a src/collocatordb.h
	install -d $(INSTALL_PATH)/include && \
	install -d $(INSTALL_PATH)/lib && \
	install -t $(INSTALL_PATH)/include -D -C -m 644  src/collocatordb.h $ && \
	install -t $(INSTALL_PATH)/lib -C -D -m 755 lib/libcollocatordb.a

install-shared: lib/libcollocatordb.so.$(COLLOCATDB_MAJOR).$(COLLOCATDB_MINOR) src/collocatordb.h
	install -d $(INSTALL_PATH)/include && \
	install -d $(INSTALL_PATH)/lib && \
	install -Dt $(INSTALL_PATH)/include -C -m 644 src/collocatordb.h && \
	install -Dt $(INSTALL_PATH)/lib -C -m 755 lib/libcollocatordb.so.$(COLLOCATDB_MAJOR).$(COLLOCATDB_MINOR) && \
		ln -fs $(INSTALL_PATH)/lib/libcollocatordb.so.$(COLLOCATDB_MAJOR).$(COLLOCATDB_MINOR) $(INSTALL_PATH)/lib/libcollocatordb.so.$(COLLOCATDB_MAJOR) && \
		ln -fs $(INSTALL_PATH)/lib/libcollocatordb.so.$(COLLOCATDB_MAJOR) $(INSTALL_PATH)/lib/libcollocatordb.so

clean:
	rm -rf $(LIB_DIR) $(BUILD_DIR) $(BIN_DIR)
	rm -f *.so* *.a *.o c_testcdb collocatordb dumpldafdiff dumppmicubed dumpllr testcdb hello_world
