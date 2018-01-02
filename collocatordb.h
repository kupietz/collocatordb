#include <typeinfo>
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
#define W1(key) (uint64_t)(key & 0xffffff)
#define W2(key) (uint64_t)((key >> 24) & 0xffffff)
#define DIST(key) (int8_t)((uint64_t)((key >> 56) & 0xff))

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
    
    class Collocators {
    public:
        Collocators(const char *db_name);
        ~Collocators();
        void inc(const uint32_t w1, const uint32_t w2, const uint8_t dist);
        CollocatorIterator* SeekIterator(uint64_t w1, uint64_t w2, int8_t dist);
    };
    
}
