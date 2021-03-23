#ifndef _NPERF_STATS_H
#define _NPERF_STATS_H

#include <inttypes.h>
#include <string.h>
#include <stdio.h>

#include "common.h"

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

struct xfer_stats_1 {
	uint64_t	msgs;
	uint64_t	calls;
	uint64_t	bytes;
};

struct xfer_stats {
	struct xfer_stats_1	rx;
	struct xfer_stats_1	tx;
};

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

static inline void xfer_stats_1_print(const struct xfer_stats_1 *stats)
{
	printf("%8" PRIu64 "%8" PRIu64 "%12" PRIu64,
	       stats->msgs, stats->calls, stats->bytes); 
}

static inline void xfer_stats_print(const struct xfer_stats *stats)
{
	xfer_stats_1_print(&stats->rx);
	xfer_stats_1_print(&stats->tx);
}

#endif /* _NPERF_STATS_H */
