#include <typeinfo>
#include <assert.h>
#include <memory>
#include <iostream>
#include <stdint.h>
#include "../src/collocatordb.h"
using namespace rocksdb;

void dumpDb(Collocators& counters) {
    auto it = std::unique_ptr<CollocatorIterator>(counters.SeekIterator(1000,0,0));
    for (; it->isValid(); it->Next()) {
      uint64_t value = it->intValue();
      uint64_t key = it->intKey();
      std::cout << "w1:" << W1(key) << ", w2:" << W2(key) << ", dist:" << (int32_t) DIST(key) << " - count:" << value << std::endl;
    }
    std::cout << "ready dumping\n";
  }

  void testCollocators(Collocators& counters) {
    counters.inc(100,200,5);
    counters.inc(1000,2000,-5);
    counters.inc(1000,2000,5);
    counters.inc(1000,2500,-3);
    counters.inc(1000,2500,4);
    counters.inc(1000,2900,3);

    counters.inc(1001,2900,3);

    for(int i=0; i<10000; i++)
      counters.inc(rand()%1010,rand()%1010,rand()%10-5);

    //  dumpDb(db);

    counters.inc(100,200,5);
    counters.inc(1000,2000,5);
    counters.inc(1000,2500,4);
    counters.inc(1000,2900,3);

    counters.inc(1001,2900,3);

    dumpDb(counters);
    std::cout << "ready testing\n";
  }

  
int main() {
    Collocators counters = "/tmp/cdb";
    std::cout << "testing now\n";
    testCollocators(counters);
    std::cout << "ready running\n";
    return 0;
}
