/* amstats.h */
#ifndef AMSTATS_H
#define AMSTATS_H

struct AM_Stats {
    double time_ms;
    int logicalReads;
    int physicalReads;
    int logicalWrites;
    int physicalWrites;
    int pagesAccessed;
};

extern struct AM_Stats AMstats;

void AM_ResetStats();
void AM_CaptureStats(double elapsed_ms);

#endif
