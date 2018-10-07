#include <typeinfo>
#include <assert.h>
#include <memory>
#include <iostream>
#include <stdint.h>
#include "collocatordb.h"
#include <thread>
#include <chrono>
#include <sstream> // for ostringstream
#include <fstream>

using namespace rocksdb;

  
int main(int argc, char** argv) {
  const int START=1;
  const int STOP=300000;
  uint32_t *array;
  int done = 0;

  array = (uint32_t *)malloc(STOP * 20 * sizeof(uint32_t));
  memset(array, 0, STOP * 20 * sizeof(uint32_t));
  FILE* pFile;
  CollocatorDB cdb = CollocatorDB(argv[1], true);
  std::cerr << "Database " << argv[1] << " opened\n";
  #pragma omp parallel for
  for(uint32_t i=START; i< STOP; i++) {
    std::vector<rocksdb::Collocator> cs = cdb.get_collocators(i);
    int j=0;
    for (rocksdb::Collocator c : cs) {
      array[i*20+j] = (uint32_t) c.w2;
      if(++j >=20)
        break;
    }
    if(done++  % 100 == 0) {
      std::cerr <<"\r\033[2K"<<std::flush;
      std::cerr << "done: " << done * 100.0 / (STOP-START) << "%" <<std::flush;
    }
  }
  pFile = fopen("file.binary", "wb");
  fwrite(array, STOP*20, sizeof(uint32_t), pFile);
  fclose(pFile);
  std::cout << std::flush;
}
