#include <stddef.h>
#include <stdio.h>
#include <string.h>
#define __USE_XOPEN_EXTENDED
#include <ftw.h>
#include "../src/collocatordb.h"
#include "acutest.h"

char dbpath[] = "../tests/data/wpd19_10000";
const int testword = 10; // ist

void test_open_db() {
  COLLOCATORDB* cdb = open_collocatordb(dbpath);
  TEST_ASSERT(cdb != NULL);
}

void test_get_word() {
  COLLOCATORDB* cdb = open_collocatordb(dbpath);
  TEST_ASSERT(cdb != NULL);
  char *word = get_word(cdb, testword);
  char *expected = "ist";
  TEST_CHECK(strcmp(word, expected) == 0);
  TEST_MSG("Expected: %s", expected);
  TEST_MSG("Produced: %s", word);
}

void test_collocation_scores() {
  COLLOCATORDB* cdb = open_collocatordb(dbpath);
  TEST_ASSERT(cdb != NULL);
  char *expected = " { \"f1\": 217,\"w1\":\"Aluminium\", \"N\": 152743, \"collocates\": [{\"word\":\"Anwendungstechnologie\",\"f2\":16,\"f\":16,\"npmi\":0.594849,\"pmi\":8.4592,\"llr\":188.227,\"lfmd\":16.4592,\"md\":12.4592,\"md_nws\":10.1373,\"dice\":0.0711111,\"ld\":10.1862,\"ln_count\":16,\"rn_count\":0,\"ln_pmi\":9.4592,\"rn_pmi\":-1,\"ldaf\":11.1358,\"win\":32,\"afwin\":32}]}\n";
  char *produced = get_collocation_scores_as_json(cdb, 62, 966);
  TEST_CHECK(strcmp(produced, expected) == 0);
  TEST_MSG("Expected: %s", expected);
  TEST_MSG("Produced: %s", produced);
}


void test_collocation_analysis_as_json() {
  COLLOCATORDB* cdb = open_collocatordb(dbpath);
  TEST_ASSERT(cdb != NULL);
  char *json = get_collocators_as_json(cdb, testword);
  char *needle = "\"word\":\"um\",\"f2\":264,\"f\":5,\"npmi\":-0.0556349,\"pmi\":-0.958074,\"llr\":2.87723,\"lfmd\":3.68578,\"md\":1.36385,\"md_nws\":0.363854,\"dice\":0.00169952,\"ld\":4.79935,\"ln_count\":0,\"rn_count\":1,\"ln_pmi\":-1,\"rn_pmi\":-1,\"ldaf\":4.79935,\"win\":668,\"afwin\":668";
  TEST_CHECK(strstr(json, needle) > 0);
  TEST_MSG("Expected to contain: %s", needle);
}

void test_collocation_analysis() {
  COLLOCATORDB* cdb = open_collocatordb(dbpath);
  TEST_ASSERT(cdb != NULL);
  char *expected = "Anwendungstechnologie";
  const COLLOCATOR *c = get_collocators(cdb, 62);
  char *produced = get_word(cdb,c[0].w2);
  TEST_CHECK(strcmp(produced, expected) == 0);
  TEST_MSG("Expected: %s", expected);
  TEST_MSG("Produced: %s", produced);
}

int unlink_cb(const char *fpath, const struct stat *sb, int typeflag, struct FTW *ftwbuf) {
  int rv = remove(fpath);
  if (rv)
    perror(fpath);
  return rv;
}

int rmrf(char *path) {
  return nftw(path, unlink_cb, 64, FTW_DEPTH | FTW_PHYS);
}

void test_writing() {
  char tmp_template[] = "/tmp/tmpfileXXXXXX";
  int fd = mkstemp(tmp_template);
  if (fd == -1) {
    perror("mkstemp");
    exit(EXIT_FAILURE);
  }
  close(fd);
  char *tmp = strdup(tmp_template);

  long size = 0;
  int i;

  char *rocksdbfn = malloc(strlen(tmp) + strlen(".rocksdb") + 1);
  strcpy(rocksdbfn, tmp);
  strcat(rocksdbfn, ".rocksdb");
  COLLOCATORDB *cdb = open_collocatordb_for_write(rocksdbfn);

  char *vocabfn = malloc(strlen(tmp) + strlen(".vocab") + 1);
  strcpy(vocabfn, tmp);
  strcat(vocabfn, ".vocab");
  FILE *h = fopen(vocabfn, "w");
  fprintf(h, "word0 2000\n");
  fprintf(h, "word1 2000\n");
  fprintf(h, "word2 2000\n");
  fclose(h);
  read_vocab(cdb, vocabfn);
  inc_collocator(cdb, 0, 1, 4); size++;
  for (i = 0; i < 1000; i++) {
    inc_collocator(cdb, 0, 1, i % 5); size++;
    inc_collocator(cdb, 0, 1, -i % 5); size++;
    inc_collocator(cdb, 1, 0, i % 5); size++;
    inc_collocator(cdb, 1, 0, -i % 5); size++;
    inc_collocator(cdb, 0, 2, i % 5); size++;
    inc_collocator(cdb, 0, 2, -i % 5); size++;
  }
  inc_collocator(cdb, 1, 2, 4); size++;
  COLLOCATOR *c = get_collocators(cdb, 0);
  TEST_ASSERT(c != NULL);
  TEST_CHECK(c[0].w2 == 1);
  TEST_CHECK(c[0].raw == 2001);
  TEST_CHECK(c[0].left_raw == 200);
  TEST_CHECK(c[0].right_raw == 200);

  rmrf(rocksdbfn);
}

void test_version_function() {
  char *version = get_version();
  TEST_CHECK(strcmp(version, "1.3.2") == 0);
  TEST_MSG("Unexpected version: %s", version);
}

void test_get_word_id() {
  COLLOCATORDB* cdb = open_collocatordb(dbpath);
  TEST_ASSERT(cdb != NULL);
  uint64_t id = get_word_id(cdb, "ist");
  TEST_CHECK(id == 10);
  TEST_MSG("Unexpected word id: %lu", id);
}

TEST_LIST = {
    { "open database for reading", test_open_db },
    { "get word", test_get_word },
    { "collocation scores", test_collocation_scores },
    { "collocation analysis", test_collocation_analysis },
    { "collocation analysis as json", test_collocation_analysis_as_json },
    { "writing", test_writing },
    { "version function", test_version_function },
    { "get word id", test_get_word_id },
    { NULL, NULL }
};
