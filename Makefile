COLLOCATDB_MAJOR=1
COLLOCATDB_MINOR=2
COLLOCATDB_PATCH=0

INSTALL_PATH ?= /usr/local

PLATFORM_CCFLAGS= -DROCKSDB_PLATFORM_POSIX -DROCKSDB_LIB_IO_POSIX  -DOS_LINUX -fno-builtin-memcmp -DROCKSDB_FALLOCATE_PRESENT -DSNAPPY -DGFLAGS=1 -DZLIB -DBZIP2 -DLZ4 -DZSTD -DROCKSDB_MALLOC_USABLE_SIZE -DROCKSDB_PTHREAD_ADAPTIVE_MUTEX -DROCKSDB_BACKTRACE -DROCKSDB_RANGESYNC_PRESENT -DROCKSDB_SCHED_GETCPU_PRESENT -march=native  -DROCKSDB_SUPPORT_THREAD_LOCAL
PLATFORM_CXXFLAGS=-std=c++11  -DROCKSDB_PLATFORM_POSIX -DROCKSDB_LIB_IO_POSIX  -DOS_LINUX -fno-builtin-memcmp -DROCKSDB_FALLOCATE_PRESENT -DSNAPPY -DGFLAGS=1 -DZLIB -DBZIP2 -DLZ4 -DZSTD -DROCKSDB_MALLOC_USABLE_SIZE -DROCKSDB_PTHREAD_ADAPTIVE_MUTEX -DROCKSDB_BACKTRACE -DROCKSDB_RANGESYNC_PRESENT -DROCKSDB_SCHED_GETCPU_PRESENT -march=native  -DROCKSDB_SUPPORT_THREAD_LOCAL
PLATFORM=OS_LINUX
PLATFORM_LDFLAGS= -L$(INSTALL_PATH)/lib -Wl,-rpath=$(INSTALL_PATH)/lib -lpthread -lrt -lsnappy -lz -lbz2 -llz4 -lzstd

CXXFLAGS = -Wall -Wno-reorder -I/usr/local/include -g -std=c++11  -Ofast
CFLAGS = -Wall -I/usr/local/rocksdb -I$(INSTALL_PATH)/include -g -std=gnu99  -O2

ARFLAGS = ${EXTRA_ARFLAGS} rs

ifneq ($(USE_RTTI), 1)
	CXXFLAGS += -fno-rtti
endif
LIB_SOURCES = collocatordb.cc

LIBOBJECTS = $(LIB_SOURCES:.cc=.o)

.PHONY: static_lib shared_lib install-static install-shared

all: static_lib shared_lib dumpllr c_testcdb

install: install-static install-shared

hello_world: hello_world.c collocatordb.h collocatordb.o Makefile
	$(CC) $(CFLAGS) -L. -L$(INSTALL_PATH)/lib $@.c -o$@ collocatordb.o --std=gnu99 -lstdc++ -lm $(PLATFORM_LDFLAGS) $(PLATFORM_CCFLAGS) $(EXEC_LDFLAGS) -lrocksdb

testcdb: testcdb.cc collocatordb.h collocatordb.o Makefile
	$(CXX) $(CXXFLAGS) -L. -L$(INSTALL_PATH)/lib $@.cc -o$@ collocatordb.o -lrocksdb $(PLATFORM_LDFLAGS) $(PLATFORM_CXXFLAGS) $(EXEC_LDFLAGS)

dumpllr: dumpllr.cc collocatordb.h collocatordb.o Makefile
	$(CXX) $(CXXFLAGS) -L. -L$(INSTALL_PATH)/lib $@.cc -fopenmp -o$@ collocatordb.o $(INSTALL_PATH)/lib/librocksdb.a $(PLATFORM_LDFLAGS) $(PLATFORM_CXXFLAGS) $(EXEC_LDFLAGS)

