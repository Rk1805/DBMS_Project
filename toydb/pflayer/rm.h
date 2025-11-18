#ifndef RM_H
#define RM_H

#include "pf.h"
#include "pftypes.h"

typedef struct RID {
    int page;
    int slot;
} RID;

typedef struct RM_FileHandle {
    int fd;
    int totalRecords;         /* total inserted */
    int totalDeleted;         /* total deleted (slot offset = -1) */
    int totalPayloadBytes;    /* total payload bytes */
} RM_FileHandle;

typedef struct RM_Record {
    int length;
    char *data;
} RM_Record;

/* Page header stored in each slotted page */
struct RM_PageHdr {
    int freeStart;      /* offset where free space begins (grows upward) */
    int freeEnd;        /* offset where free space ends (grows downward) */
    int numSlots;       /* number of slots */
};

struct RM_Slot {
    short offset;       /* -1 means deleted */
    short length;       /* length of record */
};

/*********** RM Interface *************/
int RM_CreateFile();     /* RM_CreateFile(char *fname) */
int RM_DestroyFile();    /* RM_DestroyFile(char *fname) */
int RM_OpenFile();       /* RM_OpenFile(char *fname, RM_FileHandle *fh) */
int RM_CloseFile();      /* RM_CloseFile(RM_FileHandle *fh) */

int RM_InsertRecord();   /* RM_InsertRecord(fh, record, rid) */
int RM_DeleteRecord();   /* RM_DeleteRecord(fh, rid) */

int RM_GetFirstRecord(); /* RM_GetFirstRecord(fh, rid, record) */
int RM_GetNextRecord();  /* RM_GetNextRecord(fh, rid, record) */

int RM_AnalyzePage(); /* RM_AnalyzePage(fh, pageNum) */

int RM_ComputeFileStats();

#endif
