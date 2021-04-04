#ifndef _NPERF_STATS_H
#define _NPERF_STATS_H

#include <inttypes.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

#include "estimate.h"

#define XFER_STATS_TOTAL ((unsigned int)(-1))

enum print_unit {
	PRINT_UNIT_BYTE,
	PRINT_UNIT_TRANS,
};

struct print_options {
	enum print_unit	unit;
	unsigned int	width;
	bool		exact;
	bool		binary_prefix;
};

struct xfer_stats_1 {
	uint64_t	msgs;
	uint64_t	calls;
	uint64_t	bytes;
};

struct xfer_stats {
	struct xfer_stats_1	rx;
	struct xfer_stats_1	tx;
};

void print_opts_setup(struct print_options *opts, unsigned int test_mode);
double xfer_stats_result(const struct xfer_stats *client,
			 const struct xfer_stats *server,
			 unsigned int test_mode, double elapsed);
void xfer_stats_raw_header(const char *label);
void xfer_stats_print_raw(const struct xfer_stats *stats, unsigned int id);
void xfer_stats_print_thread(const struct xfer_stats *client,
			     const struct xfer_stats *server, unsigned int id,
			     unsigned int test_mode, double elapsed,
			     const struct print_options *opts);
void xfer_stats_thread_footer(double sum, double sum_sqr, unsigned int n,
			      const struct print_options *opts);
void print_iter_result(unsigned int iter, unsigned int n_iter, double result,
		       double sum, double sum_sqr, enum confid_level level,
		       const struct print_options *opts);

double mdev_n(double sum, double sum_sqr, unsigned int n);

#if __BYTE_ORDER == __LITTLE_ENDIAN
static inline uint64_t ntoh64(uint64_t x)
{
	return (x << 32) | (x >> 32);
}

static inline uint64_t hton64(uint64_t x)
{
	return (x << 32) | (x >> 32);
}
#else
static inline uint64_t ntoh64(uint64_t x)
{
	return x;
}

static inline uint64_t hton64(uint64_t x)
{
	return x;
}
#endif

static inline void xfer_stats_reset(struct xfer_stats *stats)
{
	memset(stats, '\0', sizeof(*stats));
}

static inline void xfer_stats_1_ntoh(const struct xfer_stats_1 *src,
				     struct xfer_stats_1 *dst)
{
	dst->msgs = ntoh64(src->msgs);
	dst->calls = ntoh64(src->calls);
	dst->bytes = ntoh64(src->bytes);
}

static inline void xfer_stats_ntoh(const struct xfer_stats *src,
				   struct xfer_stats *dst)
{
	xfer_stats_1_ntoh(&src->rx, &dst->rx);
	xfer_stats_1_ntoh(&src->tx, &dst->tx);
}

static inline void xfer_stats_1_hton(const struct xfer_stats_1 *src,
				     struct xfer_stats_1 *dst)
{
	dst->msgs = hton64(src->msgs);
	dst->calls = hton64(src->calls);
	dst->bytes = hton64(src->bytes);
}

static inline void xfer_stats_hton(const struct xfer_stats *src,
				   struct xfer_stats *dst)
{
	xfer_stats_1_hton(&src->rx, &dst->rx);
	xfer_stats_1_hton(&src->tx, &dst->tx);
}

static inline void xfer_stats_1_add(struct xfer_stats_1 *dst,
				    const struct xfer_stats_1 *src)
{
	dst->msgs += src->msgs;
	dst->calls += src->calls;
	dst->bytes += src->bytes;
}

static inline void xfer_stats_add(struct xfer_stats *dst,
				  const struct xfer_stats *src)
{
	xfer_stats_1_add(&dst->rx, &src->rx);
	xfer_stats_1_add(&dst->tx, &src->tx);
}

#endif /* _NPERF_STATS_H */
