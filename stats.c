#include <stdio.h>
#include <math.h>

#include "stats.h"
#include "common.h"
#include "estimate.h"

static const char *unit_names[] = {
	[PRINT_UNIT_BYTE]	= "B",
	[PRINT_UNIT_TRANS]	= "tr",
};

double mdev_n(double sum, double sum_sqr, unsigned int n)
{
	return sqrt(n * sum_sqr - sum * sum) / n;
}

void print_opts_setup(struct print_options *opts, unsigned int test_mode)
{
	switch(test_mode) {
	case MODE_TCP_STREAM:
		opts->unit = PRINT_UNIT_BYTE;
		opts->width = 13;
		break;
	case MODE_TCP_RR:
		opts->unit = PRINT_UNIT_TRANS;
		opts->width = 9;
		break;
	default:
		break;
	}
}

void print_count(uint64_t val, const struct print_options *opts)
{
	unsigned int base = opts->binary_prefix ? 1024 : 1000;
	const char *unit_str = unit_names[opts->unit];
	const char prefixes[] = " KMGT";
	const char *prefix = &prefixes[0];
	double dval;

	if (opts->exact) {
		printf("%*" PRIu64 " %s", opts->width, val, unit_str);
		return;
	}
	if (val < 20 * base) {
		printf("%5" PRIu64 "    %s", val, unit_str);
		return;
	}

	dval = val / base;
	prefix++;
	while ((dval >= 20 * base) && *prefix) {
		dval /= base;
		prefix++;
	}
	printf("%7.1lf %c%s", dval, *prefix, unit_str);

}

void print_rate(double val, const struct print_options *opts)
{
	unsigned int base = opts->binary_prefix ? 1024 : 1000;
	const char *unit_str = unit_names[opts->unit];
	const char prefixes[] = " KMGT";
	const char *prefix = &prefixes[0];

	if (opts->exact) {
		printf("%*.1lf %s/s", opts->width, val, unit_str);
		return;
	}

	while (val >= 20000 && *prefix) {
		val /= base;
		prefix++;
	}
	printf("%7.1lf %c%s/s", val, *prefix, unit_str);

}

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
			      const struct print_options *opts)
{
	double avg, mdev;

	avg = sum / n;
	mdev = mdev_n(sum, sum_sqr, n);
	fputs("thread average ", stdout);
	print_rate(avg, opts);
	fputs(", mdev ", stdout);
	print_rate(mdev, opts);
	printf(" (%.1lf%%)\n", 100 * mdev / avg);
}

void xfer_stats_print_thread(const struct xfer_stats *client,
			     const struct xfer_stats *server, unsigned int id,
			     unsigned int test_mode, double elapsed,
			     const struct print_options *opts)
{
	struct print_options byte_opts = *opts;

	if (id == XFER_STATS_TOTAL)
		fputs("total     ", stdout);
	else
		printf("thread %-3d", id);

	switch (test_mode) {
	case MODE_TCP_STREAM:
		fputs(" sent ", stdout);
		print_count(client->tx.bytes, opts);
		fputs(", rate ", stdout);
		print_rate(client->tx.bytes / elapsed, opts);
		fputs(", received ", stdout);
		print_count(server->rx.bytes, opts);
		fputs(", rate ", stdout);
		print_rate(server->rx.bytes / elapsed, opts);
		putchar('\n');
		break;
	case MODE_TCP_RR:
		print_opts_setup(&byte_opts, MODE_TCP_STREAM);
		fputs(" sent ", stdout);
		print_count(client->tx.msgs, opts);
		fputs(", rate ", stdout);
		print_rate(client->tx.msgs / elapsed, opts);
		fputs(", ", stdout);
		print_rate(client->tx.bytes / elapsed, &byte_opts);

		print_opts_setup(&byte_opts, MODE_TCP_STREAM);
		fputs(", received ", stdout);
		print_count(client->rx.msgs, opts);
		fputs(", rate ", stdout);
		print_rate(client->rx.msgs / elapsed, opts);
		fputs(", ", stdout);
		print_rate(client->rx.bytes / elapsed, &byte_opts);
		putchar('\n');
		break;
	}
}

void print_iter_result(unsigned int iter, unsigned int n_iter, double result,
		       double sum, double sum_sqr, enum confid_level level,
		       const struct print_options *opts)
{
        double avg, mdev, confid;
	unsigned int n;
	int width;

        if (iter == XFER_STATS_TOTAL) {
                n = n_iter;
		width = opts->exact ? opts->width : 8;
		width += strlen(unit_names[opts->unit]);
                printf("all%*s", width + 5, "");
        } else {
                n = iter;
                printf("%-3u ", iter);
		print_rate(result, opts);
		putchar(',');
        }

	avg = sum / n;
        mdev = mdev_n(sum, sum_sqr, n);
	if (n > 1)
		confid = confid_interval(sum, sum_sqr, n, level);

	fputs("  avg ", stdout);
	print_rate(avg, opts);
	fputs(", mdev ", stdout);
	print_rate(mdev, opts);
	printf(" (%5.1lf%%)", 100.0 * mdev / avg);
	if (n > 1) {
		fputs(", confid. +/- ", stdout);
		print_rate(confid, opts);
		printf(" (%5.1lf%%)", 100.0 * confid / avg);
	}
	putchar('\n');
}
