#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rm.h"
#include "pf.h"
#include "pftypes.h"

/*************** INTERNAL CONSTANTS *****************/

#define RM_PAGE_HDR_SIZE   sizeof(struct RM_PageHdr)
#define RM_SLOT_SIZE       sizeof(struct RM_Slot)

/*************** INTERNAL FUNCTIONS *****************/

static int rm_InitSlottedPage(pagebuf)
char *pagebuf;
{
    struct RM_PageHdr *hdr;

    hdr = (struct RM_PageHdr *) pagebuf;
    hdr->freeStart = sizeof(struct RM_PageHdr);
    hdr->freeEnd   = PF_PAGE_SIZE;
    hdr->numSlots  = 0;

    return PFE_OK;
}

static struct RM_PageHdr *rm_GetHdr(pagebuf)
char *pagebuf;
{
    return (struct RM_PageHdr *) pagebuf;
}

static struct RM_Slot *rm_GetSlot(pagebuf, slotno)
char *pagebuf;
int slotno;
{
    struct RM_PageHdr *hdr = rm_GetHdr(pagebuf);
    char *base = pagebuf;
    return (struct RM_Slot *)( base + PF_PAGE_SIZE - (slotno+1)*RM_SLOT_SIZE );
}

/*************** PUBLIC RM FUNCTIONS ****************/

int RM_CreateFile(fname)
char *fname;
{
    return PF_CreateFile(fname);
}

int RM_DestroyFile(fname)
char *fname;
{
    return PF_DestroyFile(fname);
}

int RM_OpenFile(fname, fh)
char *fname;
RM_FileHandle *fh;
{
    int fd;
    fd = PF_OpenFile(fname, PF_REPLACE_LRU);
    if (fd < 0)
        return fd;
    fh->fd = fd;

    /* initialize RM metrics */
    fh->totalRecords = 0;
    fh->totalDeleted = 0;
    fh->totalPayloadBytes = 0;

    return PFE_OK;
}

int RM_CloseFile(fh)
RM_FileHandle *fh;
{
    return PF_CloseFile(fh->fd);
}

/*************** INSERT RECORD ****************/

int RM_InsertRecord(fh, rec, rid)
RM_FileHandle *fh;
RM_Record *rec;
RID *rid;
{
    int fd = fh->fd;
    int page, error;
    char *pagebuf;
    struct RM_PageHdr *hdr;
    struct RM_Slot *slot;

    /* scan existing pages to find free space */
    error = PF_GetFirstPage(fd, &page, &pagebuf);
    while (error != PFE_EOF) {

        hdr = rm_GetHdr(pagebuf);

        /* check free space */
        if (hdr->freeEnd - hdr->freeStart >= rec->length + RM_SLOT_SIZE) {
            /* found suitable page */
            break;
        }

        PF_UnfixPage(fd, page, FALSE);
        error = PF_GetNextPage(fd, &page, &pagebuf);
    }

    if (error == PFE_EOF) {
        /* allocate new page */
        if ((error = PF_AllocPage(fd, &page, &pagebuf)) != PFE_OK)
            return error;
        rm_InitSlottedPage(pagebuf);
    }

    hdr = rm_GetHdr(pagebuf);

    /* allocate new slot */
    slot = rm_GetSlot(pagebuf, hdr->numSlots);
    slot->offset = hdr->freeStart;
    slot->length = rec->length;

    /* copy record */
    memcpy(pagebuf + hdr->freeStart, rec->data, rec->length);

    /* update header */
    hdr->freeStart += rec->length;
    hdr->freeEnd   -= RM_SLOT_SIZE;
    hdr->numSlots++;

    /* return RID */
    rid->page = page;
    rid->slot = hdr->numSlots - 1;

    /* --- Metrics: update AFTER successful insert --- */
    fh->totalRecords++;
    fh->totalPayloadBytes += rec->length;

    PF_UnfixPage(fd, page, TRUE);
    return PFE_OK;
}

/*************** DELETE RECORD ****************/

int RM_DeleteRecord(fh, rid)
RM_FileHandle *fh;
RID *rid;
{
    int fd = fh->fd;
    char *pagebuf;
    struct RM_PageHdr *hdr;
    struct RM_Slot *slot;
    int error;

    if ((error = PF_GetThisPage(fd, rid->page, &pagebuf)) != PFE_OK)
        return error;

    hdr = rm_GetHdr(pagebuf);
    if (rid->slot < 0 || rid->slot >= hdr->numSlots) {
        PF_UnfixPage(fd, rid->page, FALSE);
        PFerrno = PFE_INVALIDPAGE;
        return PFerrno;
    }

    slot = rm_GetSlot(pagebuf, rid->slot);
    if (slot->offset == -1) {
        /* already deleted */
        PF_UnfixPage(fd, rid->page, FALSE);
        PFerrno = PFE_PAGEFREE;
        return PFerrno;
    }

    slot->offset = -1; /* mark deleted */
    fh->totalDeleted++;

    PF_UnfixPage(fd, rid->page, TRUE);
    return PFE_OK;
}


/*************** SCAN FIRST RECORD ****************/

