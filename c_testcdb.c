#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "collocatordb.h"

int main() {
	COLLOCATORS *cdb = open_collocators("/tmp/test2");
	inc_collocators(cdb, 2000, 2000, 4);
	inc_collocators(cdb, 2000, 2001, 4);
	inc_collocators(cdb, 2000, 2002, 4);
	dump_collocators(cdb, 2000, 0, 0);
	return 0;
}
