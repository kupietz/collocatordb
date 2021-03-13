# CollocatorDB: Storing and retrieving collocation counts based on [RocksDB](https://github.com/facebook/rocksdb)

## Installation
### Install RocksdDB and prerequisites
* install prerequisites on CentOS, FeDora, RHEL
    ```
    sudo yum install snappy snappy-devel zlib zlib-devel bzip2 bzip2-devel lz4-devel libzstd-devel libomp-devel
    
    git clone https://github.com/gflags/gflags.git
    cd gflags
    git checkout v2.0
    ./configure && make && sudo make install
    cd ..
    ```
* install prerequisites on Ubuntu, Debian
    ```
    sudo apt-get install libgflags-dev libsnappy-dev zlib1g-dev libbz2-dev liblz4-dev libzstd-dev libomp-dev
    ```
* install RocksDBn [v5.11.3](https://github.com/facebook/rocksdb/releases/tag/v5.11.3)
    ```
    git clone https://github.com/facebook/rocksdb.git
    cd rocksdb
    git checkout v5.11.3
    make static_lib DISABLE_WARNING_AS_ERROR=1 && sudo make install-static DISABLE_WARNING_AS_ERROR=1
    make shared_lib DISABLE_WARNING_AS_ERROR=1 && sudo make install-shared DISABLE_WARNING_AS_ERROR=1
    ```
### Install CollocatorDB
```
make && make install
```
## Provided API
```
COLLOCATORDB *open_collocatordb(const char *path_to_rocksdb_without_extension);
COLLOCATORDB *open_collocatordb_for_write(const char *path_to_rocksdb_without_extension);
void inc_collocator(COLLOCATORDB *db, uint64_t w1, uint64_t w2, int8_t dist);
void dump_collocators(COLLOCATORDB *db, uint32_t w1, uint32_t w2, int8_t dist);
char *get_collocators_as_json(COLLOCATORDB *db, uint32_t w1);
char *get_collocation_scores_as_json(COLLOCATORDB *db, uint32_t w1, uint32_t w2);
char *get_word(COLLOCATORDB *db, uint32_t w1);
```

## TODO
* extend APIs
* add unit and ci tests
* improve build process

## License

Based on RocksDB, CollocatorDB is dual-licensed under both the GPLv2 (found in the COPYING file in the root directory) and Apache 2.0 License (found in the LICENSE.Apache file in the root directory).  You may select, at your option, one of the above-listed licenses.
