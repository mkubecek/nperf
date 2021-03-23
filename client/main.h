#ifndef __NPERF_CLIENT_MAIN_H
#define __NPERF_CLIENT_MAIN_H

#include <stdint.h>

struct client_config {
	const char			*server_host;
	uint16_t			ctrl_port;
	uint16_t			test_port;
	unsigned int			test_mode;
	unsigned int			test_length;
	unsigned int			n_threads;
	unsigned int			rcvbuf_size;
	unsigned int			sndbuf_size;
	unsigned int			msg_size;
	bool				tcp_nodelay;
	int				ctrl_sd;
	unsigned char			*buffers;
	unsigned long			buff_size;
	unsigned long			buffers_size;
	struct client_worker_data       *workers_data;
};

extern struct client_config client_config;

#endif /* __NPERF_CLIENT_MAIN_H */
