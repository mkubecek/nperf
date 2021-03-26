#include <stdio.h>
#include <string.h>
#include <getopt.h>
#include <stdint.h>
#include <stdbool.h>
#include <limits.h>
#include <errno.h>
#include <unistd.h>
#include <netdb.h>
#include <malloc.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/mman.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

#include "../common.h"
#include "main.h"
#include "worker.h"

#define MAX_ITERATIONS INT_MAX

const char *opts = "H:i:l:m:M:p:s:S:t:n";
const struct option long_opts[] = {
	{ .name = "host",		.has_arg = 1,	.val = 'H' },
	{ .name = "iterate",		.has_arg = 1,	.val = 'i' },
	{ .name = "seconds",		.has_arg = 1,	.val = 'l' },
	{ .name = "msg-length",		.has_arg = 1,	.val = 'm' },
	{ .name = "threads",		.has_arg = 1,	.val = 'M' },
	{ .name = "port",		.has_arg = 1,	.val = 'p' },
	{ .name = "rcvbuf-size",	.has_arg = 1,	.val = 's' },
	{ .name = "sndbuf-size",	.has_arg = 1,	.val = 'S' },
	{ .name = "test",		.has_arg = 1,	.val = 't' },
	{ .name = "tcp-nodelay",			.val = 'n' },
	{}
};

struct client_config client_config = {
	.ctrl_port	= DEFAULT_PORT,
	.test_mode	= MODE_TCP_STREAM,
	.test_length	= 10,
	.n_iter		= 1,
	.n_threads	= 1,
	.tcp_nodelay	= false,
};
union sockaddr_any server_addr;

static int name_lookup(const char *name, const char *names[],
		       unsigned int names_count)
{
	unsigned int i;

	for (i = 0; i < names_count; i++)
		if (!strcasecmp(name, names[i]))
			return i;

	return -ENOENT;
}

static int client_init(void)
{
	return ignore_signal(SIGPIPE);
}

static int parse_cmdline(int argc, char *argv[], struct client_config *config)
{
	unsigned long val;
	int ret;
	int c;

	while ((c = getopt_long(argc, argv, opts, long_opts, NULL)) != -1) {
		switch(c) {
		case 'H':
			config->server_host = optarg;
			break;
		case 'i':
			ret = parse_ulong_range("iterations", optarg, &val,
						0, MAX_ITERATIONS);
			if (ret < 0)
				return -EINVAL;
			config->n_iter = val;
			break;
		case 'l':
			ret = parse_ulong_range("test length", optarg, &val,
						0, UINT_MAX);
			if (ret < 0)
				return -EINVAL;
			config->test_length = val;
			break;
		case 'm':
			ret = parse_ulong_range("message size", optarg, &val,
						0, INT_MAX);
			if (ret < 0)
				return -EINVAL;
			config->msg_size = val;
			break;
		case 'M':
			ret = parse_ulong_range("thread count", optarg, &val,
						1, MAX_THREADS);
			if (ret < 0)
				return -EINVAL;
			config->n_threads = val;
			break;
		case 'n':
			config->tcp_nodelay = true;
			break;
		case 'p':
			ret = parse_ulong_range("port", optarg, &val,
						0, USHRT_MAX);
			if (ret < 0)
				return -EINVAL;
			config->ctrl_port = val;
			break;
		case 's':
			ret = parse_ulong_range("receive buffer size", optarg,
						&val, 0, INT_MAX);
			if (ret < 0)
				return -EINVAL;
			config->rcvbuf_size = val;
			break;
		case 'S':
			ret = parse_ulong_range("send buffer size", optarg,
						&val, 0, INT_MAX);
			if (ret < 0)
				return -EINVAL;
			config->sndbuf_size = val;
			break;
		case 't':
			ret = name_lookup(optarg, test_mode_names, MODE_COUNT);
			if (ret < 0) {
				fprintf(stderr, "invalid test '%s'\n", optarg);
				return -EINVAL;
			}
			config->test_mode = ret;
			break;
		case '?':
			return -EINVAL;
		default:
			fprintf(stderr, "unknown option '-%c'\n", c);
			return -EINVAL;
		}
	}

	if (!config->msg_size) {
		switch(config->test_mode) {
		case MODE_TCP_STREAM:
			config->msg_size = 1U << 20; /* 1 MB */
			break;
		case MODE_TCP_RR:
			config->msg_size = 1U;
			break;
		default:
			fprintf(stderr, "test mode '%s' not supported yet\n",
				test_mode_names[config->test_mode]);
			return -EINVAL;
		}
	}

	return 0;
}

