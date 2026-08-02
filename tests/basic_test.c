#include <stddef.h>
#include <stdio.h>
#include <string.h>
#define __USE_XOPEN_EXTENDED
#include <ftw.h>
#include "../src/collocatordb.h"
#include "acutest.h"
#include "config.h"

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
  /* Both collocates of word0 have to be there, in whatever order they are
     sorted into: word1 with 2001 and word2 with 2000. The one that came last
     used to be dropped, which is why this only asked for the first one. */
  int n, seen1 = 0, seen2 = 0;
  for (n = 0; c[n].raw > 0; n++) {
    if (c[n].w2 == 1) {
      seen1 = 1;
      TEST_CHECK(c[n].raw == 2001);
      TEST_CHECK(c[n].left_raw == 200);
      TEST_CHECK(c[n].right_raw == 200);
    } else if (c[n].w2 == 2) {
      seen2 = 1;
      TEST_CHECK(c[n].raw == 2000);
    }
  }
  TEST_CHECK(n == 2);
  TEST_MSG("expected 2 collocates, got %d", n);
  TEST_CHECK(seen1 && seen2);

  rmrf(rocksdbfn);
}

/* Every increment has to end up in the database, also when rocksdb would
   rather cancel the write because compaction is behind, and also when they are
   still in memory when the process that wrote them is gone. Both used to lose
   counts silently: a quarter of them under write pressure, and everything that
   had not been flushed when the indexer ended. */
void test_no_increment_is_lost() {
  char tmp_template[] = "/tmp/tmpfileXXXXXX";
  int fd = mkstemp(tmp_template);
  if (fd == -1) {
    perror("mkstemp");
    exit(EXIT_FAILURE);
  }
  close(fd);
  char *tmp = strdup(tmp_template);
  char rocksdbfn[1024], vocabfn[1024];
  const long increments = 200000;
  const int collocates = 100;
  long i, total = 0;

  snprintf(rocksdbfn, sizeof(rocksdbfn), "%s.rocksdb", tmp);
  snprintf(vocabfn, sizeof(vocabfn), "%s.vocab", tmp);
  FILE *h = fopen(vocabfn, "w");
  for (i = 0; i <= collocates + 1; i++)
    fprintf(h, "word%ld 1000\n", i);
  fclose(h);

  /* small write buffers, so that the writer runs into flushes and stalls */
  setenv("COLLOCATORDB_WRITE_BUFFER_MB", "1", 1);
  setenv("COLLOCATORDB_WRITE_BUFFERS", "2", 1);

  COLLOCATORDB *cdb = open_collocatordb_for_write(rocksdbfn);
  TEST_ASSERT(cdb != NULL);
  for (i = 0; i < increments; i++)
    inc_collocator(cdb, 1, 2 + (i % collocates), 1);   /* never w2 == w1 */
  close_collocatordb(cdb);      /* has to write what is still in memory */

  unsetenv("COLLOCATORDB_WRITE_BUFFER_MB");
  unsetenv("COLLOCATORDB_WRITE_BUFFERS");

  cdb = open_collocatordb(tmp);
  TEST_ASSERT(cdb != NULL);
  COLLOCATOR *c = get_collocators(cdb, 1);
  TEST_ASSERT(c != NULL);
  for (i = 0; i < collocates && c[i].raw > 0; i++)
    total += c[i].raw;
  TEST_CHECK(i == collocates);
  TEST_MSG("only %ld of %d collocates are in the database", i, collocates);
  TEST_CHECK(total == increments);
  TEST_MSG("%ld of %ld increments survived", total, increments);

  rmrf(rocksdbfn);
}

void test_version_function() {
  char *version = get_version();
  /* against the version of the build, so that a version bump does not have to
     be repeated here */
  TEST_CHECK(strcmp(version, PROJECT_VERSION) == 0);
  TEST_MSG("Unexpected version: %s, expected %s", version, PROJECT_VERSION);
}

void test_get_word_id() {
  COLLOCATORDB* cdb = open_collocatordb(dbpath);
  TEST_ASSERT(cdb != NULL);
  uint64_t id = get_word_id(cdb, "ist");
  TEST_CHECK(id == 10);
  TEST_MSG("Unexpected word id: %lu", id);
}

void test_collocatordb_query_command_line_tool() {
  int result = system("../build/collocatordb_query ../tests/data/wpd19_10000 ist > /dev/null 2>&1");
  TEST_CHECK(result == 0);
  TEST_MSG("collectordb_query command failed with result: %d", result);
}

void test_get_corpus_size() {
  COLLOCATORDB* cdb = open_collocatordb(dbpath);
  TEST_ASSERT(cdb != NULL);
  uint64_t size = get_corpus_size(cdb);
  TEST_CHECK(size == 152743);
  TEST_MSG("Unexpected corpus size: %lu", size);
}

void test_get_word_frequency() {
  COLLOCATORDB* cdb = open_collocatordb(dbpath);
  TEST_ASSERT(cdb != NULL);
  int w1 = get_word_id(cdb, "Test");
  uint64_t freq = get_word_frequency(cdb, w1);
  TEST_CHECK(freq == 3);
  TEST_MSG("Unexpected word frequency: %lu", freq);
}

TEST_LIST = {
    { "open database for reading", test_open_db },
    { "get word", test_get_word },
    { "collocation scores", test_collocation_scores },
    { "collocation analysis", test_collocation_analysis },
    { "collocation analysis as json", test_collocation_analysis_as_json },
    { "writing", test_writing },
    { "no increment is lost", test_no_increment_is_lost },
    { "version function", test_version_function },
    { "get word id", test_get_word_id },
    { "get corpus size", test_get_corpus_size},
    { "get word frequency", test_get_word_frequency},
    { "collocatordb_query command line tool", test_collocatordb_query_command_line_tool},
    { NULL, NULL }
};
