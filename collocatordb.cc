#include <typeinfo>
#define EXPORT __attribute__((visibility("visible")))
#define IMPORT
#include <assert.h>
#include <inttypes.h>
#include <memory>
#include <iostream>
#include <algorithm>
#include <vector>
#include <stdint.h>
#include <string>
#include <sstream> // for ostringstream
#include <math.h>
#include <rocksdb/cache.h>
#include "rocksdb/comparator.h"
#include "rocksdb/db.h"
#include "rocksdb/env.h"
#include "rocksdb/table.h"
#include <rocksdb/merge_operator.h>
#include <rocksdb/slice_transform.h>
#include "rocksdb/utilities/db_ttl.h"
#include "rocksdb/filter_policy.h"
#include "merge_operators.h"

#define WINDOW_SIZE 5.0
#define FREQUENCY_THRESHOLD 5
#define IS_BIG_ENDIAN (*(uint16_t *)"\0\xff" < 0x100)
#define encodeCollocation(w1, w2, dist) (((uint64_t)dist << 56) | ((uint64_t)w2 << 24) | w1)
#define W1(key) (uint64_t)(key & 0xffffff)
#define W2(key) (uint64_t)((key >> 24) & 0xffffff)
#define DIST(key) (int8_t)((uint64_t)((key >> 56) & 0xff))

typedef struct {
  uint64_t freq;
  char *word;
}  vocab_entry;

// typedef struct Collocator {
//   uint64_t w2;
//   uint64_t sum;
// };

using namespace rocksdb;
using namespace std;

namespace rocksdb {
    class Collocator {
    public:
    uint64_t w2;
    uint64_t raw;
    double pmi;
    double npmi;
    double llr;
    double lfmd;
    double md;
    double left_lfmd;
    double right_lfmd;
    double left_npmi;
    double right_npmi;
    double dice;
    double logdice;
  };

  size_t num_merge_operator_calls;
  void resetNumMergeOperatorCalls() { num_merge_operator_calls = 0; }

  size_t num_partial_merge_calls;
  void resetNumPartialMergeCalls() { num_partial_merge_calls = 0; }


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

  static inline double ca_pmi(uint64_t f1, uint64_t f2, uint64_t f12, uint64_t total, double window_size) {
    double
      r1 = f1 * window_size,
      c1 = f2,
      e = r1 * c1 / total,
      o = f12;
    return log2(o/e);
  }

  // Bouma, Gerlof (2009): <a href="https://svn.spraakdata.gu.se/repos/gerlof/pub/www/Docs/npmi-pfd.pdf">
  // Normalized (pointwise) mutual information in collocation extraction</a>. In Proceedings of GSCL. 
  static inline double ca_npmi(uint64_t f1, uint64_t f2, uint64_t f12, uint64_t total, double window_size) {
    double
      r1 = f1 * window_size,
      c1 = f2,
      e = r1 * c1 / total,
      o = f12;
    if(f12 < FREQUENCY_THRESHOLD)
      return -1.0;
    else
      return log2(o/e) / (-log2(o/total/window_size));
  }

  // Thanopoulos, A., Fakotakis, N., Kokkinakis, G.: Comparative evaluation of collocation extraction metrics.
  // In: International Conference on Language Resources and Evaluation (LREC-2002). (2002) 620–625
  // double md = log2(pow((double)max * window_size / total, 2) /  (window_size * ((double)_vocab[w1].freq/total) * ((double)_vocab[last_w2].freq/total)));
  static inline double ca_md(uint64_t f1, uint64_t f2, uint64_t f12, uint64_t total, double window_size) {
    double
      r1 = f1 * window_size,
      c1 = f2,
      e = r1 * c1 / total,
      o = f12;
    return log2(o*o/e);
  }

  static inline double ca_lfmd(uint64_t f1, uint64_t f2, uint64_t f12, uint64_t total, double window_size) {
    double
      r1 = f1 * window_size,
      c1 = f2,
      e = r1 * c1 / total,
      o = f12;
    if(f12 == 0)
      return 0;
    else
      return log2(o*o*o/e);
  }

