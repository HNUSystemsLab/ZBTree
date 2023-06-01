export PMEM_NO_FLUSH=1
for tree in fptree_so # nbtree_so utree_so roart_so dptree_so
do
    make clean
    make $(tree)
    sudo numactl -N 0 ./main 28 c
    sudo rm -rf /mnt/pmem/nbpool
    sudo rm -rf /mnt/pmem/fppool
    sudo rm -rf /mnt/pmem/fast
    sudo rm -rf /mnt/pmem/dppool
    sudo rm -rf /mnt/pmem/roart
    sudo rm -rf /mnt/pmem/zbtree/*
    sudo rm -rf /mnt/pmem/zb_pmdk
    sudo rm -rf /mnt/pmem/utree
done