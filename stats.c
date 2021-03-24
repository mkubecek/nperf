#include <stdio.h>

#include "stats.h"

static void xfer_stats_1_print(const struct xfer_stats_1 *stats)
{
	printf("%8" PRIu64 "%8" PRIu64 "%12" PRIu64,
	       stats->msgs, stats->calls, stats->bytes); 
}

void xfer_stats_print(const struct xfer_stats *stats)
{
	xfer_stats_1_print(&stats->rx);
	xfer_stats_1_print(&stats->tx);
}
