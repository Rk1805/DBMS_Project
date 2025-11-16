/* ambuild.c
 * Index construction for AM layer - K&R C
 *
 * Supports:
 *   1. Incremental build (AM_InsertEntry per record)
 *   2. Build index from existing file via sorting + bulk load
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "am.h"
#include "pf.h"
#include "../pflayer/rm.h"

/*******************  
 * GLOBALS FOR QSORT
 *******************/
char global_attrType;
int  global_attrLength;

/*******************************
 * PAIR STRUCT FOR KEY SORTING
 *******************************/
struct sortpair {
    char *k;   /* key bytes */
    int   r;   /* recid as int */
};

/*********************************
 * QSORT COMPARATOR (K&R STYLE)
 *********************************/
int cmpfunc(a, b)
const void *a;
const void *b;
{
    const struct sortpair *pa = (const struct sortpair *) a;
    const struct sortpair *pb = (const struct sortpair *) b;

    return AM_Compare(pa->k, global_attrType, global_attrLength, pb->k);
}

/**********************************************
 * METHOD 2 — INCREMENTAL INDEX CONSTRUCTION
 **********************************************/
int AM_BuildIndexIncremental(dataFileName, dataFd, attrType, attrLength, indexFileName, indexNo)
char *dataFileName;
int dataFd;
char attrType;
int attrLength;
char *indexFileName;
int indexNo;
{
    char idxname[AM_MAX_FNAME_LENGTH];
    RM_FileHandle fh;
    RM_Record rec;
    RID rid;
    int err;

    /* Create physical index file */
    sprintf(idxname, "%s.%d", indexFileName, indexNo);
    if (PF_CreateFile(idxname) != PFE_OK) {
        AM_Errno = AME_PF;
        return AME_PF;
    }

    /* Create logical B+ tree root */
    if (AM_CreateIndex(indexFileName, indexNo, attrType, attrLength) != AME_OK) {
        return AM_Errno;
    }

    /* Open RM file */
    fh.fd = dataFd;

    /* Scan RM records */
    err = RM_GetFirstRecord(&fh, &rid, &rec);
    while (err != PFE_EOF) {

        /* insert into index: key = rec.data (first attribute) */
        AM_InsertEntry(
            AM_RootPageNum,                      /* root page num */
            attrType, attrLength,                /* attribute */
            rec.data,                            /* key bytes */
            RecIdToInt(rid)                      /* recid */
        );

        err = RM_GetNextRecord(&fh, &rid, &rec);
    }

    return AME_OK;
}

/**********************************************
 * METHOD 1 — ONE-SHOT BUILD USING SORT + BULK
 **********************************************/
int AM_BuildIndexFromExistingFile(dataFileName, dataFd, attrType, attrLength, indexFileName, indexNo)
char *dataFileName;
int dataFd;
char attrType;
int attrLength;
char *indexFileName;
int indexNo;
{
    RM_FileHandle fh;
    RM_Record rec;
    RID rid;
    int err;

    int capacity = 1024;
    int count = 0;

    char **keys;
    int *recids;
    struct sortpair *arr;

    keys = (char **) malloc(sizeof(char *) * capacity);
    recids = (int *) malloc(sizeof(int) * capacity);

    fh.fd = dataFd;

    /* Scan all RM records */
    err = RM_GetFirstRecord(&fh, &rid, &rec);
    while (err != PFE_EOF) {

        if (count >= capacity) {
            capacity *= 2;
            keys = (char **) realloc(keys, sizeof(char *) * capacity);
            recids = (int *) realloc(recids, sizeof(int) * capacity);
        }

        keys[count] = (char *) malloc(attrLength);
        bcopy(rec.data, keys[count], attrLength);

        recids[count] = RecIdToInt(rid);
        count++;

        err = RM_GetNextRecord(&fh, &rid, &rec);
    }

    /* Build array for sorting */
    arr = (struct sortpair *) malloc(sizeof(struct sortpair) * count);

    {
        int i;
        for (i = 0; i < count; i++) {
            arr[i].k = keys[i];
            arr[i].r = recids[i];
        }
    }

    /* Set global comparator attributes */
    global_attrType = attrType;
    global_attrLength = attrLength;

    /* Sort key+recid pairs */
    qsort(arr, count, sizeof(struct sortpair), cmpfunc);

    /* Prepare arrays for bulk loader */
    {
        char **sortedKeys;
        int *sortedRecids;
        int i;

        sortedKeys = (char **) malloc(sizeof(char *) * count);
        sortedRecids = (int *) malloc(sizeof(int) * count);

        for (i = 0; i < count; i++) {
            sortedKeys[i] = arr[i].k;
            sortedRecids[i] = arr[i].r;
        }

        /* Call bulk loader */
        AM_BulkLoadFromSortedPairs(
            indexFileName,
            indexNo,
            attrType,
            attrLength,
            sortedKeys,
            sortedRecids,
            count
        );

        free(sortedKeys);
        free(sortedRecids);
    }

    free(arr);
    free(keys);     /* keys[i] already used */
    free(recids);

    return AME_OK;
}