  // Evert, Stefan (2004): The Statistics of Word Cooccurrences: Word Pairs and Collocations. PhD dissertation, IMS, University of Stuttgart. Published in 2005, URN urn:nbn:de:bsz:93-opus-23714. 
  // Free PDF available from http://purl.org/stefan.evert/PUB/Evert2004phd.pdf
  static inline double ca_ll(uint64_t w1, uint64_t w2, uint64_t w12, uint64_t n, uint64_t window_size) {
    double
      r1 = (double) w1 * window_size,
      r2 = (double) n - r1,
      c1 = w2,
      c2 = n - c1,
      o11 = w12,          o12 = r1 - o11,
      o21 = c1 - w12,     o22 = r2 - o21,
      e11 = r1 * c1 / n,  e12 = r1 * c2 / n,
      e21 = r2 * c1 / n,  e22 = r2 * c2 / n;
    return (2 * ( (o11>0? o11 * log(o11/e11):0) + (o12>0? o12 * log(o12/e12):0) + (o21>0? o21 * log(o21/e21):0) + (o22>0? o22 * log(o22/e22):0)));
  }


  static inline double ca_dice(uint64_t w1, uint64_t w2, uint64_t w12, uint64_t n, uint64_t window_size) {
    double
      r1 = (double) w1 * window_size,
      c1 = w2;
    return 2 * w12 / (c1+r1);
  }

  // Rychlý, Pavel (2008): <a href="http://www.fi.muni.cz/usr/sojka/download/raslan2008/13.pdf">A lexicographer-friendly association score.</a> In Proceedings of Recent Advances in Slavonic Natural Language Processing, RASLAN, 6–9.
  static inline double ca_logdice(uint64_t w1, uint64_t w2, uint64_t w12, uint64_t n, uint64_t window_size) {
    double
      e = 0.5,
      r1 = (double) w1 * window_size,
      c1 = w2;
    return 14 + log2(2 * (w12+e) / (c1+e+r1+e));
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


  class CollocatorIterator : public Iterator {
  private:
    char prefixc[sizeof(uint64_t)];
    Iterator *base_iterator_;


  public:
    CollocatorIterator(Iterator* base_iterator)
      : base_iterator_(base_iterator)
    {}

    void setPrefix(char *prefix) {
      memcpy(prefixc, prefix, sizeof(uint64_t));
    }

    virtual void SeekToFirst() { base_iterator_->SeekToFirst(); }
    virtual void SeekToLast() { base_iterator_->SeekToLast(); }
    virtual void Seek(const rocksdb::Slice& s) { base_iterator_->Seek(s); }
    virtual void Prev() { base_iterator_->Prev(); }
    virtual void Next() { base_iterator_->Next(); }
    virtual Slice key() const;
    virtual Slice value() const;
    virtual Status status() const;
    virtual bool Valid() const;
    bool isValid();
    uint64_t intValue();
    uint64_t intKey();

  };

  //  rocksdb::CollocatorIterator::CollocatorIterator(Iterator* base_iterator) {}

  bool rocksdb::CollocatorIterator::Valid() const {
    return base_iterator_->Valid() && key().starts_with(std::string(prefixc,3));
  }

  bool rocksdb::CollocatorIterator::isValid() {
    return base_iterator_->Valid() && key().starts_with(std::string(prefixc,3));
    // return key().starts_with(std::string(prefixc,3));
  }

  uint64_t rocksdb::CollocatorIterator::intKey() {
    return DecodeFixed64(base_iterator_->key().data());
  }

  uint64_t rocksdb::CollocatorIterator::intValue() {
    return DecodeFixed64(base_iterator_->value().data());
  }

  class VocabEntry {
  public:
    string word;
    uint64_t freq;
  };

  class CollocatorDB {
  private:
    WriteOptions merge_option_; // for merge
    char _one[sizeof(uint64_t)];
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

    uint64_t default_;

    std::shared_ptr<DB> OpenDb(const char *dbname);
    std::shared_ptr<DB> OpenDbForRead(const char *dbname);
    void read_vocab(string fname);
    
  public:
    string getWord(uint32_t w1);
    CollocatorDB(const char *db_name, bool read_only);

    // public interface of CollocatorDB.
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
      get(std::string(encoded_key, 8), &value);
      return value;
    }

    virtual void inc(const std::string& key) {
      db_->Merge(merge_option_, key, _one_slice);
    }

    void inc(const uint64_t key) {
      char encoded_key[sizeof(uint64_t)];
      EncodeFixed64(encoded_key, key);
      db_->Merge(merge_option_, std::string(encoded_key, 8), _one_slice);
    }

    virtual void inc(const uint32_t w1, const uint32_t w2, const uint8_t dist);
    void dump(uint32_t w1, uint32_t w2, int8_t dist);
    vector<Collocator> get_collocators(uint32_t w1);
    vector<Collocator> get_collocators(uint32_t w1, uint32_t max_w2);
    void dumpSparseLlr(uint32_t w1, uint32_t min_cooccur);
    string collocators2json(vector<Collocator> collocators);

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

    CollocatorIterator* SeekIterator(uint64_t w1, uint64_t w2, int8_t dist);
  };

