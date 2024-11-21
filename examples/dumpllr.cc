#include <iostream>
#include <vector>
#include <cstdint>
#include <string>
#include <sstream> // for ostringstream

#include <rocksdb/cache.h>
#include <thread>
#include "../src/collocatordb.h"
using namespace rocksdb;


int main(int argc, char** argv) {
  const int START=0;
  const int STOP=1500000;
  int done = 0;
  CollocatorDB cdb = CollocatorDB(argv[1], true);
  std::cerr << "Database " << argv[1] << " opened\n";

  #pragma omp parallel for ordered schedule(static,1)
  for(uint32_t i=START; i< STOP; i++) {
    //    cdb.dumpSparseLlr(i, 5);
    std::vector<rocksdb::Collocator> cs = cdb.get_collocators(i);
    std::stringstream stream;
    // stream << i << "(" << cdb.getWord(i) << "): ";
    if(cs.empty())
      stream << "0 0.0";
    for (rocksdb::Collocator c : cs) {
      stream << c.w2 << " " << c.npmi << " ";
      // stream << c.w2 << "(" << cdb.getWord(c.w2) << ") " << c.llr << " ";
      if(c.raw < 5)
        break;
    }
    stream  << "\n";
    #pragma omp ordered
    std::cout << stream.str();
    if(done++  % 100 == 0) {
      std::cerr <<"\r\033[2K"<<std::flush;
      std::cerr << "done: " << done * 100.0 / (STOP-START) << "%" <<std::flush;
    }
  }
  std::cout << std::flush;
}