static int ctrl_send_start(struct client_config *config)
{
	struct client_ctrl_msg msg = {
		.length		= htonl(sizeof(msg)),
		.version	= htonl(CTRL_VERSION),
		.test_id	= htonl(1),
		.mode		= htonl(config->test_mode),
		.n_threads	= htonl(config->n_threads),
		.msg_size	= htonl(config->msg_size),
		.tcp_nodelay	= !!config->tcp_nodelay,
	};
	int ret;

	ret = ctrl_send_msg(config->ctrl_sd, &msg, sizeof(msg));
	if (ret < 0)
		return ret;

	return 0;
}

static int ctrl_recv_start(struct client_config *config)
{
	struct server_start_msg msg;
	int ret;

	ret = ctrl_recv_msg(config->ctrl_sd, &msg, sizeof(msg));
	if (ret < 0)
		return ret;

	config->test_port = ntohs(msg.port);
	memcpy(&test_addr, &server_addr, sizeof(test_addr));
	ret = sockaddr_set_port(&test_addr, config->test_port);
	if (ret < 0)
		return ret;

	return 0;
}

static int ctrl_connect_with_lookup(struct client_config *config)
{
	struct addrinfo hints = {
		.ai_flags	= 0,
		.ai_family	= AF_UNSPEC,
		.ai_socktype	= SOCK_STREAM,
		.ai_protocol	= IPPROTO_TCP,
	};
	int sd;
	struct addrinfo *results = NULL;
	struct addrinfo *result;
	int ret;

	ret = getaddrinfo(config->server_host, NULL, &hints, &results);
	if (ret) {
		fprintf(stderr, "host '%s' lookup failed: %s\n",
			config->server_host, gai_strerror(ret));
		return -ENOENT;
	}

	ret = -ENOENT;
	for (result = results; result; result = result->ai_next) {
		ret = sockaddr_set_port((union sockaddr_any *)result->ai_addr,
					config->ctrl_port);
		if (ret < 0)
			continue;
		ret = socket(result->ai_family, SOCK_STREAM, IPPROTO_TCP);
		if (ret < 0)
			continue;
		sd = ret;
		ret = connect(sd, result->ai_addr, result->ai_addrlen);
		if (ret == 0)
			break;

		ret = -errno;
		perror("connect");
		close(sd);
	}
	if (ret < 0) {
		fprintf(stderr, "failed to connect to '%s'\n",
			config->server_host);
		return ret;
	}

	config->ctrl_sd = sd;
	memcpy(&server_addr, result->ai_addr,  result->ai_addrlen);
	return 0;
}

static int ctrl_connect_fast(struct client_config *config)
{
	socklen_t addr_len;
	int ret, sd;

	sd = socket(server_addr.sa.sa_family, SOCK_STREAM, IPPROTO_TCP);
	if (sd < 0)
		return -errno;
	ret = sockaddr_length(&server_addr);
	if (ret < 0)
		goto err;
	addr_len = ret;
	ret = connect(sd, &server_addr.sa, addr_len);
	if (ret < 0) {
		ret = -errno;
		goto err;
	}

	config->ctrl_sd = sd;
	return 0;
err:
	close(sd);
	return ret;
}

static int ctrl_initialize(struct client_config *config)
{
	int ret;

	if (server_addr.sa.sa_family)
		ret = ctrl_connect_fast(config);
	else
		ret = ctrl_connect_with_lookup(config);
	if (ret < 0)
		return ret;
	ret = ctrl_send_start(config);
	if (ret < 0)
		return ret;
	ret = ctrl_recv_start(config);
	if (ret < 0)
		return ret;

	return 0;
}

