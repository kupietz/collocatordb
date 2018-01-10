#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "collocatordb.h"

int main() {
	COLLOCATORDB *cdb = open_collocatordb("/vol/work/kupietz/Work2/kl/trunk/Analysemethoden/wang2vec/sample");
  for(int i=100; i < 1000; i++)
    get_collocators(cdb, i);
  printf("%s\n", get_collocators_as_json(cdb, 500));
	return 0;
}
