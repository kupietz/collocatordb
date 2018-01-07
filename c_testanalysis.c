#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "collocatordb.h"

uint64_t total=0;

vocab_entry vocab[100000];

void read_vocab(char *fname) {
  char strbuf[2048];
  long long freq;
	FILE *fin = fopen(fname, "rb");
	if (fin == NULL) {
		printf("Vocabulary file not found\n");
		exit(1);
	}
  uint64_t i = 0;
  while(!feof(fin)) {
		fscanf(fin, "%s %lld", strbuf, &freq);
    vocab[i].word = strdup(strbuf);
    vocab[i].freq = freq;
    total += freq;
    i++;
  }
  fclose(fin);
}

int main() {
	COLLOCATORS *cdb = open_collocators("/vol/work/kupietz/Work2/kl/trunk/Analysemethoden/wang2vec/sampledb");
  read_vocab("/vol/work/kupietz/Work2/kl/trunk/Analysemethoden/wang2vec/sample.vocab");
  for(int i=500; i < 600; i++)
    get_collocators(cdb, i, vocab, total);
  printf("%s\n", get_collocators_as_json(cdb, 500, vocab, total));
	return 0;
}