static int alloc_buffers(struct client_config *config)
{
	long page_size;
	int ret;

	page_size = sysconf(_SC_PAGESIZE);
	if (page_size < 0)
		return -EFAULT;
	config->buff_size = ROUND_UP(config->msg_size, page_size);
	config->buffers_size = config->n_threads * config->buff_size;
	config->buffers_size +=
		ROUND_UP(config->n_threads * sizeof(struct client_worker_data),
			 page_size);


	ret = 0;
	config->buffers = mmap(NULL, config->buffers_size,
			       PROT_READ | PROT_WRITE,
			       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (config->buffers == MAP_FAILED) {
		ret = -errno;
		fprintf(stderr, "failed to allocate buffers\n");
		free(config->workers_data);
	}
	config->workers_data = (struct client_worker_data *)
		(config->buffers + config->n_threads * config->buff_size);

	return ret;
}

static int prepare_buffers(struct client_config *config)
{
	unsigned int i;

	memset(config->workers_data, '\0',
	       config->n_threads * sizeof(struct client_worker_data));

	for (i = 0; i < config->n_threads; i++) {
		struct client_worker_data *wdata = &config->workers_data[i];

		wdata->id = i;
		wdata->buff = config->buffers + i * config->buff_size;
		wdata->msg_size = config->msg_size;
		wdata->reply = (config->test_mode == MODE_TCP_RR);
	}

	return 0;
}

static void free_buffers(struct client_config *config)
{
	munmap(config->buffers, config->buffers_size);
}

static int start_workers(struct client_config *config)
{
	unsigned int n, i;
	int ret;

	wsync_set_state(&client_worker_sync, WS_INIT);
	n = 0;
	while (n < config->n_threads) {
		ret = start_client_worker(&config->workers_data[n]);
		if (ret < 0)
			goto failed;
		n++;
	}
	wsync_wait_for_counter(&client_worker_sync, config->n_threads);

	return 0;
failed:
	for (i = 0; i < n; i++)
		pthread_cancel(config->workers_data[i].tid);
	return -EFAULT;
}

static void cancel_workers(struct client_config *config)
{
	unsigned int i;

	for (i = 0; i < config->n_threads; i++)
		pthread_cancel(config->workers_data[i].tid);
}

static int connect_workers(struct client_config *config)
{
	wsync_reset_counter(&client_worker_sync);
	wsync_set_state(&client_worker_sync, WS_CONNECT);
	wsync_wait_for_counter(&client_worker_sync, config->n_threads);

	return 0;
}

static int run_test(struct client_config *config)
{
	struct timespec ts0, ts1;
	int ret;

	wsync_reset_counter(&client_worker_sync);
	wsync_set_state(&client_worker_sync, WS_RUN);

	ret = clock_gettime(CLOCK_MONOTONIC, &ts0);
	if (ret < 0)
		return -errno;
	ret = wsync_sleep(&client_worker_sync, config->test_length);
	if (ret < 0)
		return ret;
	cancel_workers(&client_config);
	ret = clock_gettime(CLOCK_MONOTONIC, &ts1);
	if (ret < 0)
		return -errno;

	config->elapsed = (ts1.tv_sec - ts0.tv_sec) +
			  1E-9 * (ts1.tv_nsec - ts0.tv_nsec);
	return 0;
}

static int worker_by_port(struct client_config *config, uint16_t port)
{
	unsigned int i;

	for (i = 0; i < config->n_threads; i++)
		if (config->workers_data[i].client_port == port)
			return i;

	return -1;
}

static struct xfer_stats *recv_server_stats(struct client_config *config)
{
	struct server_thread_info tinfo;
	struct xfer_stats *server_stats;
	struct server_end_msg msg;
	unsigned int i;
	int ret;

	server_stats = calloc(config->n_threads, sizeof(server_stats[0]));
	if (!server_stats)
		return NULL;
	ret = ctrl_recv_msg(config->ctrl_sd, &msg, sizeof(msg));
	if (ret < 0 || ntohl(msg.status) ||
	    ntohl(msg.thread_length) != sizeof(tinfo) ||
	    ntohl(msg.n_threads) != config->n_threads)
		goto err;

	for (i = 0; i < config->n_threads; i++) {
		int local_idx;

		ret = recv_block(config->ctrl_sd, &tinfo, sizeof(tinfo));
		if (ret < 0)
			goto err;
		local_idx = worker_by_port(config, ntohs(tinfo.client_port));
		if (local_idx < 0)
			goto err;
		xfer_stats_ntoh(&tinfo.stats, &server_stats[local_idx]);
	}


	return server_stats;
err:
	free(server_stats);
	fprintf(stderr, "failed to receive server stats\n");
	return NULL;
}

static int collect_stats(struct client_config *config)
{
	unsigned int n_threads = config->n_threads;
	unsigned int test_mode = config->test_mode;
	struct xfer_stats sum_client, sum_server;
	double elapsed = config->elapsed;
	struct xfer_stats *server_stats;
	double sum_rslt, sum_rslt_sqr;
	uint64_t result;
	unsigned int i;

	server_stats = recv_server_stats(config);
	if (!server_stats)
		return -EFAULT;
	printf("test time: %.3lf\n\n", elapsed);

	/* raw stats */
	xfer_stats_reset(&sum_client);
	xfer_stats_raw_header("client");
	for (i = 0; i < n_threads; i++) {
		const struct xfer_stats *wstats =
			&config->workers_data[i].stats;

		xfer_stats_print_raw(wstats, i);
		xfer_stats_add(&sum_client, wstats);
	}
	xfer_stats_print_raw(&sum_client, XFER_STATS_TOTAL);
	putchar('\n');

	xfer_stats_reset(&sum_server);
	xfer_stats_raw_header("server");
	for (i = 0; i < n_threads; i++) {
		xfer_stats_print_raw(&server_stats[i], i);
		xfer_stats_add(&sum_server, &server_stats[i]);
	}
	xfer_stats_print_raw(&sum_server, XFER_STATS_TOTAL);
	putchar('\n');

	/* thread stats */
	sum_rslt = sum_rslt_sqr = 0.0;
	for (i = 0; i < n_threads; i++) {
		result = xfer_stats_result(&config->workers_data[i].stats,
					   &server_stats[i], test_mode,
					   elapsed);
		sum_rslt += result;
		sum_rslt_sqr += result * (double)result;

		xfer_stats_print_thread(&config->workers_data[i].stats,
					&server_stats[i], i, test_mode,
					elapsed);
	}
	xfer_stats_print_thread(&sum_client, &sum_server, XFER_STATS_TOTAL,
				test_mode, elapsed);
	xfer_stats_thread_footer(sum_rslt, sum_rslt_sqr, n_threads, test_mode);
	putchar('\n');

	return 0;
}

int one_iteration(struct client_config *config)
{
	int ret;

	ret = ctrl_initialize(config);
	if (ret < 0)
		goto err;
	ret = prepare_buffers(config);
	if (ret < 0)
		goto err_ctrl;
	ret = start_workers(config);
	if (ret < 0)
		goto err_ctrl;
	ret = connect_workers(config);
	if (ret < 0)
		goto err_workers;
	ret = run_test(config);
	if (ret < 0)
		goto err_workers;

	collect_stats(config);
	return 0;

err_workers:
	cancel_workers(config);
err_ctrl:
	close(config->ctrl_sd);
err:
	return 2;
}

int main(int argc, char *argv[])
{
	unsigned int iter;
	int ret;

	ret = parse_cmdline(argc, argv, &client_config);
	if (ret < 0)
		return 1;

	printf("port: %hu\n", client_config.ctrl_port);
	printf("rcvbuf_size: %u\n", client_config.rcvbuf_size);
	printf("sndbuf_size: %u\n", client_config.sndbuf_size);
	putchar('\n');

	ret = client_init();
	if (ret < 0)
		return 2;
	ret = wsync_init(&client_worker_sync);
	if (ret < 0)
		return 2;
	ret = alloc_buffers(&client_config);
	if (ret < 0)
		goto out_ws;

	for (iter = 0; iter < client_config.n_iter; iter++) {
		printf("iteration %u\n", iter + 1);
		ret = one_iteration(&client_config);
		if (ret < 0)
			break;
	}

	free_buffers(&client_config);
out_ws:
	wsync_destroy(&client_worker_sync);
	return (ret < 0) ? 2 : 0;
}
