#include <stdio.h>
#include <math.h>

#include "stats.h"
#include "common.h"

double xfer_stats_result(const struct xfer_stats *client,
			 const struct xfer_stats *server,
			 unsigned int test_mode, double elapsed)
{
	switch(test_mode) {
	case MODE_TCP_STREAM:
		return server->rx.bytes / elapsed;
	case MODE_TCP_RR:
		return client->rx.msgs / elapsed;
	default:
		return 0;
	}
}

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

void xfer_stats_thread_footer(double sum, double sum_sqr, unsigned int n,
			      unsigned int test_mode)
{
	const char *unit;
	double avg, mdev;

	switch(test_mode) {
	case MODE_TCP_STREAM:
		unit = "B/s";
		break;
	case MODE_TCP_RR:
		unit = "msg/s";
		break;
	default:
		return;
	}

	avg = sum / n;
	mdev = sqrt(n * sum_sqr - sum * sum) / n;
	printf("thread average %.1lf %s, mdev %.1lf %s (%.1lf%%)\n",
	       avg, unit, mdev, unit, 100 * mdev / avg);
}

void xfer_stats_print_thread(const struct xfer_stats *client,
			     const struct xfer_stats *server, unsigned int id,
			     unsigned int test_mode, double elapsed)
{
	if (id == XFER_STATS_TOTAL)
		fputs("total     ", stdout);
	else
		printf("thread %-3d", id);

	switch (test_mode) {
	case MODE_TCP_STREAM:
		printf(" sent %13" PRIu64 " B, rate %13.1lf B/s",
		       client->tx.bytes, client->tx.bytes / elapsed);
		printf(", received %13" PRIu64 "B, rate %13.1lf B/s\n",
		       server->rx.bytes, server->rx.bytes / elapsed);
		break;
	case MODE_TCP_RR:
		printf(" sent %9" PRIu64 " msgs, %9.1lf msg/s, %13.1lf B/s",
		       client->tx.msgs, client->tx.msgs / elapsed,
		       client->tx.bytes / elapsed);
		printf(", received %9" PRIu64 " msgs, %9.1lf msg/s, %13.1lf B/s\n",
		       server->rx.msgs, server->rx.msgs / elapsed,
		       server->rx.bytes / elapsed);
		break;
	}
}
