/* ambench.c - harness to benchmark index construction methods
 *
 * This main reads "student.txt" in the repo root (or change path below).
 * It runs:
 *   - incremental build (AM_BuildIndexIncremental)
 *   - sorted insert build (AM_BuildIndexFromExistingFile)
 *   - bulk load build (AM_BulkLoadFromFileSorted)
 *
 * Make sure amlayer is compiled with -I../pflayer and link with pflayer objects.
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>

#include "am.h"
#include "../pflayer/pf.h"
#include "amstats.h"

/* declare functions from ambuild/ambulk */
int AM_BuildIndexIncremental(char *, int, char, int, char *, int);
int AM_BuildIndexFromExistingFile(char *, int, char, int, char *, int);
int AM_BulkLoadFromFileSorted(char *, char *, int, char, int);

static long timeval_diff_ms(struct timeval *a, struct timeval *b)
{
    return (a->tv_sec - b->tv_sec) * 1000 + (a->tv_usec - b->tv_usec) / 1000;
}

int main()
{
    char *dataFile  = "../../data/student.txt";
    char *indexFile = "student";   // IMPORTANT
    int dataFd      = 0;
    char attrType   = INT_TYPE;
    int attrLen     = sizeof(int);
    int status;

    printf("PF/AM Benchmark: data=%s\n", dataFile);

    /* Initialize PF buffer pool */
    PF_Init(50);

    /* Method 1: incremental */
    printf("\n=== Method: Incremental Insert ===\n");
    PFbufStatsInit();
    status = AM_BuildIndexIncremental(dataFile, dataFd, attrType, attrLen, indexFile, 1);
    if (status != AME_OK) printf("Incremental failed: %d\n", status);
    PFbufStatsPrint();
    printf("Time (ms): %.2f\n", AMstats.time_ms);

    /* Method 2: sorted-then-insert */
    printf("\n=== Method: Sorted Insert ===\n");
    PFbufStatsInit();
    status = AM_BuildIndexFromExistingFile(dataFile, dataFd, attrType, attrLen, indexFile, 2);
    if (status != AME_OK) printf("Sorted insert failed: %d\n", status);
    PFbufStatsPrint();
    printf("Time (ms): %.2f\n", AMstats.time_ms);

    /* Method 3: bulk load (only if implemented) */
    printf("\n=== Method: Bulk Load from sorted pairs ===\n");
    PFbufStatsInit();
    status = AM_BulkLoadFromFileSorted(dataFile, indexFile, 3, attrType, attrLen);
    if (status != AME_OK) printf("Bulk load failed: %d\n", status);
    PFbufStatsPrint();
    printf("Time (ms): %.2f\n", AMstats.time_ms);

    return 0;

}
