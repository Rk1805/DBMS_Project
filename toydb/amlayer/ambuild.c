/* ambuild.c
 *
 * Two index-construction helpers:
 *  - AM_BuildIndexIncremental : scan data file and AM_InsertEntry for each record
 *  - AM_BuildIndexFromExistingFile : read all keys, sort, then insert in sorted order
 *
 * Data file: semicolon-separated fields, key = 2nd field (roll number)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "am.h"
#include "../pflayer/pftypes.h"
#include "../pflayer/pf.h"
#include "amstats.h"

/* ---------------------------------------------------------
   GLOBALS
   --------------------------------------------------------- */

static int *g_keys = NULL;   /* used by qsort comparator */


/* ---------------------------------------------------------
   Helpers
   --------------------------------------------------------- */

/* extract roll number = SECOND FIELD of semicolon-separated line */
/* Extract second field (roll number). Handles missing fields safely */
static int parse_roll_from_line(const char *line, int *out_roll)
{
    char buf[256];
    int field = 0;
    int bi = 0;

    for (int i = 0; line[i] != '\0'; i++)
    {
        if (line[i] == ';')
        {
            field++;
            continue;
        }

        if (field == 1)       /* SECOND FIELD */
        {
            if (bi < sizeof(buf)-1)
            {
                buf[bi++] = line[i];
            }
        }
        else if (field > 1)   /* stop reading */
            break;
    }

    buf[bi] = '\0';

    if (bi == 0) return -1;   /* empty field */

    *out_roll = atoi(buf);
    return 0;
}


/* timeval difference in milliseconds */
static double timediff_ms(struct timeval *a, struct timeval *b)
{
    return (double)(a->tv_sec - b->tv_sec) * 1000.0 +
           (double)(a->tv_usec - b->tv_usec) / 1000.0;
}

/* qsort comparator: compares keys[ order[i] ] */
static int cmp_order(const void *pa, const void *pb)
{
    int ia = *(const int *)pa;
    int ib = *(const int *)pb;

    if (g_keys[ia] < g_keys[ib]) return -1;
    if (g_keys[ia] > g_keys[ib]) return 1;
    return 0;
}

/* ---------------------------------------------------------
   METHOD 1: Incremental Build (simple scan + InsertEntry)
   --------------------------------------------------------- */

int AM_BuildIndexIncremental(dataFileName, dataFd, attrType, attrLength, indexFileName, indexNo)
char *dataFileName;
int dataFd;  /* ignored */
char attrType;
int attrLength;
char *indexFileName;
int indexNo;
{
    FILE *f;
    char line[2048];
    int err;
    int fdIndex = -1;
    int recid = 0;
    struct timeval t1, t2;

    /* 1. Create index */
    err = AM_CreateIndex(indexFileName, indexNo, attrType, attrLength);
    if (err != AME_OK) return err;

    /* 2. Open index file */
    {
        char idxfname[256];
        sprintf(idxfname, "%s.%d", indexFileName, indexNo);
        fdIndex = PF_OpenFile(idxfname);
        if (fdIndex < 0) {
            return AME_PF;
        }
    }

    /* 3. Open data file */
    f = fopen(dataFileName, "r");
    if (!f) return AME_PF;

    PFbufStatsInit();
    gettimeofday(&t1, NULL);

    /* 4. For each line insert into index */
    while (fgets(line, sizeof(line), f) != NULL) {
        int key;

        if (parse_roll_from_line(line, &key) != 0)
            continue;

        err = AM_InsertEntry(fdIndex, attrType, attrLength,
                             (char *)&key, recid);
        if (err != AME_OK) {
            /* continue anyway */
        }
        recid++;
    }

    gettimeofday(&t2, NULL);
    fclose(f);

    /* 5. record stats */
    AMstats.time_ms = timediff_ms(&t2, &t1);
    AMstats.logicalReads   = PF_logicalReads;
    AMstats.logicalWrites  = PF_logicalWrites;
    AMstats.physicalReads  = PF_physicalReads;
    AMstats.physicalWrites = PF_physicalWrites;
    AMstats.pagesAccessed  = PF_physicalReads + PF_physicalWrites;

    return AME_OK;
}


