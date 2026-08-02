# CollocatorDB: Storing and retrieving collocation counts based on [RocksDB](https://github.com/facebook/rocksdb)

## Installation

### Install RocksDB and prerequisites

The rocksdb of the distribution is used, no particular version is required.

* on Fedora, Rocky Linux, RHEL

    ```bash
    sudo dnf install cmake gcc-c++ rocksdb-devel snappy-devel zlib-devel bzip2-devel lz4-devel libzstd-devel gflags-devel
    ```

* on Ubuntu, Debian

    ```bash
    sudo apt-get install cmake librocksdb-dev libgflags-dev libsnappy-dev zlib1g-dev libbz2-dev liblz4-dev libzstd-dev libomp-dev
    ```

* on MacOS

    ```bash
    brew install cmake rocksdb snappy zlib bzip2 lz4 zstd libomp gflags
    ```

Do not install another rocksdb below `/usr/local` next to the packaged one. The
compiler looks into `/usr/local/include` before `/usr/include`, so its headers
would be used while the library of the distribution is linked, which ends in a
long list of undefined references. The build compares the version of the
headers with the version of the library and stops with an explanation when they
do not match.

### Install CollocatorDB

```bash
git clone "https://korap.ids-mannheim.de/gerrit/ids-kl/collocatordb"
cd collocatordb
mkdir -p build
cd build
cmake -DCMAKE_INSTALL_PREFIX=/usr/local ..
make && sudo make install && sudo ldconfig
ctest --extra-verbose
```

The tests run after the installation, not before: one of them calls the
`collocatordb_query` tool, which is built with the rpath of its install
location and does not find the library as long as it is not installed. The
build directory has to be `build` inside the sources, the tests look for the
tool and for their data relative to it.

### Static library

`libcollocatordb_static.a` is built as well. It is linked against the shared
rocksdb unless a static rocksdb is found, which is enough for most purposes.

Linking rocksdb and collocatordb statically into a program is worth about 22%
when counting collocations and about 10% when looking them up, measured with
rocksdb 7.8.3, which ships both variants. The compression libraries stay
shared, so this does not need static versions of zlib, snappy and the others,
which several distributions do not have. Only a completely static binary needs
those, which is something else and rarely necessary.

Debian and Ubuntu ship `librocksdb.a` in `librocksdb-dev`, so there is nothing
to do. Fedora, Rocky Linux and RHEL do not ship one, so it has to be built.
Keep it out of `/usr/local`, where its headers would shadow those of the
package, and point the build at it:

Build the same version as the rocksdb that collocatordb is compiled against,
otherwise the static library does not match the headers:

```bash
git clone https://github.com/facebook/rocksdb.git -b v$(rpm -q --qf '%{VERSION}' rocksdb) --single-branch
cmake -S rocksdb -B rocksdb/build -DCMAKE_BUILD_TYPE=Release \
      -DFAIL_ON_WARNINGS=OFF \
      -DWITH_TESTS=OFF -DWITH_BENCHMARK_TOOLS=OFF -DWITH_TOOLS=OFF -DWITH_CORE_TOOLS=OFF \
      -DROCKSDB_BUILD_SHARED=OFF \
      -DCMAKE_INSTALL_PREFIX=$HOME/rocksdb-static
cmake --build rocksdb/build -j $(nproc)
cmake --install rocksdb/build
```

and then, in the collocatordb sources:

```bash
cmake -S . -B build -DROCKSDB_STATIC=$HOME/rocksdb-static/lib/librocksdb.a \
      -DCMAKE_INSTALL_PREFIX=/usr/local
cmake --build build -j $(nproc)
sudo cmake --install build && sudo ldconfig
```

`WITH_TESTS` and the tools bring in the bundled gtest, which is not needed for
a static library and does not compile with every compiler. `FAIL_ON_WARNINGS`
adds `-Werror`, which turns warnings of newer compilers about rocksdb's own
code into errors - on Rocky Linux with gcc 11, for instance:

```
env/env.cc:688: error: 'hostname_buf' may be used uninitialized [-Werror=maybe-uninitialized]
```

## Provided API

