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