  rocksdb::CollocatorDB::CollocatorDB(const char *db_name, bool read_only = false) {
		//		merge_option_.sync = true;
    if(read_only)
      db_ = OpenDbForRead(db_name);
    else
      db_ = OpenDb(db_name);
    assert(db_);
    uint64_t one = 1;
    EncodeFixed64(_one, one);
    _one_slice = Slice(_one, sizeof(uint64_t));
  }

  void rocksdb::CollocatorDB::inc(const uint32_t w1, const uint32_t w2, const uint8_t dist) {
    inc(encodeCollocation(w1, w2, dist));
  }

  void rocksdb::CollocatorDB::read_vocab(string fname) {
    char strbuf[2048];
    uint64_t freq;
    FILE *fin = fopen(fname.c_str(), "rb");
    if (fin == NULL) {
      cout <<  "Vocabulary file " << fname <<" not found\n";
      exit(1);
    }
    uint64_t i = 0;
    while(!feof(fin)) {
      fscanf(fin, "%s %lu", strbuf, &freq);
      _vocab.push_back({strbuf, freq});
      total += freq;
      i++;
    }
    fclose(fin);

    char size_fname[256];
    strcpy(size_fname, fname.c_str());
    char *pos = strstr(size_fname, ".vocab");
    if(pos) {
      *pos=0;
      strcat(size_fname, ".size");
      FILE *fp = fopen(size_fname, "r");
      if (fp != NULL) {
        fscanf(fp, "%lu", &sentences);
        fscanf(fp, "%lu", &total);
        float sl = (float)total/(float)sentences;
        float w = WINDOW_SIZE;
        avg_window_size = ((sl > 2*w? (sl-2*w)*2*w: 0) + (double) w * (3*w -1)) / sl;
        fprintf(stdout, "Size corrections found: corpus size: %lu tokens in %lu sentences, avg. sentence size: %f, avg. window size: %f\n", total, sentences, sl, avg_window_size);
        fclose(fp);
      } else {
        std::cout <<  "size file " << size_fname << " not found\n";
      }
    } else {
      std::cout <<  "cannot determine size file " << size_fname << "\n";
    }
  }

  std::shared_ptr<DB> rocksdb::CollocatorDB::OpenDbForRead(const char *name) {
		DB* db;
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
    read_vocab(vocabname.str());
		return std::shared_ptr<DB>(db);
  }

  std::shared_ptr<DB> rocksdb::CollocatorDB::OpenDb(const char *dbname) {
		DB* db;
		Options options;


		options.env->SetBackgroundThreads(4);
		options.create_if_missing = true;
		options.merge_operator = std::make_shared<CountMergeOperator>();
		options.max_successive_merges = 0;
    //		options.prefix_extractor.reset(NewFixedPrefixTransform(8));
		options.IncreaseParallelism();
    options.OptimizeLevelStyleCompaction();
    // options.max_write_buffer_number = 48;
    // options.max_background_jobs = 48;
    // options.allow_concurrent_memtable_write=true;
		//		options.memtable_factory.reset(rocksdb::NewHashLinkListRepFactory(200000));
		// options.enable_write_thread_adaptive_yield = 1;
		// options.allow_concurrent_memtable_write = 1;
		// options.memtable_factory.reset(new rocksdb::SkipListFactory);
		// options.write_buffer_size = 1 << 22;
		// options.allow_mmap_reads = true;
		// options.allow_mmap_writes = true;
		// options.max_background_compactions = 40;
    // BlockBasedTableOptions table_options;
    // table_options.filter_policy.reset(NewBloomFilterPolicy(24, false));
		// options.bloom_locality = 1;
    // std::shared_ptr<Cache> cache = NewLRUCache(512 * 1024 * 1024);
    // table_options.block_cache = cache;
		// options.table_factory.reset(NewBlockBasedTableFactory(table_options));
		Status s;
		//  DestroyDB(dbname, Options());
		s = DB::Open(options, dbname, &db);
		if (!s.ok()) {
			std::cerr << s.ToString() << std::endl;
			assert(false);
		}
		return std::shared_ptr<DB>(db);
  }