```c
typedef struct {
  uint32_t w2;
  uint64_t f2;
  uint64_t raw;
  double pmi;
  double npmi;
  double llr;
  double lfmd;
  double md;
  double md_nws;
  uint64_t left_raw;
  uint64_t right_raw;
  double left_pmi;
  double right_pmi;
  double dice;
  double logdice;
  double ldaf;
  int window;
  int af_window;
} COLLOCATOR ;

COLLOCATORDB *open_collocatordb(const char *s);
COLLOCATORDB *open_collocatordb_for_write(const char *s);
void close_collocatordb(COLLOCATORDB *db);
void inc_collocator(COLLOCATORDB *db, uint64_t w1, uint64_t w2, int8_t dist);
void dump_collocators(COLLOCATORDB *db, uint32_t w1, uint32_t w2, int8_t dist);
COLLOCATOR *get_collocators(COLLOCATORDB *db, uint32_t w1);
COLLOCATOR *get_collocation_scores(COLLOCATORDB *db, uint32_t w1, uint32_t w2);
char *get_collocators_as_json(COLLOCATORDB *db, uint32_t w1);
char *get_collocation_scores_as_json(COLLOCATORDB *db, uint32_t w1, uint32_t w2);
char *get_word(COLLOCATORDB *db, uint32_t w1);
void read_vocab(COLLOCATORDB *db, char *fname);
char *get_version();
uint64_t get_word_id(COLLOCATORDB *db, const char *word);
uint64_t get_corpus_size(COLLOCATORDB *db);
uint64_t get_word_frequency(COLLOCATORDB *db, uint64_t w);
```

## Indexing

Building a collocation database is one long stream of increments for the same
keys. The defaults are chosen for that, and can be adjusted from the
environment, so that a run of several days does not have to be recompiled to be
tuned:

| variable | default | meaning |
|---|---|---|
| `COLLOCATORDB_WRITE_BUFFER_MB` | 256 | size of one write buffer |
| `COLLOCATORDB_WRITE_BUFFERS` | 8 | how many of them |
| `COLLOCATORDB_WRITE_BUFFERS_TO_MERGE` | 4 | how many are combined before they are written, which collapses the increments of the same key early |
| `COLLOCATORDB_BACKGROUND_JOBS` | cores, at most 32 | threads for flushing and compaction |
| `COLLOCATORDB_SUBCOMPACTIONS` | 4 | how far a single compaction is spread over threads |
| `COLLOCATORDB_BLOCK_CACHE_MB` | 512 | block cache, only relevant for reading |

Increments are inserted one writer at a time, rocksdb does not support
concurrent memtable writes for merge operations, so more indexing threads stop
helping at some point regardless of these settings.

The database has no write ahead log, so an indexer has to call
`close_collocatordb()` when it is done, otherwise everything that has not been
flushed is lost.

## Changes

* v1.6.0 (2026-08-02)
  * fixed the loss of increments: rocksdb was told to cancel a write instead of
    waiting when compaction is behind, and the result was not looked at. Under
    write pressure a quarter of the increments were lost without any message.
    A cancelled write is repeated now
  * fixed the loss of everything that was not flushed yet when an indexer ends:
    added `close_collocatordb()`, which writes it and closes the database
  * fixed `get_collocators()` and `get_collocation_scores()` dropping the last
    collocate of every word
  * fixed the array returned by `get_collocators()` and
    `get_collocation_scores()` being far too small, `+` instead of `*` in the
    size. It is terminated with an empty entry now
  * the settings for indexing can be adjusted from the environment, see above

* v1.5.0 (2026-07-31)
  * fixed two memory leaks in the collocator lookup, a rocksdb iterator and the
    window sums, together 3.6 kB per call
  * builds against the rocksdb of the distribution, no rocksdb 5.11 from source
    anymore. Verified with 7.8 (Debian), 10.2 (Fedora) and 11.0 (Alpine);
    existing databases stay readable
  * needs C++17, and C++20 for rocksdb 11 and newer
  * added `get_corpus_size()`, which returns the total token count
  * added `get_word_frequency()`, which returns the absolute frequency of a word in the corpus

* v1.4.0 (2024-11-23)
  * added `collocatordb_query` command line tool
  * added `get_word_id()`, which returns the ID of a word
  * added `md_nws` MI² score based on nominal window size (=10) instead of actual window size, for which only positions are counted where the collocate actually occurs

* v1.3.2 (2024-11-15)
  * added `get_version()`, which returns version string

* v1.3.1 (2024-11-14)
  * fixed calculation of total token count

## TODO

* extend API
* add more unit tests

## License

Based on RocksDB, CollocatorDB is dual-licensed under both the GPLv2 (found in the COPYING file in the root directory) and Apache 2.0 License (found in the LICENSE.Apache file in the root directory).  You may select, at your option, one of the above-listed licenses.
