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

#define AVG_WINDOW_SIZE 7

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
    double fpmi;
    double left_lfmd;
    double right_lfmd;
    double left_npmi;
    double right_npmi;
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
    return log2( total * ((double) f12) / (window_size * ((double) f1) * ((double)f2) ));
  }

  // Bouma, Gerlof (2009): <a href="https://svn.spraakdata.gu.se/repos/gerlof/pub/www/Docs/npmi-pfd.pdf">
  // Normalized (pointwise) mutual information in collocation extraction</a>. In Proceedings of GSCL. 
  static inline double ca_npmi(uint64_t f1, uint64_t f2, uint64_t f12, uint64_t total, double window_size) {
    if(f12 == 0)
      return -1.0;
    else
      return log2( total * ((double) f12) / (window_size * ((double) f1) * ((double)f2) )) / (-log2(((double) f12 / window_size / total)));
  }

  // Thanopoulos, A., Fakotakis, N., Kokkinakis, G.: Comparative evaluation of collocation extraction metrics.
  // In: International Conference on Language Resources and Evaluation (LREC-2002). (2002) 620–625
  // double md = log2(pow((double)max * window_size / total, 2) /  (window_size * ((double)_vocab[w1].freq/total) * ((double)_vocab[last_w2].freq/total)));
  static inline double ca_md(uint64_t f1, uint64_t f2, uint64_t f12, uint64_t total, double window_size) {
    return log2((double)f12 * f12 /  ((double) total * window_size * window_size * f1 * f2));
  }

  static inline double ca_lfmd(uint64_t f1, uint64_t f2, uint64_t f12, uint64_t total, double window_size) {
    if(f12 == 0)
      return 0;
    else
      return log2((double)f12 * f12 /  ((double) total * window_size * window_size * f1 * f2)) + log2((double) f12 / window_size / total);
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
    uint64_t total;
    
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
    void dumpSparseLlr(uint32_t w1, uint32_t min_cooccur);
    vector<Collocator> get_collocators_avg(uint32_t w1);
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

  double calculateLLR(uint64_t f_X_, uint64_t uintN, uint64_t f_X_Y_, uint64_t f_Y_) {
    double f_e_, f_o_;
    double A=0.0, B=0.0, C=0.0, D=0.0, N=0.0;
    double LLR=0.0, statVal=0.0, minusDiffCoeff=0.0;
    double BlogB=0.0, ClogC=0.0;

    N = (double)uintN;
    A = (double)f_X_Y_;
    B = (double)f_X_   -A;
    C = (double)f_Y_   -A;
    D = (double)N      -A-B-C;;

    if (B > 0.) BlogB = B*log(B);
    if (C > 0.) ClogC = C*log(C);

    if ((A>0.) && (D>0.) && (N>0.))	{
      f_e_ = (double)f_X_  /(double)N;
      f_o_ = (double)f_X_Y_/(double)f_Y_;

      minusDiffCoeff =
        (	f_X_==0 ?   (double)((-1)*f_X_Y_) :
          (	f_X_Y_==0 ? (double)((+1)*f_X_) :
            (f_e_-f_o_)/(f_e_+f_o_)
            )
          );

      /* log likelihood ratio */
      LLR     =  2*( A*log(A)
                     +BlogB
                     +ClogC
                     +D*log(D)
                     -(A+B)*log(A+B)
                     -(A+C)*log(A+C)
                     -(B+D)*log(B+D)
                     -(C+D)*log(C+D)
                     +N*log(N)
                     );
    }
    return(minusDiffCoeff > 0 ? 0 : (statVal=LLR));
  }

	
	bool sortByNpmi(const Collocator &lhs, const Collocator &rhs) { return lhs.npmi > rhs.npmi; }
	bool sortByLfmd(const Collocator &lhs, const Collocator &rhs) { return lhs.lfmd > rhs.lfmd; }
	bool sortByLlr(const Collocator &lhs, const Collocator &rhs) { return lhs.llr > rhs.llr; }

	std::vector<Collocator> rocksdb::CollocatorDB::get_collocators_avg(uint32_t w1) {
		std::vector<Collocator> collocators;
    uint64_t w2, last_w2 = 0xffffffffffffffff;
    uint64_t sum = 0, total_w1 = 0;
    for ( auto it = std::unique_ptr<CollocatorIterator>(SeekIterator(w1, 0, 0)); it->isValid(); it->Next()) {
      uint64_t value = it->intValue(),
        key = it->intKey();
      w2 = W2(key);
      total_w1 += value;
      if(last_w2 == 0xffffffffffffffff) last_w2 = w2;
      if (w2 != last_w2) {
				double pmi = log2( total * ((double) sum) /
													 (AVG_WINDOW_SIZE * ((double)_vocab[w1].freq) * ((double)_vocab[last_w2].freq) ));
        //  Thanopoulos, A., Fakotakis, N., Kokkinakis, G.: Comparative evaluation of collocation extraction metrics. In: International Conference on Language Resources and Evaluation (LREC-2002). (2002) 620–625
        // double md = log2(pow((double)sum * AVG_WINDOW_SIZE / total, 2) /  (AVG_WINDOW_SIZE * ((double)_vocab[w1].freq/total) * ((double)_vocab[last_w2].freq/total)));
        double md = log2((double)sum * sum /  ((double) total * AVG_WINDOW_SIZE * AVG_WINDOW_SIZE * _vocab[w1].freq * _vocab[last_w2].freq));
        collocators.push_back ( {last_w2, sum, pmi, pmi / (-log2(((double) sum / AVG_WINDOW_SIZE / total))), /* normalize to [-1,1] */
							calculateLLR(_vocab[w1].freq, total, sum, _vocab[last_w2].freq), md, md + log2((double)sum / AVG_WINDOW_SIZE / total), pmi*sum/total/AVG_WINDOW_SIZE} );
        last_w2 = w2;
        sum = value;
      } else {
        sum += value;
      }
    }

		sort(collocators.begin(), collocators.end(), sortByLfmd);
		
    int i=0;
    for (Collocator c : collocators) {
      if(i++>10) break;
      std::cout << "dont call me w1:" << _vocab[w1].word << ", w2:" << _vocab[c.w2].word
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
		return collocators;
  }

	std::vector<Collocator> rocksdb::CollocatorDB::get_collocators(uint32_t w1) {
		std::vector<Collocator> collocators;
    uint64_t w2, last_w2 = 0xffffffffffffffff;
    uint64_t maxv = 0, left = 0, right = 0, total_w1 = 0;
    const double window_size = 1;
    for ( auto it = std::unique_ptr<CollocatorIterator>(SeekIterator(w1, 0, 0)); it->isValid(); it->Next()) {
      uint64_t value = it->intValue(),
        key = it->intKey();
      w2 = W2(key);
      total_w1 += value;
      if(last_w2 == 0xffffffffffffffff) last_w2 = w2;
      if (w2 != last_w2) {
				double pmi = ca_pmi(_vocab[w1].freq, _vocab[last_w2].freq, maxv, total, window_size);
        double lfmd = ca_lfmd(_vocab[w1].freq, _vocab[last_w2].freq, maxv, total, window_size);
        double left_lfmd = ca_lfmd(_vocab[w1].freq, _vocab[last_w2].freq, left, total, 1);
        double right_lfmd = ca_lfmd(_vocab[w1].freq, _vocab[last_w2].freq, right, total, 1);
        double left_npmi = ca_npmi(_vocab[w1].freq, _vocab[last_w2].freq, left, total, 1);
        double right_npmi = ca_npmi(_vocab[w1].freq, _vocab[last_w2].freq, right, total, 1);
        collocators.push_back ( {last_w2, maxv, pmi, pmi / (-log2(((double) maxv / window_size / total))), /* normalize to [-1,1] */
							calculateLLR(_vocab[w1].freq, total, maxv, _vocab[last_w2].freq), lfmd, pmi*maxv/total/window_size,
              left_lfmd,
              right_lfmd,
              left_npmi,
              right_npmi}
          );
        last_w2 = w2;
        maxv = value;
      } else {
        if(value > maxv)
          maxv = value;
      }
      if(DIST(key) == -1)
        left = value;
      else if(DIST(key) == 1)
        right = value;
    }

		sort(collocators.begin(), collocators.end(), sortByLfmd);
		
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
		return collocators;
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
          double llr = calculateLLR(_vocab[w1].freq, total, maxv, _vocab[last_w2].freq);
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

string rocksdb::CollocatorDB::collocators2json(vector<Collocator> collocators) {
  ostringstream s;
  int i = 0;
  s << "[";
  bool first = true;
  for (Collocator c : collocators) {
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
      "\"llr\":" << c.llr   << "," <<
      "\"lfmd\":" << c.lfmd  << "," <<
      "\"fpmi\":" << c.fpmi  << "," <<
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

	const char *get_collocators_as_json(COLLOCATORS *db, uint32_t w1) {
		return strdup(db->collocators2json(db->get_collocators(w1)).c_str());
	}
}
