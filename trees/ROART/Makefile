CC=g++
# CC=clang++

# Flag for debugging runs
# CFLAGS=-O0 -g -std=c++11 -pthread -mrtm -msse4.1 -mavx2

# Flag for test runs
override CFLAGS +=-O3 -g -std=c++17 -mrtm -msse4.1 -mavx2 -Wl,-rpath=/home/cwk/cwk/pmdklib/lib:/home/cwk/cwk/oneTBB/build/gnu_10.3_cxx11_64_release # -DARTPMDK
#CFLAGS=-g -std=c++17 -pthread -msse4.1 -mavx2 -Wall -O3 -fsanitize=undefined,implicit-conversion,nullability,float-divide-by-zero,unsigned-integer-overflow,bounds,bool

INCLUDE:= -I./ART -I./nvm_mgr  -I../../../pmdklib/include -I../../../oneTBB/include
LIBDIR:= -L../../../pmdklib/lib -L../../../oneTBB/build/gnu_10.3_cxx11_64_release
LIB:= -lpmem -lpmemobj -ljemalloc -ltbb -lpthread

COMMON_DEPENDS= ./ART/LeafArray.cpp ./ART/N.cpp ./ART/N4.cpp ./ART/N16.cpp ./ART/N48.cpp ./ART/N256.cpp ./ART/Tree.cpp ./nvm_mgr/Epoch.h ./nvm_mgr/Epoch.cpp ./nvm_mgr/EpochGuard.h ./nvm_mgr/nvm_mgr.h ./nvm_mgr/nvm_mgr.cpp ./nvm_mgr/pmalloc_wrap.h ./nvm_mgr/threadinfo.h ./nvm_mgr/threadinfo.cpp ./nvm_mgr/util.h
COMMON_SOURCES= ./ART/LeafArray.cpp ./ART/N.cpp ./ART/N4.cpp ./ART/N16.cpp ./ART/N48.cpp ./ART/N256.cpp ./ART/Tree.cpp ./nvm_mgr/Epoch.cpp ./nvm_mgr/nvm_mgr.cpp ./nvm_mgr/threadinfo.cpp

# -----------------------------------------------------------------------------
TARGETS=roart_wrapper

#wbtree fptree

all: ${TARGETS}

# -----------------------------------------------------------------------------
roart_wrapper: ${COMMON_DEPENDS}
	${CC} ${CFLAGS} -shared ${COMMON_SOURCES} ${INCLUDE} ${LIBDIR} ${LIB} -fPIC -o libroart.so

# -----------------------------------------------------------------------------
clean:
	-rm -rf a.out core *.s ${TARGETS} libroart.so
