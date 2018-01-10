#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "collocatordb.h"

int main() {
	COLLOCATORDB *cdb = open_collocatordb_for_write("/tmp/test.rocksdb");
	inc_collocator(cdb, 2000, 2000, 4);
	inc_collocator(cdb, 2000, 2001, 4);
	inc_collocator(cdb, 2000, 2002, 4);
	dump_collocators(cdb, 2000, 0, 0);
	return 0;
}
