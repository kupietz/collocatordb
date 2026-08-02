#define EXPORT __attribute__((visibility("visible")))
#define IMPORT

#include "config.h"
#include "export.h"
#include "merge_operators.h"
#include "rocksdb/db.h"
#include "rocksdb/env.h"
#include "rocksdb/table.h"
#include "rocksdb/slice.h"
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <rocksdb/merge_operator.h>
#include <rocksdb/slice_transform.h>
#include <rocksdb/version.h>
#include <sstream> // for ostringstream
#include <string>
#include <thread>
#include <utility>
#include <vector>

/* Since rocksdb 11 DB::Open() and DB::OpenForReadOnly() hand the database back
   as a unique_ptr instead of a raw pointer. */
#if ROCKSDB_MAJOR >= 11
#define ROCKSDB_DB_HANDLE std::unique_ptr<rocksdb::DB>
#define ROCKSDB_DB_RELEASE(handle) (handle).release()
#else
#define ROCKSDB_DB_HANDLE rocksdb::DB *
#define ROCKSDB_DB_RELEASE(handle) (handle)
#endif

#define WINDOW_SIZE 5
#define FREQUENCY_THRESHOLD 5
#define IS_BIG_ENDIAN (*(uint16_t *)"\0\xff" < 0x100)
#define encodeCollocation(w1, w2, dist)                                        \
  (((uint64_t)dist << 56) | ((uint64_t)w2 << 24) | w1)
#define W1(key) (uint64_t)(key & 0xffffff)
#define W2(key) (uint64_t)((key >> 24) & 0xffffff)
#define DIST(key) (int8_t)((uint64_t)((key >> 56) & 0xff))

typedef struct {
  uint64_t freq;
  char *word;
} vocab_entry;

// typedef struct Collocator {
//   uint64_t w2;
//   uint64_t sum;
// };

using namespace rocksdb;
using namespace std;

