/* ambulk.c
 *
 * Bulk-loading B+ tree pages from a sorted list of keys.
 * K&R-style C to match existing project.
 *
 * Exports:
 *   AM_BulkLoadFromSortedPairs(...)
 *
 * Notes:
 *   - The function creates an index file called "<fileName>.<indexNo>"
 *   - It reserves page 0 as the root placeholder (so PF_GetFirstPage
 *     returns the root page as required by the AM layer).
 *   - It creates leaf pages, fills keys and recid lists, then builds
 *     internal levels until a single root exists; the final root is
 *     written to reserved page 0.
 *
 * Assumptions:
 *   - keys[]: array of pointers to key bytes (each of length attrLength)
 *   - recIds[]: parallel array of ints (AM uses AM_si to store recid)
 *   - keys are already sorted according to AM_Compare semantics
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "am.h"
#include "pf.h"

#define MAX_CHILDREN_PER_NODE 1024  /* conservative */

static void write_leaf_page(
    char *pageBuf, char *attrVal, int attrLength,
    int recId, short *recListPtr, AM_LEAFHEADER *hdr)
{
    int recSize;
    short tempPtr;
    short nullptr = 0;

    bcopy(hdr, pageBuf, AM_sl);

    /* compute recId pointer for new rec (grow from page end) */
    hdr->recIdPtr -= AM_si + AM_ss;
    tempPtr = hdr->recIdPtr;

    /* add recId */
    bcopy((char *)&recId, pageBuf + tempPtr, AM_si);

    /* set next pointer = whatever head was for this key */
    bcopy((char *)&nullptr, pageBuf + tempPtr + AM_si, AM_ss);

    /* store key at key slot (caller handled shifting / placement) */
    /* ... the caller will fill key bytes at the correct offset. */
}

/* helper: fill an AM leaf header for a fresh leaf page */
static void init_leaf_header(AM_LEAFHEADER *h, int attrLength)
{
    h->pageType = 'l';
    h->nextLeafPage = AM_NULL_PAGE;
    h->recIdPtr = PF_PAGE_SIZE;
    h->keyPtr = AM_sl;
    h->freeListPtr = AM_NULL;
    h->numinfreeList = 0;
    h->attrLength = attrLength;
    h->numKeys = 0;

    /* compute max keys for internal nodes usage via AM_CreateIndex logic */
    {
        int maxKeys = (PF_PAGE_SIZE - AM_sint - AM_si)/(AM_si + attrLength);
        if ((maxKeys % 2) != 0) h->maxKeys = maxKeys - 1;
        else h->maxKeys = maxKeys;
    }
}

/* top-level bulk loader
 * keys: array of pointers to key bytes; each key length = attrLength
 * recIds: array of ints (recids); nKeys = number of keys
 *
 * Returns AME_OK on success, AME_PF or other AME_* on failure.
 */
