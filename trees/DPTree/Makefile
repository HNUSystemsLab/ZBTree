CXX:= g++ 
FLAGS:=-O3 -g -std=c++17 -DNDEBUG -DUSE_CLWB -fpermissive -ftree-vectorize -msse -msse2 -mavx -mrtm -mavx2 -mavx512f -mavx512vl -mavx512bw -mavx512dq -mavx512cd -DHAS_AVX512 -Wl,-rpath=/home/cwk/cwk/pmdklib/lib 
LIBS:= -lpmem -lpmemobj -ljemalloc -ltbb -lpthread
LIBS_DIR:= -L../../../pmdklib/lib 
DEFINES:=PMEM
INCLUDE_DIR:=-I./include -I./misc -I../../../pmdklib/include  -I../../../gperftools/src/gperftools
LIB_TARGET:=libdptree.so
LIB_SRCS:= src/art_idx.cpp src/util.cpp src/bloom.c src/MurmurHash2.cpp
all: wrapper_dp
# .cpp.o:
# 	$(CXX) $(FLAGS) $(LIBS_DIR) $(INCLUDE_DIR) $(LIBS) -o $@ -c $< -fPIC
wrapper_dp:
	$(CXX) $(FLAGS) -shared $(LIB_SRCS)  $(LIBS_DIR) $(INCLUDE_DIR) $(LIBS) -fPIC -o $(LIB_TARGET)
clean:
	rm -rf *.o fptree *.so