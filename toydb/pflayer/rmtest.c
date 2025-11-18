/* rmtest.c: test and metrics for the RM slotted-page manager */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "rm.h"
#include "pf.h"

#define TEST_FILE "students.rm"
#define NUM_RECORDS 5000

/* create synthetic student record */
static void make_student_record(buf, len, recno)
char *buf;
int len;
int recno;
{
    int used;
    used = sprintf(buf, "id:%d,name:Student_%d,grade:%d,", recno, recno, recno % 100);
    while (used + 20 < len) {
        used += sprintf(buf + used, "c%d,", rand() % 1000);
    }
}

static double static_util(recSize)
int recSize;
{
    int num = PF_PAGE_SIZE / recSize;
    int bytes = num * recSize;
    /* percent of page used by payload when packing fixed slots */
    return 100.0 * (double)bytes / (double)PF_PAGE_SIZE;
}

int main()
{
    RM_FileHandle fh;
    int i, len;
    int error;
    RID rid;
    int page;
    char *buf;
    char *pagebuf;

    int usedTotal = 0;
    int pagesCount = 0;
    int numSlots = 0;
    int deletedSlots = 0;
    int usedBytes;

    srand((unsigned)time(NULL));

    PF_Init(50);   /* initialize PF data structures */
    PFbufInit(50); /* allocate buffer pool */

    /* recreate file */
    PF_DestroyFile(TEST_FILE);
    RM_CreateFile(TEST_FILE);

    if ((error = RM_OpenFile(TEST_FILE, &fh)) != PFE_OK) {
        printf("RM_OpenFile failed: %d\n", error);
        return 1;
    }

    printf("Inserting %d records...\n", NUM_RECORDS);

    buf = (char *)malloc(600);
    if (buf == NULL) { printf("malloc failed\n"); return 1; }

    for (i = 0; i < NUM_RECORDS; i++) {

        len = 16 + (rand() % 497); /* 16..512 */
        make_student_record(buf, len, i);

        {
            RM_Record rec;
            rec.length = strlen(buf) + 1;
            rec.data   = buf;

            error = RM_InsertRecord(&fh, &rec, &rid);
            if (error != PFE_OK) {
                printf("Insert failed at record %d: err=%d\n", i, error);
                free(buf);
                RM_CloseFile(&fh);
                return 1;
            }
        }

        if ((i+1) % 1000 == 0)
            printf("Inserted %d\n", i+1);
    }

    free(buf);

    /* Scan all pages and compute stats */
    printf("\nComputing slotted-page statistics...\n");

    page = -1;

    while (1) {

        error = PF_GetNextPage(fh.fd, &page, &pagebuf);

        if (error == PFE_EOF)
            break;
        if (error != PFE_OK) {
            printf("Error scanning pages: %d\n", error);
            break;
        }

        {
            int slots = 0;
            int deleted = 0;

            error = RM_AnalyzePage(&fh, page, &usedBytes, &slots, &deleted);
            if (error != PFE_OK) {
                printf("RM_AnalyzePage error %d on page %d\n", error, page);
                PF_UnfixPage(fh.fd, page, FALSE);
                break;
            }
            usedTotal += usedBytes;
            pagesCount++;
            numSlots += slots;
            deletedSlots += deleted;
        }

        PF_UnfixPage(fh.fd, page, FALSE);
    }

    if (pagesCount == 0) {
        printf("No pages found!\n");
        RM_CloseFile(&fh);
        return 1;
    }

    {
        double util = 100.0 * (double)usedTotal / (double)(pagesCount * PF_PAGE_SIZE);

        printf("Pages used: %d\n", pagesCount);
        printf("Total payload bytes: %d\n", usedTotal);
        printf("Total slots: %d\n", numSlots);
        printf("Total deleted slots: %d\n", deletedSlots);
        printf("Slotted-page utilization: %.2f%%\n", util);

        printf("\nStatic table:\n");
        printf("----------------------------------------------\n");
        printf("| Static Size | rec/page | Static Util | Slotted Util |\n");
        printf("----------------------------------------------\n");

        {
            int sizes[4] = {32, 64, 128, 256};
            int k;
            for (k = 0; k < 4; k++) {
                int s = sizes[k];
                int num = PF_PAGE_SIZE / s;
                double su = static_util(s);
                printf("| %10d | %8d | %10.2f | %12.2f |\n",
                       s, num, su, util);
            }
        }

        printf("----------------------------------------------\n");
    }

    RM_CloseFile(&fh);

    return 0;
}
