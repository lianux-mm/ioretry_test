#!/bin/bash

echo "Recording initial vmstat data..."
val_drop_start=$(grep "retry_mmap_drop" /proc/vmstat | awk '{print $2}')
val_miss_start=$(grep "retry_io_miss" /proc/vmstat | awk '{print $2}')

echo "Starting stress test workload (Memory limit: 1G)..."
# Run the benchmark in the background and terminate it after the timeout
systemd-run --scope -p MemoryHigh=1G -p MemoryMax=1.2G -p MemorySwapMax=0 --unit=mmap-thrash-$$ ./mmap_lock &
TEST_PID=$!

# Run for 10 minutes (600 seconds)
sleep 600

# Cleanup the test environment
systemctl stop mmap-thrash-$$.scope 2>/dev/null
kill $TEST_PID 2>/dev/null

echo "Test finished, calculating vmstat deltas..."
val_drop_end=$(grep "retry_mmap_drop" /proc/vmstat | awk '{print $2}')
val_miss_end=$(grep "retry_io_miss" /proc/vmstat | awk '{print $2}')

diff_drop=$((val_drop_end - val_drop_start))
diff_miss=$((val_miss_end - val_miss_start))

echo "=========================================================="
echo "Total mmap_lock drops (RETRY_MMAP_DROP) : $diff_drop"
echo "Pages reclaimed after I/O (RETRY_IO_MISS): $diff_miss"

if [ "$diff_drop" -gt 0 ]; then
    ratio=$(echo "scale=2; $diff_miss * 100 / $diff_drop" | bc)
    echo "I/O invalidation ratio (Miss/Drop)       : $ratio%"
fi
echo "=========================================================="
