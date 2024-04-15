# ZBTree

A read-optimized PMem friendly B+-Tree

## Dependencies
[PMDK](https://github.com/pmem/pmdk)

[TBB](https://github.com/oneapi-src/oneTBB)

[YCSB](https://github.com/HNUSystemsLab/Halo/tree/main/YCSB)

Please follow their guidence to install related dependecies.

## Bind PMem
Our system requires Optane PMem device to run.
If you're new to this field, please refer to this [link](https://docs.pmem.io/ndctl-user-guide/managing-nvdimms) to properly config your PMem device.
First, you need to mount your PMem to your system:
```
sudo mkdir /mnt/pmem
sudo mkfs.ext4 /dev/pmem0
sudo mount -o dax /dev/pmem0 /mnt/pmem
```
Now, tape the commond to show mounted list and you should see the output like this.
```
sudo mount -l|grep pmem
/dev/pmem0 on /mnt/pmem type ext4 (rw,relatime,dax=always)
```

## How to build

### Build ZBTree:
call **make** to generate dynamic lib for ZBTree and binary for benchmark.
### Build other indices:
change indices name in command line:
For example, if you want to compile FP-Tree:
```
make fptree
```
### generate YCSB workload for ZBTree
after generate workload follow [YCSB](https://github.com/HNUSystemsLab/Halo/tree/main/YCSB), please change the path in **main.cpp** to your workload's path 

## How to Run
To run different benchmark, you need to change the complie command for make.


See `.sh` files for more details.



