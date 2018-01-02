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
			class Collocators {
			public:
        Collocators(const char *db_name);
        ~Collocators();
        void inc(const uint32_t w1, const uint32_t w2, const uint8_t dist);
        void dump(const uint32_t w1, const uint32_t w2, const uint8_t dist);
        CollocatorIterator* SeekIterator(uint64_t w1, uint64_t w2, int8_t dist);
			};
			
		}
}

typedef rocksdb::Collocators COLLOCATORS;

#else
typedef struct COLLOCATORS COLLOCATORS;
#endif

extern COLLOCATORS *open_collocators(char *s);
extern void inc_collocators(COLLOCATORS *db, uint64_t w1, uint64_t w2, int8_t dist);
extern void dump_collocators(COLLOCATORS *db, uint32_t w1, uint32_t w2, int8_t dist);
