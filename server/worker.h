#ifndef __NPERF_SERVER_WORKER_H
#define __NPERF_SERVER_WORKER_H

#include <stdint.h>
#include <stdbool.h>

#include "../common.h"
#include "../stats.h"

struct server_worker_data {
	unsigned int		id;
	int			sd;
	uint16_t		client_port;
	unsigned char 		*buff;
	bool			reply;
	unsigned long		msg_size;
	pthread_t		tid;
	struct xfer_stats	stats;
	int			status;
} __attribute__ ((__aligned__ (CACHELINE_SIZE)));

extern struct server_worker_data *workers_data;

int start_worker(struct server_worker_data *data);

#endif /* __NPERF_SERVER_WORKER_H */
