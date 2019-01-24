PLATFORM_CCFLAGS= -DROCKSDB_PLATFORM_POSIX -DROCKSDB_LIB_IO_POSIX  -DOS_LINUX -fno-builtin-memcmp -DROCKSDB_FALLOCATE_PRESENT -DSNAPPY -DGFLAGS=1 -DZLIB -DBZIP2 -DLZ4 -DZSTD -DROCKSDB_MALLOC_USABLE_SIZE -DROCKSDB_PTHREAD_ADAPTIVE_MUTEX -DROCKSDB_BACKTRACE -DROCKSDB_RANGESYNC_PRESENT -DROCKSDB_SCHED_GETCPU_PRESENT -march=native  -DROCKSDB_SUPPORT_THREAD_LOCAL
PLATFORM_CXXFLAGS=-std=c++11  -DROCKSDB_PLATFORM_POSIX -DROCKSDB_LIB_IO_POSIX  -DOS_LINUX -fno-builtin-memcmp -DROCKSDB_FALLOCATE_PRESENT -DSNAPPY -DGFLAGS=1 -DZLIB -DBZIP2 -DLZ4 -DZSTD -DROCKSDB_MALLOC_USABLE_SIZE -DROCKSDB_PTHREAD_ADAPTIVE_MUTEX -DROCKSDB_BACKTRACE -DROCKSDB_RANGESYNC_PRESENT -DROCKSDB_SCHED_GETCPU_PRESENT -march=native  -DROCKSDB_SUPPORT_THREAD_LOCAL
PLATFORM=OS_LINUX
PLATFORM_LDFLAGS= -L/usr/local/lib -lpthread -lrt -lsnappy -lz -lbz2 -llz4 -lzstd

CXXFLAGS = -Wall -Wno-reorder -I/usr/local/include -g -std=c++11  -Ofast -march=k8
CFLAGS = -Wall -I/usr/local/include -g -std=gnu99  -O2 -march=k8

ARFLAGS = ${EXTRA_ARFLAGS} rs

ifneq ($(USE_RTTI), 1)
	CXXFLAGS += -fno-rtti
endif
LIB_SOURCES = collocatordb.cc

LIBOBJECTS = $(LIB_SOURCES:.cc=.o)
INSTALL_PATH = /usr/local

hello_world: hello_world.c collocatordb.h collocatordb.o Makefile
	$(CC) $(CFLAGS) -L. -L/usr/local/lib $@.c -o$@ collocatordb.o /vol/work/kupietz/rocksdb/librocksdb.a -std=gnu99 -lstdc++ -lm $(PLATFORM_LDFLAGS) $(PLATFORM_CCFLAGS) $(EXEC_LDFLAGS) 

testcdb: testcdb.cc collocatordb.h collocatordb.o Makefile
	$(CXX) $(CXXFLAGS) -L. -L/usr/local/lib $@.cc -o$@ collocatordb.o -lrocksdb $(PLATFORM_LDFLAGS) $(PLATFORM_CXXFLAGS) $(EXEC_LDFLAGS)

dumpllr: dumpllr.cc collocatordb.h collocatordb.o Makefile
	$(CXX) $(CXXFLAGS) -L. -L/usr/local/lib $@.cc -fopenmp -o$@ collocatordb.o /vol/work/kupietz/rocksdb/librocksdb.a $(PLATFORM_LDFLAGS) $(PLATFORM_CXXFLAGS) $(EXEC_LDFLAGS)

dumppmicubed: dumppmicubed.cc collocatordb.h collocatordb.o Makefile
	$(CXX) $(CXXFLAGS) -L. -L/usr/local/lib $@.cc -fopenmp -o$@ collocatordb.o /vol/work/kupietz/rocksdb/librocksdb.a $(PLATFORM_LDFLAGS) $(PLATFORM_CXXFLAGS) $(EXEC_LDFLAGS)

c_testcdb: c_testcdb.c collocatordb.h collocatordb.o Makefile
	$(CC) $(CFLAGS) -L. -L/usr/local/lib $@.c -o$@ collocatordb.o -std=gnu99 -lstdc++ -lm -lrocksdb $(PLATFORM_LDFLAGS) $(PLATFORM_CCFLAGS) $(EXEC_LDFLAGS)

collocatordb: collocatordb.cc Makefile
	$(CXX) $(CXXFLAGS) -L/usr/local/lib $@.cc -o$@ -lrocksdb $(PLATFORM_LDFLAGS) $(PLATFORM_CXXFLAGS) $(EXEC_LDFLAGS)

libcollocatordb.a: $(LIBOBJECTS)
	$(AM_V_AR)rm -f $@
	$(AM_V_at)$(AR) $(ARFLAGS) $@ $(LIBOBJECTS)

libcollocatordb.so.1: collocatordb.cc
	$(CXX) $(CXXFLAGS) -D_GLIBCXX_PARALLEL -march=native -Ofast -fopenmp -c collocatordb.cc -Wl,-soname=libcollocatordb.so.1 -Wl,--version-script=collocatordb.exmap -shared -fPIC -o libcollocatordb.so.1

.cc.o:
	$(CXX) $(CXXFLAGS) -c $< -o$@ $(PLATFORM_CXXFLAGS)

install-static: libcollocatordb.a
	install -C -m 755 libcollocatordb.a $(INSTALL_PATH)/lib

install-shared: libcollocatordb.so.1
	install -C -m 755 libcollocatordb.so.1 $(INSTALL_PATH)/lib && \
		ln -fs $(INSTALL_PATH)/lib/libcollocatordb.so.1 $(INSTALL_PATH)/lib/libcollocatordb.so