  CollocatorIterator* rocksdb::CollocatorDB::SeekIterator(uint64_t w1, uint64_t w2, int8_t dist) {
    ReadOptions options;
    options.prefix_same_as_start = true;
    char prefixc[sizeof(uint64_t)];
    EncodeFixed64(prefixc, encodeCollocation(w1, w2, dist));
    Iterator *it = db_->NewIterator(options);
    CollocatorIterator *cit = new CollocatorIterator(it);
    cit->Seek(std::string(prefixc,3));// it->Valid() && it->key().starts_with(std::string(prefixc,3)); it->Next()) {
    cit->setPrefix(prefixc);
    return cit;
  }

	void rocksdb::CollocatorDB::dump(uint32_t w1, uint32_t w2, int8_t dist) {
    auto it = std::unique_ptr<CollocatorIterator>(SeekIterator(w1, w2, dist));
    for (; it->isValid(); it->Next()) {
      uint64_t value = it->intValue();
      uint64_t key = it->intKey();
      std::cout << "w1:" << W1(key) << ", w2:" << W2(key) << ", dist:" << (int32_t) DIST(key) << " - count:" << value << std::endl;
    }
    std::cout << "ready dumping\n";
  }

	bool sortByNpmi(const Collocator &lhs, const Collocator &rhs) { return lhs.npmi > rhs.npmi; }
	bool sortByLfmd(const Collocator &lhs, const Collocator &rhs) { return lhs.lfmd > rhs.lfmd; }
	bool sortByLlr(const Collocator &lhs, const Collocator &rhs) { return lhs.llr > rhs.llr; }
	bool sortByLogDice(const Collocator &lhs, const Collocator &rhs) { return lhs.logdoce > rhs.logdice; }

	std::vector<Collocator> rocksdb::CollocatorDB::get_collocators(uint32_t w1, uint32_t max_w2) {
		std::vector<Collocator> collocators;
    uint64_t w2, last_w2 = 0xffffffffffffffff;
    uint64_t maxv = 0, sum = 0, left = 0, right = 0;

    for ( auto it = std::unique_ptr<CollocatorIterator>(SeekIterator(w1, 0, 0)); it->isValid(); it->Next()) {
      uint64_t value = it->intValue(),
        key = it->intKey();
      if((w2 = W2(key)) > max_w2)
        continue;
      if(last_w2 == 0xffffffffffffffff) last_w2 = w2;
      if (w2 != last_w2) {
        if(sum >= FREQUENCY_THRESHOLD) {
          double o = sum,
            r1 = (double)_vocab[w1].freq * avg_window_size,
            c1 = (double)_vocab[last_w2].freq,
            e = r1 * c1 / total,
            pmi = log2(o/e),
            md = log2(o*o/e),
            lfmd = log2(o*o*o/e),
            llr = ca_ll((double)_vocab[w1].freq, (double)_vocab[last_w2].freq, sum, total, avg_window_size);
          double left_lfmd = ca_lfmd(_vocab[w1].freq, _vocab[last_w2].freq, left, total, 1);
          double right_lfmd = ca_lfmd(_vocab[w1].freq, _vocab[last_w2].freq, right, total, 1);
          double left_npmi = ca_npmi(_vocab[w1].freq, _vocab[last_w2].freq, left, total, 1);
          double right_npmi = ca_npmi(_vocab[w1].freq, _vocab[last_w2].freq, right, total, 1);
          collocators.push_back ( {last_w2, sum, pmi, pmi / (-log2(o/total/avg_window_size)), /* normalize to [-1,1] */
                llr, lfmd, md,
                left_lfmd,
                right_lfmd,
                left_npmi,
                right_npmi,
                ca_dice((double)_vocab[w1].freq, (double)_vocab[last_w2].freq, sum, total, avg_window_size),
                ca_logdice((double)_vocab[w1].freq, (double)_vocab[last_w2].freq, sum, total, avg_window_size)
                }
            );
        }
        last_w2 = w2;
        maxv = value;
        sum = value;
      } else {
        sum += value;
        if(value > maxv)
          maxv = value;
      }
      if(DIST(key) == -1)
        left = value;
      else if(DIST(key) == 1)
        right = value;
    }

		sort(collocators.begin(), collocators.end(), sortByLogDice);
		
    /*
    int i=0;
    for (Collocator c : collocators) {
      if(i++>10) break;
      std::cout << "w1:" << _vocab[w1].word << ", w2:" << _vocab[c.w2].word
                << "\t f(w1):" << _vocab[w1].freq
                << "\t f(w2):" << _vocab[c.w2].freq
                << "\t f(w1, x):" << total_w1
                << "\t f(w1, w2):" << c.raw
                << "\t pmi:" << c.pmi
                << "\t npmi:" << c.npmi
                << "\t llr:" << c.llr
                << "\t lfmd:" << c.lfmd
                << "\t fpmi:" << c.fpmi
                << "\t total:" << total
                << std::endl;
    }
    */
		return collocators;
  }