int AM_BulkLoadFromSortedPairs(fileName, indexNo, attrType, attrLength, keys, recIds, nKeys)
char *fileName;
int indexNo;
char attrType;
int attrLength;
char **keys;
int *recIds;
int nKeys;
{
    char indexfName[AM_MAX_FNAME_LENGTH];
    int errVal;
    int fileDesc;
    int i, j, k;

    /* Parameter checks */
    if ((attrType != 'c') && (attrType != 'f') && (attrType != 'i')) {
        AM_Errno = AME_INVALIDATTRTYPE;
        return AME_INVALIDATTRTYPE;
    }
    if ((attrLength < 1) || (attrLength > 255)) {
        AM_Errno = AME_INVALIDATTRLENGTH;
        return AME_INVALIDATTRLENGTH;
    }

    /* Build index file name */
    sprintf(indexfName, "%s.%d", fileName, indexNo);

    /* Create PF file and open it */
    errVal = PF_CreateFile(indexfName);
    if (errVal != PFE_OK) { AM_Errno = AME_PF; return AME_PF; }

    fileDesc = PF_OpenFile(indexfName);
    if (fileDesc < 0) { AM_Errno = AME_PF; return AME_PF; }

    /* Reserve page 0 as root placeholder so PF_GetFirstPage returns the root page */
    {
        int rootPageNum;
        char *rootBuf;
        errVal = PF_AllocPage(fileDesc, &rootPageNum, &rootBuf);
        if (errVal != PFE_OK) { PF_CloseFile(fileDesc); AM_Errno = AME_PF; return AME_PF; }
        /* Initialize with an empty leaf header as placeholder */
        {
            AM_LEAFHEADER tempH;
            init_leaf_header(&tempH, attrLength);
            bcopy(&tempH, rootBuf, AM_sl);
        }
        errVal = PF_UnfixPage(fileDesc, rootPageNum, TRUE);
        if (errVal != PFE_OK) { PF_CloseFile(fileDesc); AM_Errno = AME_PF; return AME_PF; }
    }

    /* ---------------------------------------------------------------------
       Create leaf pages sequentially, filling them greedily until full.
       We'll store the page numbers and for each leaf the first key to later
       build internal nodes.
       --------------------------------------------------------------------- */

    /* arrays of leaf page nums and their first-key buffers */
    {
        int maxLeaves = (nKeys / 1) + 10; /* rough upper bound */
        int *leafPageNums = (int *) malloc(sizeof(int) * maxLeaves);
        char **leafFirstKeys = (char **) malloc(sizeof(char *) * maxLeaves);
        int leafCount = 0;

        /* allocate first leaf */
        {
            int pnum;
            char *pbuf;
            errVal = PF_AllocPage(fileDesc, &pnum, &pbuf);
            if (errVal != PFE_OK) { PF_CloseFile(fileDesc); AM_Errno = AME_PF; return AME_PF; }
            /* init header */
            {
                AM_LEAFHEADER tmp;
                init_leaf_header(&tmp, attrLength);
                bcopy(&tmp, pbuf, AM_sl);
            }
            /* record */
            leafPageNums[leafCount] = pnum;
            leafFirstKeys[leafCount] = malloc(attrLength);
            /* set first key later when first record is written */
            leafCount++;
            /* unfix after init - will get and write below */
            errVal = PF_UnfixPage(fileDesc, pnum, TRUE);
            if (errVal != PFE_OK) { PF_CloseFile(fileDesc); AM_Errno = AME_PF; return AME_PF; }
        }

        /* Now append keys one by one */
        {
            int curLeaf = 0;
            for (i = 0; i < nKeys; i++) {
                int pnum = leafPageNums[curLeaf];
                char *pbuf;
                AM_LEAFHEADER hdr;
                int recSize = attrLength + AM_ss;
                int indexWhereKeyPlaced;

                /* get the current leaf page */
                errVal = PF_GetThisPage(fileDesc, pnum, &pbuf);
                if (errVal != PFE_OK) { PF_CloseFile(fileDesc); AM_Errno = AME_PF; return AME_PF; }

                bcopy(pbuf, &hdr, AM_sl);

                /* if this is the very first key for the leaf set the first-key buffer */
                if (hdr.numKeys == 0) {
                    bcopy(keys[i], leafFirstKeys[curLeaf], attrLength);
                }

                /* compute if there is room to add another key and its recid pointer and recid storage */
                if ((hdr.recIdPtr - hdr.keyPtr) >= (recSize + AM_si + AM_ss)) {
                    /* place key at keyPtr */
                    indexWhereKeyPlaced = hdr.numKeys + 1;
                    bcopy(keys[i], pbuf + AM_sl + (indexWhereKeyPlaced - 1) * recSize, attrLength);
                    /* place recId list head (initially 0) */
                    {
                        short zero = 0;
                        bcopy((char *)&zero, pbuf + AM_sl + (indexWhereKeyPlaced - 1) * recSize + attrLength, AM_ss);
                    }
                    /* allocate recid node at recIdPtr - AM_si - AM_ss */
                    hdr.recIdPtr -= AM_si + AM_ss;
                    bcopy((char *)&recIds[i], pbuf + hdr.recIdPtr, AM_si);
                    /* next pointer after recid = 0 */
                    {
                        short zero = 0;
                        bcopy((char *)&zero, pbuf + hdr.recIdPtr + AM_si, AM_ss);
                    }
                    /* set head pointer in key slot to this recid offset */
                    bcopy((char *)&hdr.recIdPtr, pbuf + AM_sl + (indexWhereKeyPlaced - 1) * recSize + attrLength, AM_ss);

                    hdr.numKeys++;
                    hdr.keyPtr += recSize;

                    /* write header back */
                    bcopy(&hdr, pbuf, AM_sl);

                    /* done with this page */
                    errVal = PF_UnfixPage(fileDesc, pnum, TRUE);
                    if (errVal != PFE_OK) { PF_CloseFile(fileDesc); AM_Errno = AME_PF; return AME_PF; }
                } else {
                    /* no room: allocate new leaf page and append there */
                    errVal = PF_UnfixPage(fileDesc, pnum, TRUE);
                    if (errVal != PFE_OK) { PF_CloseFile(fileDesc); AM_Errno = AME_PF; return AME_PF; }

                    /* allocate a new leaf */
                    {
                        int newp;
                        char *newbuf;
                        errVal = PF_AllocPage(fileDesc, &newp, &newbuf);
                        if (errVal != PFE_OK) { PF_CloseFile(fileDesc); AM_Errno = AME_PF; return AME_PF; }
                        AM_LEAFHEADER tmp;
                        init_leaf_header(&tmp, attrLength);
                        bcopy(&tmp, newbuf, AM_sl);
                        errVal = PF_UnfixPage(fileDesc, newp, TRUE);
                        if (errVal != PFE_OK) { PF_CloseFile(fileDesc); AM_Errno = AME_PF; return AME_PF; }

                        /* link previous leaf nextLeafPage to newp */
                        {
                            char *oldbuf;
                            int oldp = leafPageNums[curLeaf];
                            errVal = PF_GetThisPage(fileDesc, oldp, &oldbuf);
                            if (errVal != PFE_OK) { PF_CloseFile(fileDesc); AM_Errno = AME_PF; return AME_PF; }
                            AM_LEAFHEADER oldhdr;
                            bcopy(oldbuf, &oldhdr, AM_sl);
                            oldhdr.nextLeafPage = newp;
                            bcopy(&oldhdr, oldbuf, AM_sl);
                            errVal = PF_UnfixPage(fileDesc, oldp, TRUE);
                            if (errVal != PFE_OK) { PF_CloseFile(fileDesc); AM_Errno = AME_PF; return AME_PF; }
                        }

                        leafPageNums[++curLeaf] = newp;
                        leafFirstKeys[curLeaf] = malloc(attrLength);
                        /* write the i-th key into this new leaf in next iteration
                           (decrement i so outer loop retries this key on the new leaf) */
                        i--; /* reprocess same key with new leaf */
                    }
                }
            } /* for each key */
            leafCount = curLeaf + 1;
        }

        /* At this point we have leafCount leaves with keys filled.
           Now build interior levels by grouping children and writing internal nodes.
           We'll create arrays of (pageNum, firstKey) to represent children of current level,
           and iteratively build parents until one remains. */

        /* child arrays start as leaves */
        {
            int levelChildCount = leafCount;
            int *childPageNums = (int *) malloc(sizeof(int) * levelChildCount);
            char **childFirstKeys = (char **) malloc(sizeof(char *) * levelChildCount);
            int curLevel = 0;
            for (i = 0; i < levelChildCount; i++) {
                childPageNums[i] = leafPageNums[i];
                childFirstKeys[i] = leafFirstKeys[i];
            }

            /* iteratively build parents until only one child (root) remains */
            while (levelChildCount > 1) {
                int parentCountEstimate = (levelChildCount / (PF_PAGE_SIZE / (attrLength + AM_si))) + 10;
                int *parentPageNums = (int *) malloc(sizeof(int) * parentCountEstimate);
                char **parentFirstKeys = (char **) malloc(sizeof(char *) * parentCountEstimate);
                int parentCount = 0;

                /* pack children into internal nodes greedily */
                i = 0;
                while (i < levelChildCount) {
                    /* allocate internal node page */
                    int ipnum;
                    char *ibuf;
                    errVal = PF_AllocPage(fileDesc, &ipnum, &ibuf);
                    if (errVal != PFE_OK) { PF_CloseFile(fileDesc); AM_Errno = AME_PF; return AME_PF; }

                    /* prepare header for internal node */
                    {
                        AM_INTHEADER ihead;
                        ihead.pageType = 'i';
                        ihead.numKeys = 0; /* set later */
                        ihead.attrLength = attrLength;
                        ihead.maxKeys = (PF_PAGE_SIZE - AM_sint - AM_si)/(AM_si + attrLength);
                        bcopy(&ihead, ibuf, AM_sint);
                    }

                    /* fill child pointers and keys into page */
                    {
                        AM_INTHEADER ih;
                        bcopy(ibuf, &ih, AM_sint);
                        int recSize = ih.attrLength + AM_si;
                        int offset = AM_sint;
                        int childIdx = 0;
                        /* put first child pointer (page number) */
                        bcopy((char *)&childPageNums[i], ibuf + AM_sint + 0, AM_si);
                        childIdx++;

                        /* now add as many child keys as can fit */
                        while ((i < levelChildCount) && ((AM_sint + (ih.numKeys + 1) * recSize + AM_si) <= PF_PAGE_SIZE)) {
                            if (ih.numKeys == 0) {
                                /* the first key is the key of child i+1 */
                                if ((i+1) < levelChildCount) {
                                    bcopy(childFirstKeys[i+1],
                                          ibuf + AM_sint + AM_si + ih.numKeys * recSize,
                                          attrLength);
                                    /* write the child pointer following the key */
                                    bcopy((char *)&childPageNums[i+1],
                                          ibuf + AM_sint + AM_si + ih.numKeys * recSize + attrLength,
                                          AM_si);
                                    ih.numKeys++;
                                    i++;
                                } else {
                                    /* only one child left, break */
                                    break;
                                }
                            } else {
                                /* subsequent keys: place childFirstKeys[i+1] if room */
                                if ((i+1) < levelChildCount) {
                                    bcopy(childFirstKeys[i+1],
                                          ibuf + AM_sint + AM_si + ih.numKeys * recSize,
                                          attrLength);
                                    bcopy((char *)&childPageNums[i+1],
                                          ibuf + AM_sint + AM_si + ih.numKeys * recSize + attrLength,
                                          AM_si);
                                    ih.numKeys++;
                                    i++;
                                } else break;
                            }
                        } /* while fit */
                        /* write header back */
                        bcopy(&ih, ibuf, AM_sint);
                    }

                    /* done with this internal page */
                    errVal = PF_UnfixPage(fileDesc, ipnum, TRUE);
                    if (errVal != PFE_OK) { PF_CloseFile(fileDesc); AM_Errno = AME_PF; return AME_PF; }

                    /* add to parent array */
                    parentPageNums[parentCount] = ipnum;
                    parentFirstKeys[parentCount] = (char *) malloc(attrLength);
                    /* copy the first key for this internal node (which is the first key of its first child) */
                    /* get that from childFirstKeys */
                    bcopy(childFirstKeys[(parentCount==0?0:0)], parentFirstKeys[parentCount], attrLength);
                    parentCount++;
                } /* while i < levelChildCount */

                /* move up one level */
                /* free previous child arrays (but not the key buffers that we still need) */
                free(childPageNums);
                free(childFirstKeys);

                /* new arrays */
                childPageNums = parentPageNums;
                childFirstKeys = parentFirstKeys;
                levelChildCount = parentCount;
            } /* while levelChildCount > 1 */

            /* At this point childPageNums[0] is the root page number (some page possibly >0).
               We MUST ensure the root is at page 0 reserved previously (so PF_GetFirstPage works).
               We'll copy the built root page content into page 0. */

            /* read root built page */
            {
                int builtRootPage = childPageNums[0];
                char *builtBuf;
                char *rootBuf;
                int reservedRoot = 0; /* reserved earlier as page 0 */

                /* Get both pages */
                errVal = PF_GetThisPage(fileDesc, builtRootPage, &builtBuf);
                if (errVal != PFE_OK) { PF_CloseFile(fileDesc); AM_Errno = AME_PF; return AME_PF; }
                errVal = PF_GetThisPage(fileDesc, reservedRoot, &rootBuf);
                if (errVal != PFE_OK) { PF_UnfixPage(fileDesc, builtRootPage, FALSE); PF_CloseFile(fileDesc); AM_Errno = AME_PF; return AME_PF; }

                /* copy content */
                bcopy(builtBuf, rootBuf, PF_PAGE_SIZE);

                /* mark root dirty and unfix both pages */
                errVal = PF_UnfixPage(fileDesc, reservedRoot, TRUE);
                if (errVal != PFE_OK) { PF_UnfixPage(fileDesc, builtRootPage, FALSE); PF_CloseFile(fileDesc); AM_Errno = AME_PF; return AME_PF; }
                errVal = PF_UnfixPage(fileDesc, builtRootPage, FALSE);
                if (errVal != PFE_OK) { PF_CloseFile(fileDesc); AM_Errno = AME_PF; return AME_PF; }

                /* done */
            }

            /* free child arrays containers (not their keys) */
            free(childPageNums);
            free(childFirstKeys);
        }

        /* free leaf arrays */
        for (i = 0; i < leafCount; i++) free(leafFirstKeys[i]);
        free(leafFirstKeys);
        free(leafPageNums);
    }

    /* Close the index file */
    PF_CloseFile(fileDesc);
    AM_Errno = AME_OK;
    return AME_OK;
}
