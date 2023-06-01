CXX:=g++
CFLAGS:= -std=c++11 -DUSE_NVM_MALLOC -DCLEAR_NVM_POOL -ggdb -O3 -mrtm
RPATH:= -Wl,-rpath=/home/cwk/cwk/pmdklib/lib:/home/cwk/cwk/ZBTree/trees/nbtree/third-party-lib
INCLUDE_DIR:=-I./include -I../../../pmdklib/include -I./third-party-lib/tbb
LIBS_DIR:=-L../../../pmdklib/lib -L./third-party-lib
LIBS:=-lpmemobj -pthread -ltbb
SHARE_FLAG:=-fPIC
SOURCE:=./src/nbtree.cpp
all: wrapper_nb

wrapper_nb:
	$(CXX) $(CFLAGS) $(RPATH) -shared $(SOURCE) $(INCLUDE_DIR) $(LIBS_DIR) $(LIBS) $(SHARE_FLAG) -o libnbtree.so
clean:
	rm -rf *.so