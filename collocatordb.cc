//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under both the GPLv2 (found in the
//  COPYING file in the root directory) and Apache 2.0 License
//  (found in the LICENSE.Apache file in the root directory).
//
#include <typeinfo>
#include <assert.h>
#include <memory>
#include <iostream>
#include <stdint.h>
#include "rocksdb/cache.h"
#include "rocksdb/comparator.h"
#include "rocksdb/db.h"
#include "rocksdb/env.h"
#include <rocksdb/merge_operator.h>
#include "rocksdb/utilities/db_ttl.h"
#include "merge_operators.h"

#define IS_BIG_ENDIAN (*(uint16_t *)"\0\xff" < 0x100)
#define encodeCollocation(w1, w2, dist) (((uint64_t)dist << 56) | ((uint64_t)w2 << 24) | w1)
using namespace rocksdb;

namespace {
  size_t num_merge_operator_calls;
  void resetNumMergeOperatorCalls() { num_merge_operator_calls = 0; }
  
  size_t num_partial_merge_calls;
  void resetNumPartialMergeCalls() { num_partial_merge_calls = 0; }
}

inline void EncodeFixed64(char* buf, uint64_t value) {
  if (! IS_BIG_ENDIAN) {
    memcpy(buf, &value, sizeof(value));
  } else {
    buf[0] = value & 0xff;
    buf[1] = (value >> 8) & 0xff;
    buf[2] = (value >> 16) & 0xff;
    buf[3] = (value >> 24) & 0xff;
    buf[4] = (value >> 32) & 0xff;
    buf[5] = (value >> 40) & 0xff;
    buf[6] = (value >> 48) & 0xff;
    buf[7] = (value >> 56) & 0xff;
  }
}

inline uint32_t DecodeFixed32(const char* ptr) {
  if (! IS_BIG_ENDIAN) {
    // Load the raw bytes
    uint32_t result;
    memcpy(&result, ptr, sizeof(result));  // gcc optimizes this to a plain load
    return result;
  } else {
    return ((static_cast<uint32_t>(static_cast<unsigned char>(ptr[0])))
            | (static_cast<uint32_t>(static_cast<unsigned char>(ptr[1])) << 8)
            | (static_cast<uint32_t>(static_cast<unsigned char>(ptr[2])) << 16)
            | (static_cast<uint32_t>(static_cast<unsigned char>(ptr[3])) << 24));
  }
}

inline uint64_t DecodeFixed64(const char* ptr) {
  if (! IS_BIG_ENDIAN) {
    // Load the raw bytes
    uint64_t result;
    memcpy(&result, ptr, sizeof(result));  // gcc optimizes this to a plain load
    return result;
  } else {
    uint64_t lo = DecodeFixed32(ptr);
    uint64_t hi = DecodeFixed32(ptr + 4);
    return (hi << 32) | lo;
  }
}


class CountMergeOperator : public AssociativeMergeOperator {
public:
  CountMergeOperator() {
    mergeOperator_ = MergeOperators::CreateUInt64AddOperator();
  }

  virtual bool Merge(const Slice& key,
                     const Slice* existing_value,
                     const Slice& value,
                     std::string* new_value,
                     Logger* logger) const override {
    assert(new_value->empty());
    ++num_merge_operator_calls;
    if (existing_value == nullptr) {
      new_value->assign(value.data(), value.size());
      return true;
    }

    return mergeOperator_->PartialMerge(
                                        key,
                                        *existing_value,
                                        value,
                                        new_value,
                                        logger);
  }

  virtual const char* Name() const override {
    return "UInt64AddOperator";
  }

private:
  std::shared_ptr<MergeOperator> mergeOperator_;
};

namespace {
}  // namespace

class CollocatorIterator : public Iterator {
public:
  explicit  CollocatorIterator() {
  }
};

class Collocators {
private:
  WriteOptions merge_option_; // for merge
  char _one[sizeof(uint64_t)];
  Slice _one_slice;
  
protected:
  std::shared_ptr<DB> db_;

  WriteOptions put_option_;
  ReadOptions get_option_;
  WriteOptions delete_option_;

  uint64_t default_;

  std::shared_ptr<DB> OpenDb(const std::string& dbname, const bool ttl = false,
                             const size_t max_successive_merges = 0) {
    DB* db;
    Options options;
    options.create_if_missing = true;
    options.merge_operator = std::make_shared<CountMergeOperator>();
    options.max_successive_merges = max_successive_merges;
    Status s;
    //  DestroyDB(dbname, Options());
    s = DB::Open(options, dbname, &db);
    if (!s.ok()) {
      std::cerr << s.ToString() << std::endl;
      assert(false);
    }
    return std::shared_ptr<DB>(db);
  }

public:
 explicit Collocators(const std::string& db_name)
   : put_option_(),
     get_option_(),
     delete_option_(),
     merge_option_() {
   db_ = OpenDb(db_name, false, 0);
   assert(db_);
   uint64_t one = 1;
   EncodeFixed64(_one, one);
   _one_slice = Slice(_one, sizeof(uint64_t));
  }