dumppmicubed: dumppmicubed.cc collocatordb.h collocatordb.o Makefile
	$(CXX) $(CXXFLAGS) -L. -L$(INSTALL_PATH)/lib $@.cc -fopenmp -o$@ collocatordb.o -lrocksdb $(PLATFORM_LDFLAGS) $(PLATFORM_CXXFLAGS) $(EXEC_LDFLAGS)

dumpldafdiff: dumpldafdiff.cc collocatordb.h collocatordb.o Makefile
	$(CXX) $(CXXFLAGS) -D_GLIBCXX_PARALLEL -L. -L$(INSTALL_PATH)/lib $@.cc -fopenmp -o$@ collocatordb.o /vol/work/kupietz/rocksdb/librocksdb.a $(PLATFORM_LDFLAGS) $(PLATFORM_CXXFLAGS) $(EXEC_LDFLAGS)

c_testcdb: c_testcdb.c collocatordb.h collocatordb.o Makefile
	$(CC) $(CFLAGS) -L. -L$(INSTALL_PATH)/lib $@.c -o$@ collocatordb.o -std=gnu99 -lstdc++ -lm -lrocksdb $(PLATFORM_LDFLAGS) $(PLATFORM_CCFLAGS) $(EXEC_LDFLAGS)

collocatordb: collocatordb.cc Makefile
	$(CXX) $(CXXFLAGS) -L$(INSTALL_PATH)/lib $@.cc -o$@ -lrocksdb $(PLATFORM_LDFLAGS) $(PLATFORM_CXXFLAGS) $(EXEC_LDFLAGS)

static_lib: libcollocatordb.a

shared_lib: libcollocatordb.so.$(COLLOCATDB_MAJOR).$(COLLOCATDB_MINOR)

libcollocatordb.a: $(LIBOBJECTS)
	$(AM_V_AR)rm -f $@
	$(AM_V_at)$(AR) $(ARFLAGS) $@ $(LIBOBJECTS)

libcollocatordb.so.$(COLLOCATDB_MAJOR).$(COLLOCATDB_MINOR): collocatordb.cc
	$(CXX) $(CXXFLAGS) -Wall -D_GLIBCXX_PARALLEL -fopenmp -c collocatordb.cc -Wl,-soname=libcollocatordb.so.1 -Wl,--version-script=collocatordb.exmap -shared -fPIC -o libcollocatordb.so.$(COLLOCATDB_MAJOR).$(COLLOCATDB_MINOR)

.cc.o:
	$(CXX) $(CXXFLAGS) -c $< -o$@ $(PLATFORM_CXXFLAGS)

install-static: libcollocatordb.a collocatordb.h
	install -d $(INSTALL_PATH)/include && \
	install -d $(INSTALL_PATH)/lib && \
	install -t $(INSTALL_PATH)/include -D -C -m 644  collocatordb.h $ && \
	install -t $(INSTALL_PATH)/lib -C -D -m 755 libcollocatordb.a

install-shared: libcollocatordb.so.$(COLLOCATDB_MAJOR).$(COLLOCATDB_MINOR) collocatordb.h
	install -d $(INSTALL_PATH)/include && \
	install -d $(INSTALL_PATH)/lib && \
	install -Dt $(INSTALL_PATH)/include -C -m 644 collocatordb.h && \
	install -Dt $(INSTALL_PATH)/lib -C -m 755 libcollocatordb.so.$(COLLOCATDB_MAJOR).$(COLLOCATDB_MINOR) && \
		ln -fs $(INSTALL_PATH)/lib/libcollocatordb.so.$(COLLOCATDB_MAJOR).$(COLLOCATDB_MINOR) $(INSTALL_PATH)/lib/libcollocatordb.so.$(COLLOCATDB_MAJOR) && \
		ln -fs $(INSTALL_PATH)/lib/libcollocatordb.so.$(COLLOCATDB_MAJOR) $(INSTALL_PATH)/lib/libcollocatordb.so

clean:
	rm -f *.so* *.a *.o c_testcdb collocatordb dumpldafdiff dumppmicubed dumpllr testcdb hello_world
