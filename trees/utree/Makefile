CXX:= g++
FLAGS:= -O3 -g -std=c++17 -DUSE_PMDK -Wl,-rpath=/home/cwk/cwk/pmdklib/lib:/home/cwk/cwk/oneTBB/build/gnu_10.3_cxx11_64_release -mavx512f -mavx512vl -mavx512bw -mavx512dq -mavx512cd -mrtm
LIBS:= -lpmemobj -ljemalloc -ltbb -lpthread
LIBS_DIR:= -L../../../pmdklib/lib -L../../../oneTBB/build/gnu_10.3_cxx11_64_release 
INCLUDE_DIR:=-I. -I../../../oneTBB/include -I../../../pmdklib/include
EXE_SRCS:= utree.h
LIB_TARGET:= libutree.so
all: wrapper_fp

wrapper_fp:
	$(CXX) $(FLAGS) -shared $(EXE_SRCS) $(LIBS_DIR) $(INCLUDE_DIR) $(LIBS) -fPIC -o $(LIB_TARGET)
clean:
	rm -rf *.so