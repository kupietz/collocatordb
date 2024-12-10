#ifdef __cplusplus
#include <typeinfo>
#include "rocksdb/db.h"
#endif
#include <stdint.h>

#define IS_BIG_ENDIAN (*(uint16_t *)"\0\xff" < 0x100)
#define encodeCollocation(w1, w2, dist) (((uint64_t)dist << 56) | ((uint64_t)w2 << 24) | w1)
#define W1(key) (uint64_t)(key & 0xffffff)
#define W2(key) (uint64_t)((key >> 24) & 0xffffff)
#define DIST(key) (int8_t)((uint64_t)((key >> 56) & 0xff))

#ifdef __cplusplus
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

class CollocatorIterator : public rocksdb::Iterator {
public:
    CollocatorIterator(rocksdb::Iterator* base_iterator);
    void setPrefix(char* prefix);
    void SeekToFirst() override;
    void SeekToLast() override;
    void Seek(const rocksdb::Slice& s) override;
    void SeekForPrev(const rocksdb::Slice& s) override;
    void Prev() override;
    void Next() override;
    rocksdb::Slice key() const override;
    rocksdb::Slice value() const override;
    rocksdb::Status status() const override;
    bool Valid() const override;
    bool isValid();
    uint64_t intValue();
    uint64_t intKey();

private:
    char prefixc[sizeof(uint64_t)];
    rocksdb::Iterator* base_iterator_;
};

class CollocatorDB {
public:
    CollocatorDB(const char* db_name, bool read_only = false);
    void readVocab(std::string fname);
    std::string getWord(uint32_t w1);
    void inc(const uint32_t w1, const uint32_t w2, const uint8_t dist);
    void dump(uint32_t w1, uint32_t w2, int8_t dist);
    std::vector<Collocator> get_collocators(uint32_t w1);
    std::vector<Collocator> get_collocators(uint32_t w1, uint32_t max_w2);
    std::vector<Collocator> get_collocation_scores(uint32_t w1, uint32_t w2);
    std::vector<Collocator> get_collocators(uint32_t w1, uint32_t min_w2, uint32_t max_w2);
    void dumpSparseLlr(uint32_t w1, uint32_t min_cooccur);
    std::string collocators2json(uint32_t w1, std::vector<Collocator> collocators);
    CollocatorIterator* SeekIterator(uint64_t w1, uint64_t w2, int8_t dist);

private:
    std::shared_ptr<rocksdb::DB> OpenDb(const char* dbname);
    std::shared_ptr<rocksdb::DB> OpenDbForRead(const char* dbname);
    void applyCAMeasures(const uint32_t w1, const uint32_t w2, uint64_t* sumWindow, const uint64_t sum, const int usedPositions, int true_window_size, Collocator* result);

    rocksdb::WriteOptions merge_option_;
    char _one[sizeof(uint64_t)];
    rocksdb::Slice _one_slice;
    std::vector<Collocator> _vocab;
    uint64_t total;
    uint64_t sentences;
    float avg_window_size;
    std::shared_ptr<rocksdb::DB> db_;
    rocksdb::WriteOptions put_option_;
    rocksdb::ReadOptions get_option_;
    rocksdb::WriteOptions delete_option_;
    uint64_t default_;
};

} // namespace rocksdb


#else
typedef struct COLLOCATORDB COLLOCATORDB;

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

extern COLLOCATORDB *open_collocatordb(const char *s);
extern COLLOCATORDB *open_collocatordb_for_write(const char *s);
extern void inc_collocator(COLLOCATORDB *db, uint64_t w1, uint64_t w2, int8_t dist);
extern void dump_collocators(COLLOCATORDB *db, uint32_t w1, uint32_t w2, int8_t dist);
extern COLLOCATOR *get_collocators(COLLOCATORDB *db, uint32_t w1);
extern COLLOCATOR *get_collocation_scores(COLLOCATORDB *db, uint32_t w1, uint32_t w2);
extern char *get_collocators_as_json(COLLOCATORDB *db, uint32_t w1);
extern char *get_collocation_scores_as_json(COLLOCATORDB *db, uint32_t w1, uint32_t w2);
extern char *get_word(COLLOCATORDB *db, uint32_t w1);
extern void read_vocab(COLLOCATORDB *db, char *fname);
extern char *get_version();
extern uint64_t get_word_id(COLLOCATORDB *db, const char *word);
extern uint64_t get_corpus_size(COLLOCATORDB *db);
extern uint64_t get_word_frequency(COLLOCATORDB *db, uint64_t w1);

#endif
