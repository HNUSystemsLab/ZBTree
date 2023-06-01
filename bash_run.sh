export PMEM_NO_FLUSH=1
for workloads in c
do
    for threads in 56
    do
        sudo rm -rf /mnt/pmem/nbpool
        sudo rm -rf /mnt/pmem/fppool
        sudo rm -rf /mnt/pmem/fast
        sudo rm -rf /mnt/pmem/zbtree/*
        sudo rm -rf /mnt/pmem/dppool
        sudo rm -rf /mnt/pmem/utree
        sudo rm -rf /mnt/pmem/roart
        sudo numactl -N 0 ./main $threads $workloads >> templatency_$workloads
    done
done