  virtual ~Collocators() {}

  // public interface of Collocators.
  // All four functions return false
  // if the underlying level db operation failed.

  // mapped to a levedb Put
  bool set(const std::string& key, uint64_t value) {
    // just treat the internal rep of int64 as the string
    char buf[sizeof(value)];
    EncodeFixed64(buf, value);
    Slice slice(buf, sizeof(value));
    auto s = db_->Put(put_option_, key, slice);

    if (s.ok()) {
      return true;
    } else {
      std::cerr << s.ToString() << std::endl;
      return false;
    }
  }

  DB *getDb() {
    return db_.get();
  }

  // mapped to a rocksdb Delete
  bool remove(const std::string& key) {
    auto s = db_->Delete(delete_option_, key);

    if (s.ok()) {
      return true;
    } else {
      std::cerr << s.ToString() << std::endl;
      return false;
    }
  }

  // mapped to a rocksdb Get
  bool get(const std::string& key, uint64_t* value) {
    std::string str;
    auto s = db_->Get(get_option_, key, &str);

    if (s.IsNotFound()) {
      // return default value if not found;
      *value = default_;
      return true;
    } else if (s.ok()) {
      // deserialization
      if (str.size() != sizeof(uint64_t)) {
        std::cerr << "value corruption\n";
        return false;
      }
      *value = DecodeFixed64(&str[0]);
      return true;
    } else {
      std::cerr << s.ToString() << std::endl;
      return false;
    }
  }


  uint64_t get(const uint32_t w1, const uint32_t w2, const int8_t dist) {
    char encoded_key[sizeof(uint64_t)];
    EncodeFixed64(encoded_key, encodeCollocation(w1,w2,dist));
    uint64_t value = default_;
    int result = get(std::string(encoded_key, 8), &value);
    return value;
  }

  virtual void inc(const std::string& key) {
    db_->Merge(merge_option_, key, _one_slice);
  }

  virtual void inc(const uint64_t key) {
    char encoded_key[sizeof(uint64_t)];
    EncodeFixed64(encoded_key, key);
    db_->Merge(merge_option_, std::string(encoded_key, 8), _one_slice);
  }

  virtual void inc(const uint32_t w1, const uint32_t w2, const uint8_t dist) {
    inc(encodeCollocation(w1, w2, dist));
  }

  // mapped to a rocksdb Merge operation
  virtual bool add(const std::string& key, uint64_t value) {
    char encoded[sizeof(uint64_t)];
    EncodeFixed64(encoded, value);
    Slice slice(encoded, sizeof(uint64_t));
    auto s = db_->Merge(merge_option_, key, slice);

    if (s.ok()) {
      return true;
    } else {
      std::cerr << s.ToString() << std::endl;
      return false;
    }
  }

};

namespace {
  void dumpDb(DB* db) {
    char prefixc[sizeof(uint64_t)];
    EncodeFixed64(prefixc, encodeCollocation(1000,0,0));
    ReadOptions options;
    options.prefix_same_as_start = true;  
    auto it = unique_ptr<Iterator>(db->NewIterator(options));

    for (it->Seek(std::string(prefixc,3)); it->Valid() && it->key().starts_with(std::string(prefixc,3)); it->Next()) {
      uint64_t value = DecodeFixed64(it->value().data());
      uint64_t key = DecodeFixed64(it->key().data());
      uint64_t w1 = (uint64_t)(key & 0xffffff);
      uint64_t w2 = (uint64_t)((key >> 24) & 0xffffff);
      int8_t dist = (uint64_t)((key >> 56) & 0xff);
      std::cout << "w1:" << w1 << ", w2:" << w2 << ", dist:" << (int32_t) dist << " - count:" << value << std::endl;
    }

  
    assert(it->status().ok());  // Check for any errors found during the scan
  }

  void testCollocators(Collocators& counters) {

    FlushOptions o;
    DB *db = counters.getDb();

    o.wait = true;

    counters.inc(100,200,5);
    counters.inc(1000,2000,-5);
    counters.inc(1000,2000,5);
    counters.inc(1000,2500,-3);
    counters.inc(1000,2500,4);
    counters.inc(1000,2900,3);

    counters.inc(1001,2900,3);

    for(int i=0; i<1000000; i++)
      counters.inc(rand()%1000,rand()%1000,5);

    //  dumpDb(db);

    counters.inc(100,200,5);
    counters.inc(1000,2000,5);
    counters.inc(1000,2500,4);
    counters.inc(1000,2900,3);

    counters.inc(1001,2900,3);

    dumpDb(db);
  }

  void runTest(int argc, const std::string& dbname, const bool use_ttl = false) {
    std::cout << "Test merge-based counters... \n";
    Collocators counters(dbname);
    testCollocators(counters);
  }
}  // namespace

int main(int argc, char *argv[]) {
  runTest(argc, "/tmp/merge_testdb");
  printf("Passed all tests!\n");
  return 0;
}
