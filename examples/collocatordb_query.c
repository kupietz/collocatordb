#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../src/collocatordb.h"

int main(int argc, char *argv[]) {
  if (argc < 3) {
    fprintf(stderr,
            "Usage: %s <dbpath> <word1> [word2] ... [wordN]\n"
            "Gets the collocators of the given words from a collocatordb and "
            "prints them as json.\n"
            "Example: %s ../tests/data/wpd19_10000 geht auch\n",
            argv[0], argv[0]);
    return 1;
  }

  char *dbpath = argv[1];
    char *ext = strrchr(dbpath, '.');
    if (ext && strcmp(ext, ".rocksdb") == 0) {
        *ext = '\0';
    }

    COLLOCATORDB *cdb;

    fprintf(stderr, "opening collocatordb for reading: %s ...\n", dbpath);
    if (!(cdb = open_collocatordb(dbpath))) {
        fprintf(stderr, "Error opening %s exiting.\n", dbpath);
        return 1;
    }
    fprintf(stderr, "Successfully opened %s.\n", dbpath);

    int i;

    printf("[\n");
    for (i = 2; i < argc; i++) {
        uint32_t word_id = get_word_id(cdb, argv[i]);
        if (word_id == 0) {
            fprintf(stderr, "Word \"%s\" not found in the database.\n", argv[i]);
            continue;
        }

        fprintf(stderr, "printing collocators of \"%s\" as json:\n", argv[i]);
        printf("%s%s\n", get_collocators_as_json(cdb, word_id), i < argc - 1 ? "," : "");
    }
    printf("]\n");
    return 0;
}