	std::vector<Collocator> rocksdb::CollocatorDB::get_collocators(uint32_t w1) {
    return get_collocators(w1, UINT32_MAX);
  }

  void rocksdb::CollocatorDB::dumpSparseLlr(uint32_t w1, uint32_t min_cooccur) {
		std::vector<Collocator> collocators;
    std::stringstream stream;
    uint64_t w2, last_w2 = 0xffffffffffffffff;
    uint64_t maxv = 0, total_w1 = 0;
    bool first = true;
    for ( auto it = std::unique_ptr<CollocatorIterator>(SeekIterator(w1, 0, 0)); it->isValid(); it->Next()) {
      uint64_t value = it->intValue(),
        key = it->intKey();
      w2 = W2(key);
      total_w1 += value;
      if(last_w2 == 0xffffffffffffffff) last_w2 = w2;
      if (w2 != last_w2) {
        if(maxv >= min_cooccur) {
          double llr = ca_ll(_vocab[w1].freq, _vocab[last_w2].freq,  maxv, total, 1);
          if(first)
            first = false;
          else
           stream << " ";
          stream << w2  << " " << llr;
        }
        last_w2 = w2;
        maxv = value;
      } else {
        if(value > maxv)
          maxv = value;
      }
    }
    if(first)
      stream  << "1 0.0";
    stream  << "\n";
    std::cout << stream.str();
  }

  rocksdb::Slice rocksdb::CollocatorIterator::key() const { return base_iterator_->key(); }
  rocksdb::Slice rocksdb::CollocatorIterator::value() const { return base_iterator_->value(); }
  rocksdb::Status rocksdb::CollocatorIterator::status() const { return base_iterator_->status(); }

};

string rocksdb::CollocatorDB::getWord(uint32_t w1) {
  return _vocab[w1].word;
}

string rocksdb::CollocatorDB::collocators2json(vector<Collocator> collocators) {
  ostringstream s;
  int i = 0;
  s << "[";
  bool first = true;
  for (Collocator c : collocators) {
    if(strncmp(_vocab[c.w2].word.c_str(), "quot", 4) == 0) continue;
    if (i++ > 200)
      break;
    if(!first)
      s << ",\n";
    else
      first = false;
    s << "{"
      "\"word\":\"" << string(_vocab[c.w2].word) << "\"," <<
      "\"rank\":" << c.w2    << "," <<
      "\"f\":" << c.raw    << "," <<
      "\"npmi\":" << c.npmi  << "," <<
      "\"pmi\":" << c.pmi  << "," <<
      "\"llr\":" << c.llr   << "," <<
      "\"lfmd\":" << c.lfmd  << "," <<
      "\"md\":" << c.md  << "," <<
      "\"dice\":" << c.dice  << "," <<
      "\"ld\":" << c.logdice  << "," <<
      "\"llfmd\":" << c.left_lfmd  << "," <<
      "\"rlfmd\":" << c.right_lfmd  << "," <<
      "\"lnpmi\":" << c.left_npmi  << "," <<
      "\"rnpmi\":" << c.right_npmi  <<
      "}";
  }
  s << "]\n";
  //  cout << s.str();
  return s.str();
}

typedef rocksdb::CollocatorDB COLLOCATORS;

extern "C" {
	COLLOCATORS *open_collocatordb_for_write(char *dbname) {
		return new rocksdb::CollocatorDB(dbname, false);
	}
	
	COLLOCATORS *open_collocatordb(char *dbname) {
		return new rocksdb::CollocatorDB(dbname, true);
	}
	
	void inc_collocator(COLLOCATORS *db, uint32_t w1, uint32_t w2, int8_t dist) {
		db->inc(w1, w2, dist);
	}

	void dump_collocators(COLLOCATORS *db, uint32_t w1, uint32_t w2, int8_t dist) {
		db->dump(w1, w2, dist);
	}

	void get_collocators(COLLOCATORS *db, uint32_t w1) {
		db->get_collocators(w1);
	}

	const char *get_word(COLLOCATORS *db, uint32_t w) {
		return db->getWord(w).c_str();
	}

	const char *get_collocators_as_json(COLLOCATORS *db, uint32_t w1) {
		return strdup(db->collocators2json(db->get_collocators(w1)).c_str());
	}
}
