/* ambench.c
 * Simple harness to benchmark index construction methods.
 * Prints a small table containing:
 *  - method name
 *  - time (ms)
 *  - PF logical/physical I/O counts from PFbufStats*
 *
 * Usage:
 *  compile and link with your project along with PF/RM/AM files.
 *
 * NOTE: adapt dataFileName, attrType/Length and calls to RM API as needed.
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>

#include "am.h"
#include "pf.h"
#include "../pflayer/rm.h"

static long timeval_diff_ms(struct timeval *a, struct timeval *b)
{
    return (a->tv_sec - b->tv_sec) * 1000 + (a->tv_usec - b->tv_usec) / 1000;
}

/* Example driver - adapt file names and fds to your environment */
int AM_BenchmarkIndexConstruction(dataFileName, dataFd, attrType, attrLength, indexFileName)
char *dataFileName;
int dataFd;
char attrType;
int attrLength;
char *indexFileName;
{
    struct timeval tstart, tend;
    int indexNo = 1;
    int status;

    printf("Method,Time_ms,LogicalReads,LogicalWrites,PhysicalReads,PhysicalWrites\n");

    /* Method 1: incremental (scan RM -> AM_InsertEntry)
       reset PF stats, run, print */
    PFbufStatsInit();
    gettimeofday(&tstart, NULL);
    status = AM_BuildIndexIncremental(dataFileName, dataFd, attrType, attrLength, indexFileName, indexNo);
    gettimeofday(&tend, NULL);
    if (status != AME_OK) {
        fprintf(stderr, "Incremental build failed: %d\n", status);
    }
    printf("Incremental,%ld,", timeval_diff_ms(&tend, &tstart));
    PFbufStatsPrint();

    /* Method 2: collect & sort then insert sequentially (simulates single operation with sorted inserts) */
    indexNo++;
    PFbufStatsInit();
    gettimeofday(&tstart, NULL);
    status = AM_BuildIndexFromExistingFile(dataFileName, dataFd, attrType, attrLength, indexFileName, indexNo);
    gettimeofday(&tend, NULL);
    if (status != AME_OK) {
        fprintf(stderr, "Sorted-build (bulk) failed: %d\n", status);
    }
    printf("BulkSorted,%ld,", timeval_diff_ms(&tend, &tstart));
    PFbufStatsPrint();

    /* Method 3: Bulk loader called directly - if you have arrays ready you can call AM_BulkLoadFromSortedPairs
       For demonstration, reuse Method 2's behavior (as it calls AM_BulkLoadFromSortedPairs internally).
    */

    return 0;
}
