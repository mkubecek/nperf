#ifndef __NPERF_COMMON_H
#define __NPERF_COMMON_H

#include <stdint.h>
#include <stdio.h>
#include <errno.h>
#include <netinet/in.h>

#include "stats.h"

#define CTRL_VERSION 1
#define DEFAULT_PORT 12543

#define CACHELINE_SIZE 64

#define ROUND_UP(x, d) ((((x) + (d) - 1) / (d)) * (d))

union sockaddr_any {
	struct sockaddr		sa;
	struct sockaddr_in	sa4;
	struct sockaddr_in6	sa6;
};

enum test_mode {
	MODE_TCP_STREAM,
	MODE_TCP_RR,

	MODE_COUNT
};

extern const char *const test_mode_names[MODE_COUNT];

/* all entries in network byte order (BE) */
struct client_ctrl_msg {
	uint32_t	length;
	uint32_t	version;
	uint32_t	test_id;
	uint32_t	mode;
	uint32_t	n_threads;
	uint32_t	msg_size;
	uint8_t		tcp_nodelay;
	uint8_t		_padding[3];
};

/* all entries in network byte order (BE) */
struct server_start_msg {
	uint32_t	length;
	uint32_t	version;
	uint32_t	test_id;
	uint16_t	port;
	uint8_t		_padding[2];
};

/* all entries in network byte order (BE) */
struct server_end_msg {
	uint32_t	length;
	uint32_t	version;
	uint32_t	test_id;
	uint32_t	status;
	uint32_t	thread_length;
	uint32_t	n_threads;
};

/* all entries in network byt order (BE) */
struct server_thread_info {
	struct xfer_stats	stats;
	uint32_t		status;
	uint16_t		client_port;
	uint8_t			_padding[2];
};

int parse_ulong(const char *name, const char *str, unsigned long *val);
int parse_ulong_delim(const char *name, const char *str, unsigned long *val,
		      char delimiter, const char **next);
int parse_ulong_range(const char *name, const char *str, unsigned long *val,
		      unsigned long min_val, unsigned long max_val);
int parse_ulong_range_delim(const char *name, const char *str,
			    unsigned long *val, unsigned long min_val,
			    unsigned long max_val, char delimiter,
			    const char **next);
int parse_double(const char *name, const char *str, double *val);
int parse_double_delim(const char *name, const char *str, double *val,
		       char delimiter, const char **next);
int parse_double_range(const char *name, const char *str, double *val,
		       double min_val, double max_val);
int parse_double_range_delim(const char *name, const char *str,
			     double *val, double min_val,
			     double max_val, char delimiter,
			     const char **next);
int ignore_signal(int signum);
int send_block(int sd, const void *buff, unsigned int length);
int recv_block(int sd, void *buff, unsigned int length);
int ctrl_send_msg(int sd, const void *buff, unsigned int length);
int ctrl_recv_msg(int sd, void *buff, unsigned int length);

static inline int sockaddr_get_port(const union sockaddr_any *addr)
{
	switch(addr->sa.sa_family) {
	case AF_INET:
		return ntohs(addr->sa4.sin_port);
		break;
	case AF_INET6:
		return ntohs(addr->sa6.sin6_port);
		break;
	default:
		return -EINVAL;
	}
}

static inline int sockaddr_set_port(union sockaddr_any *addr, uint16_t port)
{
	switch(addr->sa.sa_family) {
	case AF_INET:
		addr->sa4.sin_port = htons(port);
		return 0;
	case AF_INET6:
		addr->sa6.sin6_port = htons(port);
		return 0;
	default:
		return -EINVAL;
	}
}

static inline int sockaddr_length(const union sockaddr_any *addr)
{
	switch(addr->sa.sa_family) {
	case AF_INET:
		return sizeof(addr->sa4);
	case AF_INET6:
		return sizeof(addr->sa6);
	default:
		return -EINVAL;
	}
}

#endif /* __NPERF_COMMON_H */
