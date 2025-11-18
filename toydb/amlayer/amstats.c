#include "../pflayer/pftypes.h"
#include "amstats.h"

struct AM_Stats AMstats;

void AM_ResetStats()
{
    PFbufStatsInit();
    AMstats.pagesAccessed = 0;
}

void AM_CaptureStats(double elapsed_ms)
{
    AMstats.time_ms = elapsed_ms;
    AMstats.logicalReads  = PF_logicalReads;
    AMstats.physicalReads = PF_physicalReads;
    AMstats.logicalWrites = PF_logicalWrites;
    AMstats.physicalWrites = PF_physicalWrites;
}
