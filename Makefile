PLATFORM_CCFLAGS= -DROCKSDB_PLATFORM_POSIX -DROCKSDB_LIB_IO_POSIX  -DOS_LINUX -fno-builtin-memcmp -DROCKSDB_FALLOCATE_PRESENT -DSNAPPY -DGFLAGS=1 -DZLIB -DBZIP2 -DLZ4 -DZSTD -DROCKSDB_MALLOC_USABLE_SIZE -DROCKSDB_PTHREAD_ADAPTIVE_MUTEX -DROCKSDB_BACKTRACE -DROCKSDB_RANGESYNC_PRESENT -DROCKSDB_SCHED_GETCPU_PRESENT -march=native  -DROCKSDB_SUPPORT_THREAD_LOCAL
PLATFORM_CXXFLAGS=-std=c++11  -DROCKSDB_PLATFORM_POSIX -DROCKSDB_LIB_IO_POSIX  -DOS_LINUX -fno-builtin-memcmp -DROCKSDB_FALLOCATE_PRESENT -DSNAPPY -DGFLAGS=1 -DZLIB -DBZIP2 -DLZ4 -DZSTD -DROCKSDB_MALLOC_USABLE_SIZE -DROCKSDB_PTHREAD_ADAPTIVE_MUTEX -DROCKSDB_BACKTRACE -DROCKSDB_RANGESYNC_PRESENT -DROCKSDB_SCHED_GETCPU_PRESENT -march=native  -DROCKSDB_SUPPORT_THREAD_LOCAL
PLATFORM=OS_LINUX
PLATFORM_LDFLAGS= -lpthread -lrt -lsnappy -lz -lbz2 -llz4 -lzstd

CXXFLAGS = -Wall -Wno-reorder -I/usr/local/include -g -std=c++11  -Ofast -march=k8
CFLAGS = -Wall -I/usr/local/include -g -std=gnu99  -O2 -march=k8

ARFLAGS = ${EXTRA_ARFLAGS} rs

ifneq ($(USE_RTTI), 1)
	CXXFLAGS += -fno-rtti
endif
LIB_SOURCES = collocatordb.cc

LIBOBJECTS = $(LIB_SOURCES:.cc=.o)

testcdb: testcdb.cc collocatordb.h collocatordb.o Makefile
	$(CXX) $(CXXFLAGS) -L. -L/usr/local/lib $@.cc -o$@ collocatordb.o -lrocksdb $(PLATFORM_LDFLAGS) $(PLATFORM_CXXFLAGS) $(EXEC_LDFLAGS)

c_testcdb: c_testcdb.c collocatordb.h collocatordb.o Makefile
	$(CC) $(CFLAGS) -L. -L/usr/local/lib $@.c -o$@ collocatordb.o -std=gnu99 -lstdc++ -lm -lrocksdb $(PLATFORM_LDFLAGS) $(PLATFORM_CCFLAGS) $(EXEC_LDFLAGS)

c_testanalysis: c_testanalysis.c collocatordb.h collocatordb.o Makefile
	$(CC) $(CFLAGS) -L. -L/usr/local/lib $@.c -o$@ collocatordb.o -std=gnu99 -lstdc++ -lm -lrocksdb $(PLATFORM_LDFLAGS) $(PLATFORM_CCFLAGS) $(EXEC_LDFLAGS)

collocatordb: collocatordb.cc Makefile
	$(CXX) $(CXXFLAGS) -L/usr/local/lib $@.cc -o$@ -lrocksdb $(PLATFORM_LDFLAGS) $(PLATFORM_CXXFLAGS) $(EXEC_LDFLAGS)

libcollocatordb.a: $(LIBOBJECTS)
	$(AM_V_AR)rm -f $@
	$(AM_V_at)$(AR) $(ARFLAGS) $@ $(LIBOBJECTS)

libcollocatordb.so.1: collocatordb.cc
	$(CXX) $(CXXFLAGS) -c collocatordb.cc -Wl,-soname=libcollocatordb.so.1 -Wl,--version-script=collocatordb.exmap -shared -fPIC -o libcollocatordb.so.1

.cc.o:
	$(CXX) $(CXXFLAGS) -c $< -o$@ $(PLATFORM_CXXFLAGS)