/* ---------------------------------------------------------
   METHOD 2: Sorted Build (collect → sort → insert ordered)
   --------------------------------------------------------- */

int AM_BuildIndexFromExistingFile(dataFileName, dataFd, attrType, attrLength, indexFileName, indexNo)
char *dataFileName;
int dataFd;  /* ignored */
char attrType;
int attrLength;
char *indexFileName;
int indexNo;
{
    FILE *f;
    char line[2048];
    int err;
    int fdIndex = -1;
    int *keys = NULL;
    int *recids = NULL;
    int *order = NULL;
    int count = 0;
    int capacity = 4096;
    int recid = 0;
    struct timeval t1, t2;
    int i;

    /* 1. Create index file */
    err = AM_CreateIndex(indexFileName, indexNo, attrType, attrLength);
    if (err != AME_OK) return err;

    /* 2. Open index */
    {
        char idxfname[256];
        sprintf(idxfname, "%s.%d", indexFileName, indexNo);
        fdIndex = PF_OpenFile(idxfname);
        if (fdIndex < 0) return AME_PF;
    }

    /* allocate arrays */
    keys   = (int *) malloc(sizeof(int) * capacity);
    recids = (int *) malloc(sizeof(int) * capacity);
    if (!keys || !recids) {
        if (keys) free(keys);
        if (recids) free(recids);
        return AME_PF;
    }

    /* 3. Open data file */
    f = fopen(dataFileName, "r");
    if (!f) {
        free(keys);
        free(recids);
        return AME_PF;
    }

    /* 4. Read + collect keys */
    while (fgets(line, sizeof(line), f) != NULL) {
        int key;

        if (parse_roll_from_line(line, &key) != 0)
            continue;

        if (count >= capacity) {
            capacity *= 2;
            keys   = (int *) realloc(keys,   sizeof(int) * capacity);
            recids = (int *) realloc(recids, sizeof(int) * capacity);
            if (!keys || !recids) {
                fclose(f);
                free(keys);
                free(recids);
                return AME_PF;
            }
        }

        keys[count] = key;
        recids[count] = recid;
        recid++;
        count++;
    }
    fclose(f);

    /* 5. create index-order array */
    order = (int *) malloc(sizeof(int) * count);
    if (!order) {
        free(keys);
        free(recids);
        return AME_PF;
    }

    for (i = 0; i < count; i++)
        order[i] = i;

    /* 6. sort order[] using global comparator */
    g_keys = keys;
    qsort(order, count, sizeof(int), cmp_order);

    /* 7. Insert sorted keys */
    PFbufStatsInit();
    gettimeofday(&t1, NULL);

    for (i = 0; i < count; i++) {
        int idx = order[i];
        err = AM_InsertEntry(fdIndex, attrType, attrLength,
                             (char *)&keys[idx], recids[idx]);
        /* ignore error, continue */
    }

    gettimeofday(&t2, NULL);

    /* record stats */
    AMstats.time_ms = timediff_ms(&t2, &t1);
    AMstats.logicalReads   = PF_logicalReads;
    AMstats.logicalWrites  = PF_logicalWrites;
    AMstats.physicalReads  = PF_physicalReads;
    AMstats.physicalWrites = PF_physicalWrites;
    AMstats.pagesAccessed  = PF_physicalReads + PF_physicalWrites;

    free(keys);
    free(recids);
    free(order);

    return AME_OK;
}

int AM_BulkLoadFromFileSorted(char *dataFileName, int dataFd,
                              char attrType, int attrLength,
                              char *indexFileName, int indexNo)
{
    /* Not implemented in this project, just prevent linker error */
    return AME_OK;
}