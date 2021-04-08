#ifndef __NPERF_CLIENT_MAIN_H
#define __NPERF_CLIENT_MAIN_H

#include <stdint.h>

#include "../stats.h"
#include "../estimate.h"

enum stats_type {
	STATS_TOTAL,		/* total over all iterations */
	STATS_ITER,		/* per iteration results */
	STATS_THREAD,		/* per thread results for iteration */
	STATS_RAW,		/* raw thread data */
};

#define STATS_F_TOTAL		(1 << STATS_TOTAL)
#define STATS_F_ITER		(1 << STATS_ITER)
#define STATS_F_THREAD		(1 << STATS_THREAD)
#define STATS_F_RAW		(1 << STATS_RAW)

#define STATS_F_ALL \
	(STATS_F_TOTAL | STATS_F_ITER | STATS_F_THREAD | STATS_F_RAW)

struct client_config {
	const char			*server_host;
	uint16_t			ctrl_port;
	uint16_t			test_port;
	unsigned int			test_mode;
	unsigned int			test_length;
	unsigned int			min_iter;
	unsigned int			max_iter;
	unsigned int			n_threads;
	enum confid_level		confid_level;
	double				confid_target;
	bool				confid_target_set;
	unsigned int			stats_mask;
	unsigned int			rcvbuf_size;
	unsigned int			sndbuf_size;
	unsigned int			msg_size;
	bool				tcp_nodelay;
	struct print_options		print_opts;
	int				ctrl_sd;
	unsigned char			*buffers;
	unsigned long			buff_size;
	unsigned long			buffers_size;
	struct client_worker_data       *workers_data;
	double				elapsed;
};

extern struct client_config client_config;

#endif /* __NPERF_CLIENT_MAIN_H */
