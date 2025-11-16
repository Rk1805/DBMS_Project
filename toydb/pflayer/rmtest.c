/* rmmgr.c: test and metrics for the RM slotted-page manager */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "rm.h"
#include "pf.h"

#define TEST_FILE "students.rm"
#define NUM_RECORDS 5000

/* Random student record generator (variable length) */
static void make_student_record(char *buf, int len, int recno) {
    /* simple synthetic record: "id:xxx,name:Student_xxx,notes:..." */
    int used = snprintf(buf, len, "id:%d,name:Student_%d,grade:%d,", recno, recno, recno % 100);
    while (used + 20 < len) {
        /* pad with some random text to vary length */
        int add = snprintf(buf + used, len - used, "c%d,", rand() % 1000);
        if (add <= 0) break;
        used += add;
    }
}

/* compute static utilization for fixed record size */
static double static_util(int recSize) {
    int num = PF_PAGE_SIZE / recSize;
    int bytes = num * recSize;
    return 100.0 * (double)bytes / (double)PF_PAGE_SIZE;
}

int main() {
    srand((unsigned)time(NULL));
    PF_Init(50); /* init PF buffer manager with 50 buffers */

    /* create/destroy if exists */
    PF_DestroyFile(TEST_FILE);
    if (RM_CreateFile(TEST_FILE) != PFE_OK) {
        fprintf(stderr, "RM_CreateFile failed\n");
        return 1;
    }

    RM_FileHandle fh;
    if (RM_OpenFile(TEST_FILE, &fh) != PFE_OK) {
        fprintf(stderr, "RM_OpenFile failed\n");
        return 1;
    }

    printf("Inserting %d variable-length student records...\n", NUM_RECORDS);

    /* insert NUM_RECORDS records, lengths vary from 16 to 512 */
    int i;
    int maxLen = 512;
    char *buf = malloc(maxLen);
    RID rid;
    for (i = 0; i < NUM_RECORDS; i++) {
        int len = 16 + (rand() % 497); /* 16..512 */
        make_student_record(buf, len, i);
        Record rec;
        rec.length = (int)strlen(buf) + 1; /* include terminator so we can print safely */
        rec.data = buf;
        if (RM_InsertRecord(&fh, rec, &rid) != PFE_OK) {
            fprintf(stderr, "Insert failed at rec %d\n", i);
            free(buf);
            RM_CloseFile(&fh);
            return 1;
        }
        if ((i+1) % 1000 == 0) printf("  inserted %d\n", i+1);
    }
    free(buf);

    /* Gather stats: iterate pages, compute per-page used bytes and holes */
    printf("\nCollecting page-level statistics...\n");
    int page = -1;
    char *pagebuf;
    int usedTotal = 0;
    int pagesCount = 0;
    int maxUsed = 0;
    int minUsed = PF_PAGE_SIZE;
    int error;
    int pageNum;
    int totalDeletedSlots = 0;
    int totalSlots = 0;

    /* use PF_GetFirstPage / PF_GetNextPage to enumerate used pages */
    page = -1;
    while (1) {
        error = PF_GetNextPage(fh.fd, &page, (char **)&pagebuf);
        if (error == PFE_OK) {
            /* analyze page by reading header/slots */
            int usedBytes = 0, numSlots = 0, numDeleted = 0;
            RM_AnalyzePage(&fh, page, &usedBytes, &numSlots, &numDeleted);
            usedTotal += usedBytes;
            pagesCount++;
            if (usedBytes > maxUsed) maxUsed = usedBytes;
            if (usedBytes < minUsed) minUsed = usedBytes;
            totalDeletedSlots += numDeleted;
            totalSlots += numSlots;
            /* unfix handled inside RM_AnalyzePage */
        } else if (error == PFE_EOF) {
            break;
        } else {
            fprintf(stderr, "Error scanning pages: %d\n", error);
            break;
        }
    }

    double slotted_util = 100.0 * (double)usedTotal / (double)(pagesCount * PF_PAGE_SIZE);
    printf("Pages used: %d\n", pagesCount);
    printf("Total payload bytes (sum of record bytes): %d\n", usedTotal);
    printf("Slotted-page utilization: %.2f%%\n", slotted_util);
    printf("Slot count total: %d, deleted slots total: %d\n", totalSlots, totalDeletedSlots);

    /* Compare with static fixed lengths */
    int static_sizes[] = {32, 64, 128, 256};
    int num_static = sizeof(static_sizes) / sizeof(static_sizes[0]);

    printf("\nComparison table (Static fixed-record vs Slotted variable-length):\n");
    printf("---------------------------------------------------------------\n");
    printf("| Max Static Size | #rec/page | Static Util (%%) | Slotted Util (%%) |\n");
    printf("---------------------------------------------------------------\n");
    int si;
    for (si = 0; si < num_static; si++) {
        int s = static_sizes[si];
        int num = PF_PAGE_SIZE / s;
        double util = static_util(s);
        printf("| %15d | %8d | %15.2f | %16.2f |\n", s, num, util, slotted_util);
    }
    printf("---------------------------------------------------------------\n");

    RM_CloseFile(&fh);

    return 0;
}
