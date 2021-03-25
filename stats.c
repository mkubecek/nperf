#include <stdio.h>

#include "stats.h"
#include "common.h"

static void xfer_stats_1_print(const struct xfer_stats_1 *stats)
{
	printf("%9" PRIu64 " %9" PRIu64 " %13" PRIu64,
	       stats->calls, stats->msgs, stats->bytes);
}

void xfer_stats_raw_header(const char *label)
{
	printf("%-8s %9s %9s %13s    %9s %9s %13s\n",
	       label, "recv", "msg", "bytes", "send", "msg", "bytes");
}

void xfer_stats_print_raw(const struct xfer_stats *stats, unsigned int id)
{
	if (id == XFER_STATS_TOTAL)
		fputs("total    ", stdout);
	else
		printf("%-8u ", id);

	xfer_stats_1_print(&stats->rx);
	fputs("    ", stdout);
	xfer_stats_1_print(&stats->tx);
	putchar('\n');
}