int RM_GetFirstRecord(fh, rid, rec)
RM_FileHandle *fh;
RID *rid;
RM_Record *rec;
{
    int fd = fh->fd;
    int page, s, error;
    char *pagebuf;
    struct RM_PageHdr *hdr;
    struct RM_Slot *slot;

    error = PF_GetFirstPage(fd, &page, &pagebuf);
    while (error != PFE_EOF) {

        hdr = rm_GetHdr(pagebuf);

        for (s = 0; s < hdr->numSlots; s++) {
            slot = rm_GetSlot(pagebuf, s);
            if (slot->offset != -1) {
                rid->page = page;
                rid->slot = s;

                rec->length = slot->length;
                if (rec->length > 0) {
                    rec->data = (char *) malloc(rec->length);
                    if (rec->data == NULL) {
                        PF_UnfixPage(fd, page, FALSE);
                        return PFE_NOMEM;
                    }
                    memcpy(rec->data, pagebuf + slot->offset, rec->length);
                } else {
                    rec->data = NULL;
                }
                PF_UnfixPage(fd, page, FALSE);
                return PFE_OK;
            }
        }

        PF_UnfixPage(fd, page, FALSE);
        error = PF_GetNextPage(fd, &page, &pagebuf);
    }

    return PFE_EOF;
}

/*************** SCAN NEXT RECORD ****************/

int RM_GetNextRecord(fh, rid, rec)
RM_FileHandle *fh;
RID *rid;
RM_Record *rec;
{
    int fd = fh->fd;
    int page = rid->page;
    int s = rid->slot + 1;
    char *pagebuf;
    struct RM_PageHdr *hdr;
    struct RM_Slot *slot;
    int error;

    /* first try same page */
    if ((error = PF_GetThisPage(fd, page, &pagebuf)) != PFE_OK)
        return error;

    hdr = rm_GetHdr(pagebuf);

    for ( ; s < hdr->numSlots; s++) {
        slot = rm_GetSlot(pagebuf, s);
        if (slot->offset != -1) {
            rid->slot = s;
            rec->length = slot->length;
            if (rec->length > 0) {
                rec->data = (char *) malloc(rec->length);
                if (rec->data == NULL) {
                    PF_UnfixPage(fd, page, FALSE);
                    return PFE_NOMEM;
                }
                memcpy(rec->data, pagebuf + slot->offset, rec->length);
            } else {
                rec->data = NULL;
            }
            PF_UnfixPage(fd, page, FALSE);
            return PFE_OK;
        }
    }

    PF_UnfixPage(fd, page, FALSE);

    /* move to next pages */
    error = PF_GetNextPage(fd, &page, &pagebuf);
    while (error != PFE_EOF) {

        hdr = rm_GetHdr(pagebuf);

        for (s = 0; s < hdr->numSlots; s++) {
            slot = rm_GetSlot(pagebuf, s);
            if (slot->offset != -1) {

                rid->page = page;
                rid->slot = s;

                rec->length = slot->length;
                if (rec->length > 0) {
                    rec->data = (char *) malloc(rec->length);
                    if (rec->data == NULL) {
                        PF_UnfixPage(fd, page, FALSE);
                        return PFE_NOMEM;
                    }
                    memcpy(rec->data, pagebuf + slot->offset, rec->length);
                } else {
                    rec->data = NULL;
                }
                PF_UnfixPage(fd, page, FALSE);
                return PFE_OK;
            }
        }

        PF_UnfixPage(fd, page, FALSE);
        error = PF_GetNextPage(fd, &page, &pagebuf);
    }

    return PFE_EOF;
}


int RM_AnalyzePage(fh, pageNum, usedBytes, numSlots, numDeleted)
RM_FileHandle *fh;
int pageNum;
int *usedBytes;
int *numSlots;
int *numDeleted;
{
    char *pagebuf;
    struct RM_PageHdr *hdr;
    struct RM_Slot *slot;
    int error, s;
    int payload = 0;
    int deleted = 0;

    /* Try to fetch the page. PF_GetThisPage may return PFE_OK or
       PFE_PAGEFIXED (page already fixed). Treat both as success. */
    error = PF_GetThisPage(fh->fd, pageNum, &pagebuf);
    if (error != PFE_OK && error != PFE_PAGEFIXED) {
        /* real error */
        return error;
    }

    hdr = (struct RM_PageHdr *) pagebuf;

    *numSlots = hdr->numSlots;
    *numDeleted = 0;

    for (s = 0; s < hdr->numSlots; s++) {
        slot = rm_GetSlot(pagebuf, s);
        if (slot->offset == -1)
            (*numDeleted)++;
        else
            payload += slot->length;
    }

    *usedBytes = payload;

    /* Unfix the page now that we're done with it.
       PF_UnfixPage should be called regardless of whether
       PF_GetThisPage returned PFE_OK or PFE_PAGEFIXED. */
    PF_UnfixPage(fh->fd, pageNum, FALSE);
    return PFE_OK;
}


int RM_ComputeFileStats(fh, totalPages, totalPayload, slottedUtil, totalSlots, deletedSlots)
RM_FileHandle *fh;
int *totalPages;
int *totalPayload;
double *slottedUtil;
int *totalSlots;
int *deletedSlots;
{
    int error, page = -1;
    char *pagebuf;
    int used, slots, deleted;

    *totalPages = 0;
    *totalPayload = 0;
    *totalSlots = 0;
    *deletedSlots = 0;

    while (1) {
        error = PF_GetNextPage(fh->fd, &page, &pagebuf);
        if (error == PFE_EOF)
            break;
        if (error != PFE_OK)
            return error;

        RM_AnalyzePage(fh, page, &used, &slots, &deleted);

        *totalPages += 1;
        *totalPayload += used;
        *totalSlots += slots;
        *deletedSlots += deleted;
    }

    *slottedUtil = 100.0 * ((double)*totalPayload) / (*totalPages * PF_PAGE_SIZE);
    return PFE_OK;
}
