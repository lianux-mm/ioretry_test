# ioretry_test
A stress testing suite designed to reproduce and quantify the mmap_lock retry inefficiency in filemap_fault() under severe memory pressure and scheduler contention.

# Background
In the major fault path, when using mmap_lock, the lock is dropped while I/O is in flight. If the faulting task is delayed (e.g., due to CPU contention) before it can re-acquire the lock, the newly fetched folios can be aggressively evicted by LRU reclaim. Consequently, the completed I/O work is lost, forcing the task to retry the fault and severely amplifying latency.

This repository provides a reproducible environment to observe this exact window between I/O completion and lock reacquisition, simulating a scenario where thousands of threads batter the storage re-reading data due to continuous reclaim.

### Repository Structure
1.vmstat_count.patch: A kernel patch that adds temporary instrumentation to /proc/vmstat to track the retry behavior:

- retry_io_miss: Counts instances where the folio is no longer present in the page cache after I/O completion.

- retry_mmap_drop: Counts retries where mmap_lock is dropped for I/O.


2.ioretry_base.c: The core faulting workload. It spawns hundreds of threads continuously triggering filemap_fault() on a file.

3.ioretry_noise.c: The advanced reproducer. It runs the core faulting threads alongside unrelated background threads pinned to the same CPUs. This deliberately introduces scheduler contention (e.g., ~10us EEVDF slice), extending the lock-free window and making the original reclaim race significantly more severe.

4.ioretry.sh: The automation script. It sets up the cgroup limits (1GB memory limit), executes the workload, and calculates the steady-state deltas from /proc/vmstat.

## How to Test
- 1. Apply the Kernel Patch
Apply vmstat_count.patch to your kernel tree, recompile, and boot into the new kernel(base commit mm/master b4f0dd314b39ea154f62f3bd3115ed0470f9f71e).

- 2. Compile the Reproducers

```
gcc -O2 -pthread ioretry_base.c -o ioretry_base
gcc -O2 -pthread ioretry_noise.c -o ioretry_noise
```

- 3. Run the Stress Test
Execute the shell script as root (it requires systemd-run for cgroup management):

```
sudo ./ioretry.sh
```
(Note: You may need to edit ioretry.sh to point to either ioretry_base or ioretry_noise depending on your test target).

## Evaluation Results
Running ioretry_noise on a 256-core x86 system restricted to a 1GB memory cgroup yields the following steady-state deltas.

Comparing the vanilla kernel (Without VMA retry) vs. the kernel with the per-VMA lock retry patchset:
