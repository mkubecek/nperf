#ifndef __NPERF_CLIENT_WORKER_H
#define __NPERF_CLIENT_WORKER_H

#include <pthread.h>
#include <stdint.h>
#include <stdbool.h>

#include "../common.h"
#include "../wsync.h"
#include "../stats.h"

enum worker_state {
	WS_INIT = 0,
	WS_CONNECT,
	WS_RUN,
	WS_FINISHED
};

extern struct worker_sync client_worker_sync;

struct client_worker_data {
	unsigned int		id;
	int			sd;
	uint16_t		client_port;
	unsigned char 		*buff;
	bool			reply;
	unsigned long		msg_size;
	pthread_t		tid;
	struct xfer_stats	stats;
	int			status;
	int			test_finished;
} __attribute__ ((__aligned__ (CACHELINE_SIZE)));

extern struct client_worker_data *workers_data;
extern union sockaddr_any test_addr;

int start_client_worker(struct client_worker_data *data);

#endif /* __NPERF_CLIENT_WORKER_H */
