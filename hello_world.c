#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "collocatordb.h"

char dbpath[] = "/vol/work/kupietz/Work2/kl/trunk/Analysemethoden/word2vec/models/dereko-2017-ii";
const int testword = 259; // Welt

int main(int argc, char* argv[]) {
  COLLOCATORDB *cdb;

  fprintf(stderr, "opening collocatordb for reading: %s ...\n", dbpath);
  if(!(cdb = open_collocatordb(dbpath))) {
    fprintf(stderr, "Error opening %s exiting.\n", dbpath);
    exit(1);
  }
  fprintf(stderr, "Successfully opened %s.\n", dbpath);

  /*
  printf("raw dump of all “%s”-neighbour positions and frequencies:\n", get_word(cdb, testword));
	dump_collocators(cdb, testword, 0, 0);
  */

  printf("printing collocators of “%s” as json:\n", get_word(cdb, testword));
  printf("%s\n", get_collocators_as_json(cdb, testword));

	return 0;
}
