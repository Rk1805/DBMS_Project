# DBMS_Project


# ToyDB Project — PF / RM / AM Layers

## Overview
This project implements three core components of a miniature database system:

- **PF layer (Page & Buffer Manager)**  
  - Buffer pool with configurable size.  
  - Two replacement policies (selectable per-file): **LRU** and **MRU**.  
  - Per-page dirty flag and explicit call to mark a page dirty.  
  - Counters for logical/physical reads and writes.

- **RM layer (Record Manager)**  
  - Slotted-page structure for variable-length records.  
  - Supports insertion, deletion, and sequential scanning.  
  - Computes slotted-page space utilization and compares with static fixed-length packing.

- **AM layer (Index Manager)**  
  - Three index construction strategies for roll-number key:  
    1. Incremental inserts (one-by-one)  
    2. Collect → sort → insert sequentially  
    3. Bulk load from sorted input (best for pre-sorted data)  
  - Benchmarks (time + PF I/O counters) compare these methods.

---

## Build instructions

> The project uses a K&R / C89-compatible style. Makefiles supplied in the project expect `cc -std=c89`. If your compiler defaults to newer C, ensure `-std=c89` is passed in `CFLAGS`.

### Build PF layer
Run:
```bash
cd pflayer
make clean
make pf_test
````

### Build RF layer
Run:
```bash
cd pflayer
make clean
make rmtest
````

This builds PF objects (e.g., `pf.o`, `buf.o`, `hash.o`, `rm.o`).

### Build AM benchmark driver

Run:

```bash
cd amlayer
make clean
make amtest
```

This builds the benchmark executable `amtest` that exercises AM index construction and prints PF statistics.

---

## Run instructions & configuration

### PF statistics test (example)

There is a PF test driver (e.g., `pf_test`). Usage:

```bash
./pf_test
```

Configuration points inside the test:

* `PF_Init(<num_buffers>)` — buffer pool size.
* `PF_OpenFile("filename", strategy)` — `strategy` is `PF_REPLACE_LRU` or `PF_REPLACE_MRU`.

**What to expect**: output showing logical/physical reads & writes across several read/write mixtures. Example:

```
=== Running mixture: 100 READS / 0 WRITES (strategy=LRU) ===
Logical Reads: 100
Logical Writes: 10
Physical Reads: 100
Physical Writes: 10
...
```

### RM test (slotted-page)

Run the RM test program:

```bash
./rmtest
```

This will:

* Insert a number of student records (configurable in test source),
* Compute per-page slotted statistics (payload, slots, deleted slots),
* Build a comparison table showing utilization for static record sizes (e.g., 32, 64, 128, 256 bytes).

**Example output:**

```
Pages used: 310
Total payload bytes: 1237475
Total slots: 5000
Total deleted slots: 0
Slotted-page utilization: 97.46%

Static table:
----------------------------------------------
| Static Size | rec/page | Static Util | Slotted Util |
----------------------------------------------
|         32 |      128 |     100.00 |        97.46 |
...
```

### AM benchmark (index construction)

Run:

```bash
./amtest
```

Configuration inside `ambench.c`:

* `dataFile` — path to your dataset (e.g., `student.txt`).
* `indexFile` — base name for index files (e.g., `student_index`).
* `PF_Init(<num>)` — choose buffer pool size before running.

**What it prints**:

* For each method (incremental, sorted-then-insert, bulk load): time (ms), and PF logical/physical I/O counters.

**Example output:**

```
PF/AM Benchmark: data=student.txt

=== Method: Incremental Insert ===
Logical Reads: 100
Logical Writes: 200
Physical Reads: 150
Physical Writes: 75
Time (ms): 1234.56

=== Method: Sorted Insert ===
Logical Reads: ...
...
=== Method: Bulk Load ===
...
```

---

## Experiments required & expected deliverables

1. **PF buffering experiments**

   * Vary read/write mixture (e.g., 100/0, 80/20, 50/50, 20/80, 0/100).
   * For each mixture and replacement strategy (LRU, MRU):

     * Report logical reads, logical writes, physical reads, physical writes.
   * Plot graphs:

     * X-axis = read/write mixture, Y-axis = chosen statistic (e.g., physical reads).
     * Compare LRU vs MRU.

2. **RM slotted-page vs static**

   * Insert a dataset of student records.
   * Compute slotted-page utilization and show pages used, payload bytes, total slots, deleted slots.
   * For static record sizes (several max lengths), compute records-per-page and utilization.
   * Present a table comparing static vs slotted utilization.

3. **AM index construction comparison**

   * Run all three index construction methods for the same data set.
   * Measure: time (ms), logical reads/writes, physical reads/writes, pages accessed.
   * Present results in a table/CSV for easy comparison.
   * If dataset is pre-sorted, show bulk-load advantage.


---

## Troubleshooting tips

* **Index or data files already exist**: Remove old index files like `student_index.0`, `.1`, `.2` before rerunning.
* **Segfault / abort during AM test**: Check data file parsing logic (AM helpers expect semicolon-separated lines and a valid integer roll-number in the second field). Use a small subset to verify parsing.
* **Undefined symbol at link**: Make sure PF object files are compiled and linked into the AM executable; do not `#include` `.c` files inadvertently.
* **Duplicate symbol errors**: Ensure functions are defined only once (move common global variables into a single `.c` and declare `extern` in headers).
* **K&R vs ANSI prototype mismatches**: Project code uses old-style function definitions. Build with `-std=c89` and avoid mixing modern prototypes unless you update all functions consistently.

---

```sh

```
