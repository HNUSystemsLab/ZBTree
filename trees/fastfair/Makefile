CXX:= g++
FLAGS:= -O3 -g -std=c++17 -DPMEM -Wl,-rpath=/home/cwk/cwk/pmdklib/lib:/home/cwk/cwk/oneTBB/build/gnu_10.3_cxx11_64_release # -mavx512f -mavx512vl -mavx512bw -mavx512dq -mavx512cd # -mrtm
LIBS:= -lpmemobj -ljemalloc -ltbb -lpthread
LIBS_DIR:= -L../../../pmdklib/lib -L../../../oneTBB/build/gnu_10.3_cxx11_64_release 
INCLUDE_DIR:=-I. -I../../../oneTBB/include -I../../../pmdklib/include
EXE_TARGET:= fastfair
LIB_TARGET:= libfast.so
EXE_SRCS:= fastfair.h
all: wrapper_fp
fptree:
	$(CXX) $(FLAGS) $(EXE_SRCS) $(LIBS_DIR) $(INCLUDE_DIR) $(LIBS) -o $(EXE_TARGET) 
# .cpp.o:
# 	$(CXX) $(FLAGS) $(LIBS_DIR) $(INCLUDE_DIR) $(LIBS) -o $@ -c $< -fPIC
wrapper_fp:
	$(CXX) $(FLAGS) -shared $(LIB_SRCS) $(EXE_SRCS) $(LIBS_DIR) $(INCLUDE_DIR) $(LIBS) -fPIC -o $(LIB_TARGET)
clean:
	rm -rf *.so