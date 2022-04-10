# CollocatorDB: Storing and retrieving collocation counts based on [RocksDB](https://github.com/facebook/rocksdb)

## Installation

### Install RocksDB and prerequisites

* prerequisites on CentOS, FeDora, RHEL

    ```bash
    sudo yum install cmake3 snappy snappy-devel zlib zlib-devel bzip2 bzip2-devel lz4-devel libzstd-devel libomp-devel

    git clone https://github.com/gflags/gflags.git
    cd gflags
    git checkout v2.0
    ./configure && make && sudo make install
    cd ..
    ```

* prerequisites on Ubuntu, Debian

    ```bash
    sudo apt-get install cmake libgflags-dev libsnappy-dev zlib1g-dev libbz2-dev liblz4-dev libzstd-dev libomp-dev
    ```

* install [RocksDB v5.11.3](https://github.com/facebook/rocksdb/releases/tag/v5.11.3)

    ```bash
    curl -L https://github.com/facebook/rocksdb/archive/refs/tags/v5.11.3.tar.gz | tar zx
    cd rocksdb-5.11.3
    make static_lib DISABLE_WARNING_AS_ERROR=1 && sudo make install-static DISABLE_WARNING_AS_ERROR=1
    make shared_lib DISABLE_WARNING_AS_ERROR=1 && sudo make install-shared DISABLE_WARNING_AS_ERROR=1
    ldconfig
    ```

### Install CollocatorDB

```bash
git clone "https://korap.ids-mannheim.de/gerrit/private/collocatordb"
cd collocatordb
mkdir -p build
cd build
cmake -DCMAKE_INSTALL_PREFIX=/usr/local ..
make && ctest --extra-verbose && sudo make install
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
void inc_collocator(COLLOCATORDB *db, uint64_t w1, uint64_t w2, int8_t dist);
void dump_collocators(COLLOCATORDB *db, uint32_t w1, uint32_t w2, int8_t dist);
COLLOCATOR *get_collocators(COLLOCATORDB *db, uint32_t w1);
COLLOCATOR *get_collocation_scores(COLLOCATORDB *db, uint32_t w1, uint32_t w2);
char *get_collocators_as_json(COLLOCATORDB *db, uint32_t w1);
char *get_collocation_scores_as_json(COLLOCATORDB *db, uint32_t w1, uint32_t w2);
char *get_word(COLLOCATORDB *db, uint32_t w1);
void read_vocab(COLLOCATORDB *db, char *fname);
```

## TODO

* extend API
* add more unit tests

## License

Based on RocksDB, CollocatorDB is dual-licensed under both the GPLv2 (found in the COPYING file in the root directory) and Apache 2.0 License (found in the LICENSE.Apache file in the root directory).  You may select, at your option, one of the above-listed licenses.
