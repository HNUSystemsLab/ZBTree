CXX:= g++
FLAGS:= -O3 -ggdb -std=c++17 -mrtm -fopenmp -mavx2 -mavx512f -mavx512vl -mavx512bw -mavx512dq -mavx512cd -msse -msse2 -DUSE_PMEM
NORMAP_RPATH:=-Wl,-rpath=/home/cwk/cwk/pmdklib/lib:/home/cwk/cwk/ZZBTree
TBB_RPATH:=-Wl,-rpath=/home/cwk/cwk/oneTBB/build/gnu_10.3_cxx11_64_release:/home/cwk/cwk/pmdklib/lib:/home/cwk/cwk/ZZBTree:/home/cwk/cwk/ZZBTree/third/CLHT:/home/cwk/cwk/ZZBTree:/home/cwk/cwk/ZZBTree/third/CLHT/external/lib
LIBS:= -lpmem -lpmemobj -ljemalloc -ltbb -lclht -lssmem -pthread # -lpcm 
LIBS_DIR:= -L. -L../pmdklib/lib -L../jemalloc/lib -L./third/CLHT -L./third/CLHT/external/lib
TBB_LIB:=-L../oneTBB/build/gnu_10.3_cxx11_64_release
INCLUDE_DIR:=-I./include -I../pmdklib/include -I./third/CLHT/include  -I./third/pcm# -I../../pibench/include
TBB_DIR:=-I../oneTBB/include
LIB_TARGET:= libnyx_seele.so
# LIB_SRCS:= nyx_na.cpp n_alc.cpp pm_util.cpp # elf_seele.cpp # search.cpp
VAR_SRCS:= zbtree.cpp n_alc.cpp pm_util.cpp 
MAIN_SRCS:= main.cpp 
check_key:=$(ARG1) # -DCHECK_KEY
avx_512:=$(ARG2) # -DAVX_512
all: single_wrapper_so exe_out # pibench_wrapper_so

fptree_so:
	make -C trees/fptree
	cp trees/fptree/libfptree.so .
	$(CXX) $(FLAGS) $(TBB_RPATH) -DUSE_FPTREE $(check_key) $(avx_512) main.cpp $(LIBS_DIR) $(TBB_LIB) $(TBB_DIR) $(INCLUDE_DIR) -I./trees/fptree $(LIBS) -lfptree -o main

nbtree_so:
	make -C trees/nbtree
	cp trees/nbtree/libnbtree.so .
	$(CXX) $(FLAGS) $(NORMAP_RPATH) -DUSE_NBTREE $(check_key) $(avx_512) main.cpp $(LIBS_DIR) $(INCLUDE_DIR) -I./trees/nbtree/include $(LIBS) -lnbtree -o main

fast_so:
	make -C trees/fastfair
	cp trees/fastfair/libfast.so .
	$(CXX) $(FLAGS) $(TBB_RPATH) -DUSE_FASTFAIR $(check_key) $(avx_512) main.cpp $(LIBS_DIR) $(TBB_LIB) $(TBB_DIR) $(INCLUDE_DIR) -I./trees/fastfair $(LIBS) -lfast -o main

roart_so:
	make -C trees/ROART
	cp trees/ROART/libroart.so .
	$(CXX) $(FLAGS) $(TBB_RPATH) -DUSE_ROART $(check_key) $(avx_512) main.cpp $(check_key) $(LIBS_DIR) $(TBB_LIB) $(TBB_DIR) $(INCLUDE_DIR) -I./trees/ROART/ART -I./trees/ROART/nvm_mgr $(LIBS) -lroart -o main

dptree_so:
	make -C trees/DPTree
	cp trees/DPTree/libdptree.so .
	$(CXX) $(FLAGS) $(TBB_RPATH) -DUSE_DPTREE $(check_key) $(avx_512) main.cpp $(check_key) $(LIBS_DIR) $(TBB_LIB) $(TBB_DIR) $(INCLUDE_DIR) -I./trees/DPTree/include -I./trees/DPTree/misc $(LIBS) -ldptree -o main

utree_so:
	make -C trees/utree
	cp trees/utree/libutree.so .
	$(CXX) $(FLAGS) $(TBB_RPATH) -DUSE_UTREE $(check_key) $(avx_512) main.cpp $(LIBS_DIR) $(TBB_LIB) $(TBB_DIR) $(INCLUDE_DIR) -I./trees/utree $(LIBS) -lutree -o main

pactree_so:
	cp trees/pactree/build/src/libpactree.so .
	$(CXX) $(FLAGS) $(TBB_RPATH) -DUSE_PACTREE $(check_key) $(avx_512) main.cpp $(LIBS_DIR) $(TBB_LIB) $(TBB_DIR) $(INCLUDE_DIR) -I./trees/pactree/src -I./trees/pactree/include $(LIBS) -lpactree -o main

single_wrapper_so:
	$(CXX) $(FLAGS) $(TBB_RPATH)  $(check_key) $(avx_512) -shared $(VAR_SRCS) $(LIBS_DIR) $(TBB_LIB) $(INCLUDE_DIR) $(TBB_DIR) $(LIBS) -fPIC -o $(LIB_TARGET)
exe_out:
	$(CXX) $(FLAGS) $(TBB_RPATH) main.cpp $(check_key) $(avx_512) $(VAR_SRCS) $(LIBS_DIR) $(TBB_LIB) $(INCLUDE_DIR) $(TBB_DIR) $(LIBS) -lnyx_seele  -o main
test_cpp:
	$(CXX) $(FLAGS) test.cpp $(LIBS_DIR) $(INCLUDE_DIR) $(LIBS) -o testout
clean:
	rm -rf *.so main output libnyx_seele.so libnyx_pibench.so