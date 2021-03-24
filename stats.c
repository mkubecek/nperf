#include <stdio.h>

#include "stats.h"
#include "common.h"

static void xfer_stats_1_print(const struct xfer_stats_1 *stats)
{
	printf("%8" PRIu64 "%8" PRIu64 "%12" PRIu64,
	       stats->msgs, stats->calls, stats->bytes); 
}

void xfer_stats_print_thread(const struct xfer_stats *stats, double elapsed,
			     unsigned int test_mode)
{
	xfer_stats_1_print(&stats->rx);
	fputs("    ", stdout);
	xfer_stats_1_print(&stats->tx);

	switch(test_mode) {
	case MODE_TCP_STREAM:
		printf("%15.1lf B/s", stats->tx.bytes / elapsed);
		break;
	case MODE_TCP_RR:
		printf("%10.1lf T/s", stats->rx.msgs / elapsed);
		break;
	default:
		fprintf(stderr, "unsupported test mode %u\n", test_mode);
		break;
	}
}
