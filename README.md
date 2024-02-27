# ZBTree

A read-optimized PMem friendly B+-Tree

## Dependencies
[PMDK](https://github.com/pmem/pmdk)

[TBB](https://github.com/oneapi-src/oneTBB)

[YCSB](https://github.com/HNUSystemsLab/Halo/tree/main/YCSB)

Please follow their guidence to install related dependecies.

## How to build

### Build ZBTree:
call **make** to generate dynamic lib for ZBTree and binary for benchmark.
### Build other indices:
change indices name command line:

For example:
```
make fptree
```

## How to Run

See `.sh` files



