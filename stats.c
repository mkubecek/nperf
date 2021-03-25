#include <stdio.h>

#include "stats.h"
#include "common.h"

static void xfer_stats_1_print(const struct xfer_stats_1 *stats)
{
	printf("%9" PRIu64 " %9" PRIu64 " %13" PRIu64,
	       stats->calls, stats->msgs, stats->bytes);
}

void xfer_stats_thread_header(const char *label, unsigned int test_mode)
{
	unsigned int result_width;
	const char *result_label;

	switch(test_mode) {
	case MODE_TCP_STREAM:
		result_label = "B/s";
		result_width = 15;
		break;
	case MODE_TCP_RR:
		result_label = "trans/s";
		result_width = 10;
		break;
	default:
		return;
	}

	printf("%-8s %9s %9s %13s    %9s %9s %13s  %*s\n",
	       label, "recv", "msg", "bytes", "send", "msg", "bytes",
	       result_width, result_label);
}

void xfer_stats_print_thread(const struct xfer_stats *stats, double elapsed,
			     unsigned int test_mode, unsigned int id)
{
	if (id == XFER_STATS_TOTAL)
		fputs("total    ", stdout);
	else
		printf("%-8u ", id);

	xfer_stats_1_print(&stats->rx);
	fputs("    ", stdout);
	xfer_stats_1_print(&stats->tx);

	switch(test_mode) {
	case MODE_TCP_STREAM:
		printf("  %15.1lf\n", stats->tx.bytes / elapsed);
		break;
	case MODE_TCP_RR:
		printf("  %10.1lf\n", stats->rx.msgs / elapsed);
		break;
	default:
		return;
	}
}
