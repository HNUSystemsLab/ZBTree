for thread in 4 8 16 28 
do
    sudo rm -rf /mnt/pmem/zbtree/*
    sudo numactl -N 0 ./main $thread f
done