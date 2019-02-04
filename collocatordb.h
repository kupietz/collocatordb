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
    uint64_t w2;
    uint64_t f2;
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
    double ldaf;
    int window;
    int af_window;
  };

    class CollocatorIterator : public Iterator  {
    public:
        CollocatorIterator(const Iterator& it);
        void SeekToFirst();
        void SeekToLast();
        void Seek(const rocksdb::Slice&);
        void Prev();
        bool isValid();
        uint64_t intValue();
        uint64_t intKey();
    };
    
		extern "C" {
			class CollocatorDB {
			public:
        std::string getWord(uint32_t w1);
        std::vector<Collocator> get_collocators(uint32_t w1);
        std::vector<Collocator> get_collocators(uint32_t w1, uint32_t max_w2);
        void dumpSparseLlr(uint32_t w1, uint32_t min_cooccur);
        CollocatorDB(const char *db_name, const bool read_only);
        void inc(const uint32_t w1, const uint32_t w2, const uint8_t dist);
        void dump(const uint32_t w1, const uint32_t w2, const uint8_t dist);
        CollocatorIterator* SeekIterator(uint64_t w1, uint64_t w2, int8_t dist);
			};
			
		}
}

typedef rocksdb::CollocatorDB COLLOCATORDB;

#else
typedef struct COLLOCATORDB COLLOCATORDB;
#endif

extern COLLOCATORDB *open_collocatordb(char *s);
extern COLLOCATORDB *open_collocatordb_for_write(char *s);
extern void inc_collocator(COLLOCATORDB *db, uint64_t w1, uint64_t w2, int8_t dist);
extern void dump_collocators(COLLOCATORDB *db, uint32_t w1, uint32_t w2, int8_t dist);
extern void get_collocators(COLLOCATORDB *db, uint32_t w1);
extern char *get_collocators_as_json(COLLOCATORDB *db, uint32_t w1);
extern char *get_word(COLLOCATORDB *db, uint32_t w1);
