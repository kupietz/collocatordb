#!/usr/bin/python3
from __future__ import print_function
import numpy 
import sys 
import nmslib 
import time 
import math 
import os.path
from scipy.sparse import csr_matrix

def eprint(*args, **kwargs):
    print(*args, file=sys.stderr, **kwargs)

def read_data(filename, max_qty = None): 
    row = []
    col = []
    data = []
    read_qty = 0
    row_max = 0
    with open(filename,'r') as f:
        read_num_ft = 0
        for line in f:
            x = line.strip().split()
            if (len(x) == 0): continue
            if (len(x) % 2 != 0):
                raise(Exception('Poorly formated line %d in file %s' % (read_qty + 1, filename)))
            for i in range(0, len(x), 2):
                #row.append(int(x[0])-1) 
                row.append(read_qty) 
                feat_id = int(x[i])
                read_num_ft = max(read_num_ft, feat_id + 1)
                col.append(feat_id)
                data.append(float(x[i+1]))

            read_qty = read_qty+1
            # row_max = max(row_max, int(x[0]))
            if max_qty != None and read_qty >= max_qty: break
            if (read_qty % 10) == 0:
                eprint('Read %d rows' % read_qty)
    eprint('Read %d rows, # of features %d' %  (read_qty, read_num_ft))
    ft_mat = csr_matrix((numpy.array(data), (numpy.array(row), numpy.array(col))), 
                        shape=(read_qty, read_num_ft)) 
    return (read_qty, ft_mat)

input_file = sys.argv[1]

# This file will contain nearest neighbors, one per line:
# node [tab char] neighbor_1 neighbor_2 ...

out_file = input_file + ".bin"

(all_qty, data_matrix) = read_data(input_file)
# Set index parameters
# These are the most important onese
M = 30
efC = 100

num_threads = 70
index_time_params = {'M': M, 'indexThreadQty': num_threads, 'efConstruction': efC, 'post' : 0}
K=100
# Intitialize the library, specify the space, the type of the vector and add data points 
index = nmslib.init(method='hnsw', space='cosinesimil_sparse', data_type=nmslib.DataType.SPARSE_VECTOR) 
index.addDataPointBatch(data_matrix)
eprint('Starting index creation.')
# Create an index
start = time.time()
index.createIndex(index_time_params, print_progress=True) 
end = time.time() 
eprint('Index-time parameters', index_time_params)
eprint('Indexing time = %f' % (end-start))

# Setting query-time parameters
efS = 100
query_time_params = {'efSearch': efS}
eprint('Setting query-time parameters', query_time_params)
index.setQueryTimeParams(query_time_params)
# Querying
query_qty = data_matrix.shape[0]
start = time.time() 
nbrs = index.knnQueryBatch(data_matrix, k = K, num_threads = num_threads)
end = time.time() 
eprint('kNN time total=%f (sec), per query=%f (sec), per query adjusted for thread number=%f (sec)' % 
      (end-start, float(end-start)/query_qty, num_threads*float(end-start)/query_qty))

for i in range(0, len(nbrs), 1):
    for j in range(0, len(nbrs[i][0]), 1):
        print("%d %f " % (nbrs[i][0][j], nbrs[i][1][j]), end='')
    print()
#index.saveIndex('sparse_index.bin')
