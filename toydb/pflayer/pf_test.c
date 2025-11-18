#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pf.h"
#include "pftypes.h"

#define OPS 1000   /* number of operations per mixture */
#define WORK_SMALL 3   /* small working set */
#define WORK_MED   6   /* medium working set */
#define WORK_LARGE 12  /* larger than buffer -> thrash */

void run_mixture(char *label, int strategy, int working_set)
{
    int fd;
    char *buf;
    int i, p;
    int op;

    printf("\n=== %s | Strategy=%s | Working-set=%d ===\n",
           label,
           (strategy == PF_REPLACE_LRU ? "LRU" : "MRU"),
           working_set);

    PFbufStatsInit();

    fd = PF_OpenFile("testfile", strategy);
    if (fd < 0) {
        PF_PrintError("Open failed");
        return;
    }

    /* Ensure file has enough pages */
    for (i = 0; i < working_set; i++) {
        int pg;
        PF_AllocPage(fd, &pg, &buf);
        memset(buf, 0, PF_PAGE_SIZE);
        PF_UnfixPage(fd, pg, TRUE);
    }

    /* Perform random READ/WRITE pattern on a controlled working-set */
    for (op = 0; op < OPS; op++) {

        p = rand() % working_set; /* page chosen from working set */

        if (rand() % 100 < 70) {
            /* 70% reads */
            PF_GetThisPage(fd, p, &buf);
            PF_UnfixPage(fd, p, FALSE);
        } else {
            /* 30% writes */
            PF_GetThisPage(fd, p, &buf);
            buf[0] = (char)op;
            PF_UnfixPage(fd, p, TRUE);
        }
    }

    PF_CloseFile(fd);

    PFbufStatsPrint();
}

int main()
{
    srand(7);

    /* Very small buffer to force replacement */
    PFbufInit(3);

    PF_CreateFile("testfile");

    /*
     * 3 Working-set sizes:
     *   SMALL  (fits in buffer → high hit rate)
     *   MEDIUM (slightly bigger → LRU better than MRU)
     *   LARGE  (much bigger -> thrashing)
     */

    /* ===== SMALL WORKING SET ===== */
    run_mixture("SMALL WORKING SET", PF_REPLACE_LRU, WORK_SMALL);
    run_mixture("SMALL WORKING SET", PF_REPLACE_MRU, WORK_SMALL);

    /* ===== MEDIUM WORKING SET ===== */
    run_mixture("MEDIUM WORKING SET", PF_REPLACE_LRU, WORK_MED);
    run_mixture("MEDIUM WORKING SET", PF_REPLACE_MRU, WORK_MED);

    /* ===== LARGE WORKING SET (thrashing) ===== */
    run_mixture("LARGE WORKING SET", PF_REPLACE_LRU, WORK_LARGE);
    run_mixture("LARGE WORKING SET", PF_REPLACE_MRU, WORK_LARGE);

    return 0;
}