namespace rocksdb {
class Collocator {
public:
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
};

size_t num_merge_operator_calls;

void resetNumMergeOperatorCalls() { num_merge_operator_calls = 0; }

size_t num_partial_merge_calls;

void resetNumPartialMergeCalls() { num_partial_merge_calls = 0; }

inline void EncodeFixed64(char *buf, uint64_t value) {
  if (!IS_BIG_ENDIAN) {
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

inline uint32_t DecodeFixed32(const char *ptr) {
  if (!IS_BIG_ENDIAN) {
    // Load the raw bytes
    uint32_t result;
    memcpy(&result, ptr, sizeof(result)); // gcc optimizes this to a plain load
    return result;
  } else {
    return ((static_cast<uint32_t>(static_cast<unsigned char>(ptr[0]))) |
            (static_cast<uint32_t>(static_cast<unsigned char>(ptr[1])) << 8) |
            (static_cast<uint32_t>(static_cast<unsigned char>(ptr[2])) << 16) |
            (static_cast<uint32_t>(static_cast<unsigned char>(ptr[3])) << 24));
  }
}

inline uint64_t DecodeFixed64(const char *ptr) {
  if (!IS_BIG_ENDIAN) {
    // Load the raw bytes
    uint64_t result;
    memcpy(&result, ptr, sizeof(result)); // gcc optimizes this to a plain load
    return result;
  } else {
    uint64_t lo = DecodeFixed32(ptr);
    uint64_t hi = DecodeFixed32(ptr + 4);
    return (hi << 32) | lo;
  }
}

static inline double ca_pmi(uint64_t f1, uint64_t f2, uint64_t f12,
                            uint64_t total, double window_size) {
  double r1 = f1 * window_size, c1 = f2, e = r1 * c1 / total, o = f12;
  if (f12 < FREQUENCY_THRESHOLD)
    return -1.0;
  else
    return log2(o / e);
}

// Bouma, Gerlof (2009): <a
// href="https://svn.spraakdata.gu.se/repos/gerlof/pub/www/Docs/npmi-pfd.pdf">
// Normalized (pointwise) mutual information in collocation extraction</a>. In
// Proceedings of GSCL.
static double ca_npmi(uint64_t f1, uint64_t f2, uint64_t f12,
                             uint64_t total, double window_size) {
  double r1 = f1 * window_size, c1 = f2, e = r1 * c1 / total, o = f12;
  if (f12 < FREQUENCY_THRESHOLD)
    return -1.0;
  else
    return log2(o / e) / (-log2(o / total / window_size));
}

// Thanopoulos, A., Fakotakis, N., Kokkinakis, G.: Comparative evaluation of
// collocation extraction metrics. In: International Conference on Language
// Resources and Evaluation (LREC-2002). (2002) 620–625 double md =
// log2(pow((double)max * window_size / total, 2) /  (window_size *
// ((double)_vocab[w1].freq/total) * ((double)_vocab[last_w2].freq/total)));
static double ca_md(uint64_t f1, uint64_t f2, uint64_t f12,
                           uint64_t total, double window_size) {
  const double r1 = f1 * window_size;
  const double c1 = f2;
  const double e = r1 * c1 / total;
  const double o = f12;
  return log2(o * o / e);
}

static double ca_lfmd(uint64_t f1, uint64_t f2, uint64_t f12,
                             uint64_t total, double window_size) {
  double r1 = f1 * window_size, c1 = f2, e = r1 * c1 / total, o = f12;
  if (f12 == 0)
    return 0;
  return log2(o * o * o / e);
}

// Evert, Stefan (2004): The Statistics of Word Cooccurrences: Word Pairs and
// Collocations. PhD dissertation, IMS, University of Stuttgart. Published in
// 2005, URN urn:nbn:de:bsz:93-opus-23714. Free PDF available from
// http://purl.org/stefan.evert/PUB/Evert2004phd.pdf
static double ca_ll(uint64_t w1, uint64_t w2, uint64_t w12, uint64_t n,
                           uint64_t window_size) {
  double r1 = (double)w1 * window_size, r2 = (double)n - r1, c1 = w2,
         c2 = n - c1, o11 = w12, o12 = r1 - o11, o21 = c1 - w12, o22 = r2 - o21,
         e11 = r1 * c1 / n, e12 = r1 * c2 / n, e21 = r2 * c1 / n,
         e22 = r2 * c2 / n;
  return (2 * ((o11 > 0 ? o11 * log(o11 / e11) : 0) +
               (o12 > 0 ? o12 * log(o12 / e12) : 0) +
               (o21 > 0 ? o21 * log(o21 / e21) : 0) +
               (o22 > 0 ? o22 * log(o22 / e22) : 0)));
}

static double ca_dice(uint64_t w1, uint64_t w2, uint64_t w12, uint64_t n,
                             uint64_t window_size) {
  double r1 = (double)w1 * window_size, c1 = w2;
  return 2 * w12 / (c1 + r1);
}

// Rychlý, Pavel (2008): <a
// href="http://www.fi.muni.cz/usr/sojka/download/raslan2008/13.pdf">A
// lexicographer-friendly association score.</a> In Proceedings of Recent
// Advances in Slavonic Natural Language Processing, RASLAN, 6–9.
static double ca_logdice(uint64_t w1, uint64_t w2, uint64_t w12,
                                uint64_t n, uint64_t window_size) {
  double r1 = (double)w1 * window_size, c1 = w2;
  return 14 + log2(2 * w12 / (c1 + r1));
}

class CountMergeOperator : public AssociativeMergeOperator {
public:
  CountMergeOperator() {
    mergeOperator_ = MergeOperators::CreateUInt64AddOperator();
  }

  bool Merge(const Slice &key, const Slice *existing_value,
                     const Slice &value, std::string *new_value,
                     Logger *logger) const override {
    assert(new_value->empty());
    ++num_merge_operator_calls;
    if (existing_value == nullptr) {
      new_value->assign(value.data(), value.size());
      return true;
    }

    return mergeOperator_->PartialMerge(key, *existing_value, value, new_value,
                                        logger);
  }

  const char *Name() const override { return "UInt64AddOperator"; }

private:
  std::shared_ptr<MergeOperator> mergeOperator_;
};

class CollocatorIterator : public Iterator {
  char prefixc[sizeof(uint64_t)]{};
  Iterator *base_iterator_;

public:
  explicit CollocatorIterator(Iterator *base_iterator) : base_iterator_(base_iterator) {}

  /* Takes ownership of the iterator handed in by SeekIterator(). Without this
     every seek leaked a rocksdb iterator and the resources it pins. */
  ~CollocatorIterator() override { delete base_iterator_; }

  void setPrefix(char *prefix) { memcpy(prefixc, prefix, sizeof(uint64_t)); }

  void SeekToFirst() override { base_iterator_->SeekToFirst(); }

  void SeekToLast() override { base_iterator_->SeekToLast(); }

  void Seek(const rocksdb::Slice &s) override { base_iterator_->Seek(s); }

  void SeekForPrev(const rocksdb::Slice &s) override {
    base_iterator_->SeekForPrev(s);
  }

  void Prev() override { base_iterator_->Prev(); }

  void Next() override { base_iterator_->Next(); }

  Slice key() const override;

  Slice value() const override;

  Status status() const override;

  bool Valid() const override;

  bool isValid();

  uint64_t intValue();

  uint64_t intKey();
};

//  rocksdb::CollocatorIterator::CollocatorIterator(Iterator* base_iterator) {}

bool CollocatorIterator::Valid() const {
  return base_iterator_->Valid() && key().starts_with(std::string(prefixc, 3));
}

bool CollocatorIterator::isValid() {
  return base_iterator_->Valid() && key().starts_with(std::string(prefixc, 3));
  // return key().starts_with(std::string(prefixc,3));
}

uint64_t CollocatorIterator::intKey() {
  return DecodeFixed64(base_iterator_->key().data());
}

uint64_t CollocatorIterator::intValue() {
  return DecodeFixed64(base_iterator_->value().data());
}

class VocabEntry {
public:
  string word;
  uint64_t freq;
};

class CollocatorDB {
  WriteOptions merge_option_; // for merge
  // to repeat a write that rocksdb cancelled, rather than lose the count
  WriteOptions blocking_merge_option_;
  std::atomic<uint64_t> stalled_writes_{0};
  std::atomic<uint64_t> failed_writes_{0};
  char _one[sizeof(uint64_t)]{};
  Slice _one_slice;
  vector<VocabEntry> _vocab;
  uint64_t total = 0;
  uint64_t sentences = 0;
  float avg_window_size = 8.0;

protected:
  std::shared_ptr<DB> db_;

  WriteOptions put_option_;
  ReadOptions get_option_;
  WriteOptions delete_option_;

  uint64_t default_{};

  std::shared_ptr<DB> OpenDb(const char *dbname);

  std::shared_ptr<DB> OpenDbForRead(const char *dbname);

public:
  virtual ~CollocatorDB();  // flushes what is still in memory, see close()
  void readVocab(const string& fname);
  string getWord(uint32_t w1);

  uint64_t getWordId(const char *word) const;

  uint64_t getCorpusSize() const;

  uint64_t getWordFrequency(uint64_t w1);

  CollocatorDB(const char *db_name, bool read_only);

  // public interface of CollocatorDB.
  // All four functions return false
  // if the underlying level db operation failed.

  // mapped to a levedb Put
  bool set(const std::string &key, uint64_t value) {
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

  DB *getDb() { return db_.get(); }

  // mapped to a rocksdb Delete
  bool remove(const std::string &key) {
    auto s = db_->Delete(delete_option_, key);

    if (s.ok()) {
      return true;
    } else {
      std::cerr << s.ToString() << std::endl;
      return false;
    }
  }

  // mapped to a rocksdb Get
  bool get(const std::string &key, uint64_t *value) {
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
    EncodeFixed64(encoded_key, encodeCollocation(w1, w2, dist));
    uint64_t value = default_;
    get(std::string(encoded_key, 8), &value);
    return value;
  }

  /* A merge that does not lose the count. rocksdb cancels a write with
     Status::Incomplete instead of waiting when it is asked not to slow down,
     and then the increment is simply gone. Such a write was not applied, so
     repeating it blocking cannot count twice. */
  void merge_one(const Slice &key) {
    Status s = db_->Merge(merge_option_, key, _one_slice);
    if (s.ok())
      return;
    if (s.IsIncomplete()) {
      ++stalled_writes_;
      s = db_->Merge(blocking_merge_option_, key, _one_slice);
      if (s.ok())
        return;
    }
    if (failed_writes_++ == 0)
      std::cerr << "collocatordb: cannot write, counts are lost: " << s.ToString()
                << std::endl;
  }

  virtual void inc(const std::string &key) {
    merge_one(Slice(key));
  }

  void inc(const uint64_t key) {
    char encoded_key[sizeof(uint64_t)];
    EncodeFixed64(encoded_key, key);
    merge_one(Slice(encoded_key, sizeof(uint64_t)));
  }

  virtual void inc(uint32_t w1, uint32_t w2, uint8_t dist);

  void dump(uint32_t w1, uint32_t w2, int8_t dist) const;

  vector<Collocator> get_collocators(uint32_t w1);

  vector<Collocator> get_collocators(uint32_t w1, uint32_t max_w2);

  vector<Collocator> get_collocation_scores(uint32_t w1, uint32_t w2);

  vector<Collocator> get_collocators(uint32_t w1, uint32_t min_w2,
                                     uint32_t max_w2);

  void applyCAMeasures(uint32_t w1, uint32_t w2,
                       uint64_t *sumWindow, uint64_t sum,
                       int usedPositions, int true_window_size,
                       Collocator *result) const;

  void dumpSparseLlr(uint32_t w1, uint32_t min_cooccur);

  string collocators2json(uint32_t w1, const vector<Collocator>& collocators);

  // mapped to a rocksdb Merge operation
  virtual bool add(const std::string &key, uint64_t value) {
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

  CollocatorIterator *SeekIterator(uint64_t w1, uint64_t w2, int8_t dist) const;

  /* Writes what is still in memory and closes the database. Without this
     everything that has not been flushed yet is lost when the process ends,
     because the write ahead log is switched off for speed. */
  void close() {
    if (!db_)
      return;
    if (stalled_writes_ > 0)
      std::cerr << "collocatordb: repeated " << stalled_writes_
                << " writes that rocksdb had cancelled" << std::endl;
    if (failed_writes_ > 0)
      std::cerr << "collocatordb: " << failed_writes_
                << " writes failed, the database is missing counts" << std::endl;
    FlushOptions flush_options;
    flush_options.wait = true;
    Status s = db_->Flush(flush_options);
    if (!s.ok() && !s.IsNotSupported())
      std::cerr << "collocatordb: cannot write what is still in memory: "
                << s.ToString() << std::endl;
    db_.reset();
  }

};

CollocatorDB::~CollocatorDB() { close(); }

CollocatorDB::CollocatorDB(const char *db_name,
                                    bool read_only = false) {
  //		merge_option_.sync = true;
  if (read_only)
    db_ = OpenDbForRead(strdup(db_name));
  else
    db_ = OpenDb(db_name);
  assert(db_);
  uint64_t one = 1;
  EncodeFixed64(_one, one);
  _one_slice = Slice(_one, sizeof(uint64_t));
}

void CollocatorDB::inc(const uint32_t w1, const uint32_t w2,
                                const uint8_t dist) {
  inc(encodeCollocation(w1, w2, dist));
}

void CollocatorDB::readVocab(const string& fname) {
  char strbuf[2048];
  uint64_t freq;
  FILE *fin = fopen(fname.c_str(), "rb");
  if (fin == nullptr) {
    cout << "Vocabulary file " << fname << " not found\n";
    exit(1);
  }
  uint64_t i = 0;
  while (fscanf(fin, "%s %lu", strbuf, &freq) == 2) {
    _vocab.push_back({strbuf, freq});
    total += freq;
    i++;
  }
  fclose(fin);

  char size_fname[256];
  strcpy(size_fname, fname.c_str());
  char *pos = strstr(size_fname, ".vocab");
  if (pos) {
    *pos = 0;
    strcat(size_fname, ".size");
    FILE *fp = fopen(size_fname, "r");
    if (fp != nullptr) {
      fscanf(fp, "%lu", &sentences);
      fscanf(fp, "%lu", &total);
      float sl = (float)total / (float)sentences;
      float w = WINDOW_SIZE;
      avg_window_size =
          ((sl > 2 * w ? (sl - 2 * w) * 2 * w : 0) + (double)w * (3 * w - 1)) /
          sl;
      fprintf(stdout,
              "Size corrections found: corpus size: %lu tokens in %lu "
              "sentences, avg. sentence size: %f, avg. window size: %f\n",
              total, sentences, sl, avg_window_size);
      fclose(fp);
    } else {
      // std::cout << "size file " << size_fname << " not found\n";
    }
  } else {
    std::cout << "cannot determine size file " << size_fname << "\n";
  }
}

std::shared_ptr<DB> CollocatorDB::OpenDbForRead(const char *name) {
  ROCKSDB_DB_HANDLE db;
  Options options;
  options.env->SetBackgroundThreads(4);
  options.create_if_missing = true;
  options.merge_operator = std::make_shared<CountMergeOperator>();
  options.max_successive_merges = 0;
  //		options.prefix_extractor.reset(NewFixedPrefixTransform(8));
  options.IncreaseParallelism();
  options.OptimizeLevelStyleCompaction();
  options.prefix_extractor.reset(NewFixedPrefixTransform(3));
  ostringstream dbname, vocabname;
  dbname << name << ".rocksdb";
  auto s = DB::OpenForReadOnly(options, dbname.str(), &db);
  if (!s.ok()) {
    std::cerr << s.ToString() << std::endl;
    assert(false);
  }
  vocabname << name << ".vocab";
  readVocab(vocabname.str());
  return std::shared_ptr<DB>(ROCKSDB_DB_RELEASE(db));
}

  /* Reads a size from the environment, so that a long indexing run can be
     tuned without recompiling. Returns the default when unset or unusable. */
  static uint64_t env_size(const char *name, uint64_t fallback) {
    const char *value = getenv(name);
    if (value == nullptr)
      return fallback;
    char *end = nullptr;
    unsigned long long parsed = strtoull(value, &end, 10);
    if (end == value || parsed == 0) {
      std::cerr << "collocatordb: ignoring " << name << "=" << value << std::endl;
      return fallback;
    }
    return (uint64_t)parsed;
  }

  std::shared_ptr<DB> CollocatorDB::OpenDb(const char *dbname) {
    ROCKSDB_DB_HANDLE db;
    Options options;

    int max_cores = static_cast<int>(std::thread::hardware_concurrency());

    options.create_if_missing = true;
    options.merge_operator = std::make_shared<CountMergeOperator>();

    /* Indexing a corpus is one long stream of merge operands for the same
       keys. rocksdb only collapses them when memtables are merged and when
       files are compacted, so the settings aim at doing that early and often -
       every collapsed operand is one less to write, to read and to merge
       again later.

       All of it can be overridden from the environment, to be able to tune a
       run that takes days without recompiling. */
    options.write_buffer_size = env_size("COLLOCATORDB_WRITE_BUFFER_MB", 256) << 20;
    options.max_write_buffer_number = (int)env_size("COLLOCATORDB_WRITE_BUFFERS", 8);
    /* collapses the operands of several memtables before they are written */
    options.min_write_buffer_number_to_merge =
        (int)env_size("COLLOCATORDB_WRITE_BUFFERS_TO_MERGE", 4);

    /* Compaction has to keep up with the writer, otherwise the level 0 files
       pile up and every read has to merge through all of them. */
    options.max_background_jobs =
        (int)env_size("COLLOCATORDB_BACKGROUND_JOBS",
                      max_cores > 4 ? (max_cores < 32 ? max_cores : 32) : 4);
    options.max_subcompactions = (int)env_size("COLLOCATORDB_SUBCOMPACTIONS", 4);
    options.level0_file_num_compaction_trigger = 4;
    options.level0_slowdown_writes_trigger = 20;
    options.level0_stop_writes_trigger = 36;

    /* Merge operands are inserted one writer at a time, rocksdb does not
       support concurrent memtable writes for them, which is why more threads
       in the indexer do not help beyond a certain point. */
    options.allow_concurrent_memtable_write = false;
    options.enable_write_thread_adaptive_yield = true;
    options.allow_mmap_reads = true;

    BlockBasedTableOptions table_options;
    table_options.block_cache =
        NewLRUCache(env_size("COLLOCATORDB_BLOCK_CACHE_MB", 512) << 20);
    options.table_factory.reset(NewBlockBasedTableFactory(table_options));

    /* No write ahead log: an interrupted indexing run is repeated, and the log
       would double the amount written. Everything that has not been flushed is
       lost when the process dies, which is what close_collocatordb() is for. */
    merge_option_.disableWAL = true;
    merge_option_.sync = false;
    /* Let rocksdb slow the writer down when compaction falls behind, instead
       of cancelling the write. It used to be told to do neither, which threw
       counts away. merge_one() repeats a cancelled write, whatever the
       settings are. */
    merge_option_.low_pri = false;
    merge_option_.no_slowdown = false;
    blocking_merge_option_ = merge_option_;
    blocking_merge_option_.low_pri = false;
    blocking_merge_option_.no_slowdown = false;

    Status s = DB::Open(options, dbname, &db);
    if (!s.ok()) {
      std::cerr << s.ToString() << std::endl;
      assert(false);
    }
    total = 1000;
    return std::shared_ptr<DB>(ROCKSDB_DB_RELEASE(db));
  }

CollocatorIterator *
CollocatorDB::SeekIterator(uint64_t w1, uint64_t w2, int8_t dist) const {
  ReadOptions options;
  options.prefix_same_as_start = true;
  char prefixc[sizeof(uint64_t)];
  EncodeFixed64(prefixc, encodeCollocation(w1, w2, dist));
  Iterator *it = db_->NewIterator(options);
  auto *cit = new CollocatorIterator(it);
  if (w2 > 0)
    cit->Seek(std::string(prefixc, 6));
  else
    cit->Seek(std::string(prefixc, 3));
  cit->setPrefix(prefixc);
  return cit;
}

void CollocatorDB::dump(uint32_t w1, uint32_t w2, int8_t dist) const {
  auto it = std::unique_ptr<CollocatorIterator>(SeekIterator(w1, w2, dist));
  for (; it->isValid(); it->Next()) {
    uint64_t value = it->intValue();
    uint64_t key = it->intKey();
    std::cout << "w1:" << W1(key) << ", w2:" << W2(key)
              << ", dist:" << (int32_t)DIST(key) << " - count:" << value
              << std::endl;
  }
  std::cout << "ready dumping\n";
}

bool sortByNpmi(const Collocator &lhs, const Collocator &rhs) {
  return lhs.npmi > rhs.npmi;
}

bool sortByLfmd(const Collocator &lhs, const Collocator &rhs) {
  return lhs.lfmd > rhs.lfmd;
}

bool sortByLlr(const Collocator &lhs, const Collocator &rhs) {
  return lhs.llr > rhs.llr;
}

bool sortByLogDice(const Collocator &lhs, const Collocator &rhs) {
  return lhs.logdice > rhs.logdice;
}

bool sortByLogDiceAF(const Collocator &lhs, const Collocator &rhs) {
  return lhs.ldaf > rhs.ldaf;
}

void CollocatorDB::applyCAMeasures(
    const uint32_t w1, const uint32_t w2, uint64_t *sumWindow,
    const uint64_t sum, const int usedPositions, int true_window_size,
    Collocator *result) const {
  uint64_t f1 = _vocab[w1].freq, f2 = _vocab[w2].freq;
  double o = sum, r1 = f1 * true_window_size, c1 = f2, e = r1 * c1 / total,
         pmi = log2(o / e), md = log2(o * o / e), lfmd = log2(o * o * o / e),
         llr = ca_ll(f1, f2, sum, total, true_window_size),
         md_nws = ca_md(f1, f2, sum, total, 2 * WINDOW_SIZE),
         ld = ca_logdice(f1, f2, sum, total, true_window_size);

  int bestWindow = usedPositions;
  double bestAF = ld;
  //          if(f1<75000000)
  // #pragma omp parallel for reduction(max:bestAF)
  // #pragma omp target teams distribute parallel for reduction(max:bestAF)
  // map(tofrom:bestAF,currentAF,bestWindow,usedPositions)
  for (int bitmask = 1; bitmask < (1 << (2 * WINDOW_SIZE)); bitmask++) {
    if ((bitmask & usedPositions) == 0 || (bitmask & ~usedPositions) > 0)
      continue;
    uint64_t currentWindowSum = 0;
    // #pragma omp target teams distribute parallel for
    // reduction(+:currentWindowSum) map(tofrom:bitmask,usedPositions)
    for (int pos = 0; pos < 2 * WINDOW_SIZE; pos++) {
      if (((1 << pos) & bitmask & usedPositions) != 0)
        currentWindowSum += sumWindow[pos];
    }
    double currentAF = ca_logdice(f1, f2, currentWindowSum, total,
                                  __builtin_popcount(bitmask));
    if (currentAF > bestAF) {
      bestAF = currentAF;
      bestWindow = bitmask;
    }
  }

  *result = {w2,
             f2,
             sum,
             pmi,
             pmi / (-log2(o / total / true_window_size)),
             llr,
             lfmd,
             md,
             md_nws,
             sumWindow[WINDOW_SIZE],
             sumWindow[WINDOW_SIZE - 1],
             ca_pmi(f1, f2, sumWindow[WINDOW_SIZE], total, 1),
             ca_pmi(f1, f2, sumWindow[WINDOW_SIZE - 1], total, 1),
             ca_dice(f1, f2, sum, total, true_window_size),
             ld,
             bestAF,
             usedPositions,
             bestWindow};
}

std::vector<Collocator>
CollocatorDB::get_collocators(uint32_t w1, uint32_t min_w2,
                                       uint32_t max_w2) {
  std::vector<Collocator> collocators;
  uint64_t w2, last_w2 = 0xffffffffffffffff;
  uint64_t maxv = 0, sum = 0;
  /* fixed size, so it lives on the stack and cannot be leaked */
  uint64_t sumWindow[2 * WINDOW_SIZE];
  memset(sumWindow, 0, sizeof(sumWindow));
  int true_window_size = 1;
  int usedPositions = 0;

  if (w1 > _vocab.size()) {
    std::cout << w1 << "> vocabulary size " << _vocab.size() << "\n";
    w1 -= _vocab.size();
  }
#ifdef DEBUG
  std::cout << "Searching for collocates of " << _vocab[w1].word << "\n";
#endif
  // #pragma omp parallel num_threads(40)
  // #pragma omp single
  for (auto it =
           std::unique_ptr<CollocatorIterator>(SeekIterator(w1, min_w2, 0));
       it->isValid(); it->Next()) {
    uint64_t value = it->intValue(), key = it->intKey();
    if ((w2 = W2(key)) > max_w2)
      continue;
    if (last_w2 == 0xffffffffffffffff)
      last_w2 = w2;
    if (w2 != last_w2) {
      if (sum >= FREQUENCY_THRESHOLD) {
        collocators.push_back({});
        Collocator *result = &(collocators[collocators.size() - 1]);
        // #pragma omp task firstprivate(last_w2, sumWindow, sum, usedPositions,
        // true_window_size) shared(w1, result) if(sum > 1000000)
        {
          // uint64_t *nsw = (uint64_t *)malloc(sizeof(uint64_t) * 2
          // *WINDOW_SIZE); memcpy(nsw, sumWindow, sizeof(uint64_t) * 2
          // *WINDOW_SIZE);
          applyCAMeasures(w1, last_w2, sumWindow, sum, usedPositions,
                          true_window_size, result);
          // free(nsw);
        }
      }
      memset(sumWindow, 0, 2 * WINDOW_SIZE * sizeof(uint64_t));
      usedPositions = 1 << (-DIST(key) + WINDOW_SIZE - (DIST(key) < 0 ? 1 : 0));
      sumWindow[-DIST(key) + WINDOW_SIZE - (DIST(key) < 0 ? 1 : 0)] = value;
      last_w2 = w2;
      maxv = value;
      sum = value;
      true_window_size = 1;
      if (min_w2 == max_w2 && w2 != min_w2)
        break;
    } else {
      sum += value;
      if (value > maxv)
        maxv = value;
      usedPositions |=
          1 << (-DIST(key) + WINDOW_SIZE - (DIST(key) < 0 ? 1 : 0));
      sumWindow[-DIST(key) + WINDOW_SIZE - (DIST(key) < 0 ? 1 : 0)] = value;
      true_window_size++;
    }
  }

  /* A collocate is only finished when the next one begins, so the last one is
     still in sumWindow when the iterator is through. It used to be dropped,
     which cost every word one of its collocates. */
  if (last_w2 != 0xffffffffffffffff && sum >= FREQUENCY_THRESHOLD) {
    collocators.push_back({});
    applyCAMeasures(w1, last_w2, sumWindow, sum, usedPositions,
                    true_window_size, &(collocators[collocators.size() - 1]));
  }

  // #pragma omp taskwait
  sort(collocators.begin(), collocators.end(), sortByLogDiceAF);

#ifdef DEBUG
  int i = 0;
  for (Collocator c : collocators) {
    if (i++ > 10)
      break;
    std::cout << "w1:" << _vocab[w1].word << ", w2: *" << _vocab[c.w2].word
              << "*"
              << "\t f(w1):" << _vocab[w1].freq
              << "\t f(w2):" << _vocab[c.w2].freq << "\t f(w1, w2):" << c.raw
              << "\t pmi:" << c.pmi << "\t npmi:" << c.npmi
              << "\t llr:" << c.llr << "\t md:" << c.md << "\t lfmd:" << c.lfmd
              << "\t total:" << total << std::endl;
  }
#endif

  return collocators;
}

std::vector<Collocator>
CollocatorDB::get_collocation_scores(uint32_t w1, uint32_t w2) {
  return get_collocators(w1, w2, w2);
}

std::vector<Collocator> CollocatorDB::get_collocators(uint32_t w1) {
  return get_collocators(w1, 0, UINT32_MAX);
}

void CollocatorDB::dumpSparseLlr(uint32_t w1, uint32_t min_cooccur) {
  std::vector<Collocator> collocators;
  std::stringstream stream;
  uint64_t w2, last_w2 = 0xffffffffffffffff;
  uint64_t maxv = 0, total_w1 = 0;
  bool first = true;
  for (auto it = std::unique_ptr<CollocatorIterator>(SeekIterator(w1, 0, 0));
       it->isValid(); it->Next()) {
    uint64_t value = it->intValue(), key = it->intKey();
    w2 = W2(key);
    total_w1 += value;
    if (last_w2 == 0xffffffffffffffff)
      last_w2 = w2;
    if (w2 != last_w2) {
      if (maxv >= min_cooccur) {
        double llr =
            ca_ll(_vocab[w1].freq, _vocab[last_w2].freq, maxv, total, 1);
        if (first)
          first = false;
        else
          stream << " ";
        stream << w2 << " " << llr;
      }
      last_w2 = w2;
      maxv = value;
    } else {
      if (value > maxv)
        maxv = value;
    }
  }
  if (first)
    stream << "1 0.0";
  stream << "\n";
  std::cout << stream.str();
}

Slice CollocatorIterator::key() const {
  return base_iterator_->key();
}

Slice CollocatorIterator::value() const {
  return base_iterator_->value();
}

Status CollocatorIterator::status() const {
  return base_iterator_->status();
}

}; // namespace rocksdb

string CollocatorDB::getWord(uint32_t w1) { return _vocab[w1].word; }

uint64_t CollocatorDB::getWordId(const char *word) const {
  for (uint64_t i = 0; i < _vocab.size(); i++) {
    if (strcmp(_vocab[i].word.c_str(), word) == 0)
      return i;
  }
  return 0;
}

uint64_t CollocatorDB::getCorpusSize() const {
  return total;
}

uint64_t CollocatorDB::getWordFrequency(uint64_t w1) {
  return _vocab[w1].freq;
}

string CollocatorDB::collocators2json(uint32_t w1,
                                               const vector<Collocator>& collocators) {
  ostringstream s;
  int i = 0;
  s << " { \"f1\": " << _vocab[w1].freq << "," << R"("w1":")"
    << string(_vocab[w1].word) << "\", " << "\"N\": " << total << ", "
    << "\"collocates\": [";
  bool first = true;
  for (Collocator c : collocators) {
    if (strncmp(_vocab[c.w2].word.c_str(), "quot", 4) == 0)
      continue;
    if (i++ > 200)
      break;
    if (!first)
      s << ",\n";
    else
      first = false;
    s << "{"
         "\"word\":\""
      << (string(_vocab[c.w2].word) == "<num>"
              ? string("###")
              : string(_vocab[c.w2].word))
      << "\"," << "\"f2\":" << c.f2 << "," << "\"f\":" << c.raw << ","
      << "\"npmi\":" << c.npmi << "," << "\"pmi\":" << c.pmi << ","
      << "\"llr\":" << c.llr << "," << "\"lfmd\":" << c.lfmd << ","
      << "\"md\":" << c.md << "," << "\"md_nws\":" << c.md_nws << "," << "\"dice\":" << c.dice << ","
      << "\"ld\":" << c.logdice << "," << "\"ln_count\":" << c.left_raw << ","
      << "\"rn_count\":" << c.right_raw << "," << "\"ln_pmi\":" << c.left_pmi
      << "," << "\"rn_pmi\":" << c.right_pmi << "," << "\"ldaf\":" << c.ldaf
      << "," << "\"win\":" << c.window << "," << "\"afwin\":" << c.af_window
      << "}";
  }
  s << "]}\n";
  //  std::cout << s.str();
  return s.str();
}

typedef CollocatorDB COLLOCATORS;

extern "C" {
#ifdef __clang__
#pragma clang diagnostic push
#pragma ide diagnostic ignored "OCUnusedGlobalDeclarationInspection"
#endif
DLL_EXPORT COLLOCATORS *open_collocatordb_for_write(char *dbname) {
  return new CollocatorDB(dbname, false);
}

DLL_EXPORT void close_collocatordb(COLLOCATORS *db) { delete db; }

DLL_EXPORT COLLOCATORS *open_collocatordb(char *dbname) {
  return new CollocatorDB(dbname, true);
}

DLL_EXPORT void inc_collocator(COLLOCATORS *db, uint32_t w1, uint32_t w2,
                               int8_t dist) {
  db->inc(w1, w2, dist);
}

DLL_EXPORT void dump_collocators(COLLOCATORS *db, uint32_t w1, uint32_t w2,
                                 int8_t dist) {
  db->dump(w1, w2, dist);
}

DLL_EXPORT COLLOCATORS *get_collocators(COLLOCATORS *db, uint32_t w1) {
  std::vector<Collocator> c = db->get_collocators(w1);
  if (c.empty())
    return nullptr;
  /* one entry more than there are collocators, terminated with w2 == 0 and
     raw == 0, so that callers can tell where the array ends */
  uint64_t size = (c.size() + 1) * sizeof c[0];
  auto *p = (COLLOCATORS *)malloc(size);
  memset(p, 0, size);
  memcpy(p, c.data(), c.size() * sizeof c[0]);
  return p;
}

DLL_EXPORT COLLOCATORS *get_collocation_scores(COLLOCATORS *db, uint32_t w1,
                                               uint32_t w2) {
  std::vector<Collocator> c = db->get_collocation_scores(w1, w2);
  if (c.empty())
    return nullptr;
  /* one entry more than there are collocators, terminated with w2 == 0 and
     raw == 0, so that callers can tell where the array ends */
  uint64_t size = (c.size() + 1) * sizeof c[0];
  auto *p = (COLLOCATORS *)malloc(size);
  memset(p, 0, size);
  memcpy(p, c.data(), c.size() * sizeof c[0]);
  return p;
}

DLL_EXPORT char *get_word(COLLOCATORS *db, uint32_t w) {
  return strdup(db->getWord(w).c_str());
}

DLL_EXPORT uint64_t get_word_id(COLLOCATORS *db, char *word) {
  return db->getWordId(word);
}

DLL_EXPORT void read_vocab(COLLOCATORS *db, char *fname) {
  std::string fName(fname);
  db->readVocab(fName);
}

DLL_EXPORT const char *get_collocators_as_json(COLLOCATORS *db, uint32_t w1) {
  return strdup(db->collocators2json(w1, db->get_collocators(w1)).c_str());
}

DLL_EXPORT const char *
get_collocation_scores_as_json(COLLOCATORS *db, uint32_t w1, uint32_t w2) {
  return strdup(
      db->collocators2json(w1, db->get_collocation_scores(w1, w2)).c_str());
}

DLL_EXPORT const char *get_version() { return PROJECT_VERSION; }

DLL_EXPORT uint64_t get_corpus_size(COLLOCATORS *db) { return db->getCorpusSize(); };

DLL_EXPORT uint64_t get_word_frequency(COLLOCATORS *db, uint64_t w1) {
  return db->getWordFrequency(w1);
}

#ifdef __clang__
#pragma clang diagnostic push
#endif
}
