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

enum stats_type {
	STATS_TOTAL,		/* total over all iterations */
	STATS_ITER,		/* per iteration results */
	STATS_THREAD,		/* per thread results for iteration */
	STATS_RAW,		/* raw thread data */
};

double *iter_results;

#define STATS_F_TOTAL		(1 << STATS_TOTAL)
#define STATS_F_ITER		(1 << STATS_ITER)
#define STATS_F_THREAD		(1 << STATS_THREAD)
#define STATS_F_RAW		(1 << STATS_RAW)

#define STATS_F_ALL \
	(STATS_F_TOTAL | STATS_F_ITER | STATS_F_THREAD | STATS_F_RAW)

enum verb_level {
	VERB_RESULT,
	VERB_ITER,
	VERB_THREAD,
	VERB_ALL,
	VERB_RAW,

	__VERB_CNT
};

static unsigned int verb_levels[] = {
	[VERB_RESULT]	= STATS_F_TOTAL,
	[VERB_ITER]	= STATS_F_TOTAL | STATS_F_ITER,
	[VERB_THREAD]	= STATS_F_TOTAL | STATS_F_ITER | STATS_F_THREAD,
	[VERB_ALL]	= STATS_F_ALL,
	[VERB_RAW]	= STATS_F_RAW,
};

static const char * verb_level_names[] = {
	[VERB_RESULT]	= "result",
	[VERB_ITER]	= "iter",
	[VERB_THREAD]	= "thread",
	[VERB_ALL]	= "all",
	[VERB_RAW]	= "raw",
};

const char *opts = "H:i:l:m:M:p:s:S:t:nv:";
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
	{ .name = "verbose",		.has_arg = 1,	.val = 'v' },
	{}
};

struct client_config client_config = {
	.ctrl_port	= DEFAULT_PORT,
	.test_mode	= MODE_TCP_STREAM,
	.test_length	= 10,
	.n_iter		= 1,
	.n_threads	= 1,
	.stats_mask	= UINT_MAX,
	.tcp_nodelay	= false,
};
union sockaddr_any server_addr;

static int name_lookup(const char *name, const char *const names[],
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
		case 'v':
			ret = name_lookup(optarg, verb_level_names, __VERB_CNT);
			if (ret >= 0) {
				config->stats_mask = verb_levels[ret];
				break;
			}
			ret = parse_ulong_range("verbosity mask", optarg,
						&val, 0, INT_MAX);
			if (ret < 0) {
				fprintf(stderr, "invalid verbosity '%s'\n",
					optarg);
				return -EINVAL;
			}
			config->stats_mask = val;
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

	if (config->stats_mask == UINT_MAX) {
		if (config->n_iter == 1) {
			if (config->n_threads == 1)
				config->stats_mask = verb_levels[VERB_RESULT];
			else
				config->stats_mask = verb_levels[VERB_THREAD];
		} else {
			config->stats_mask = verb_levels[VERB_ITER];
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

static int collect_stats(struct client_config *config, double *iter_result)
{
	bool show_thread = config->stats_mask & STATS_F_THREAD;
	bool show_raw = config->stats_mask & STATS_F_RAW;
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
	if (show_thread || show_raw)
		printf("test time: %.3lf\n\n", elapsed);

	/* raw stats */
	xfer_stats_reset(&sum_client);
	if (show_raw)
		xfer_stats_raw_header("client");
	for (i = 0; i < n_threads; i++) {
		const struct xfer_stats *wstats =
			&config->workers_data[i].stats;

		if (show_raw)
			xfer_stats_print_raw(wstats, i);
		xfer_stats_add(&sum_client, wstats);
	}
	if (show_raw) {
		xfer_stats_print_raw(&sum_client, XFER_STATS_TOTAL);
		putchar('\n');
	}

	xfer_stats_reset(&sum_server);
	if (show_raw)
		xfer_stats_raw_header("server");
	for (i = 0; i < n_threads; i++) {
		if (show_raw)
			xfer_stats_print_raw(&server_stats[i], i);
		xfer_stats_add(&sum_server, &server_stats[i]);
	}
	if (show_raw) {
		xfer_stats_print_raw(&sum_server, XFER_STATS_TOTAL);
		putchar('\n');
	}

	/* thread stats */
	sum_rslt = sum_rslt_sqr = 0.0;
	for (i = 0; i < n_threads; i++) {
		result = xfer_stats_result(&config->workers_data[i].stats,
					   &server_stats[i], test_mode,
					   elapsed);
		sum_rslt += result;
		sum_rslt_sqr += result * (double)result;

		if (show_thread)
			xfer_stats_print_thread(&config->workers_data[i].stats,
						&server_stats[i], i, test_mode,
						elapsed);
	}
	if (show_thread) {
		xfer_stats_print_thread(&sum_client, &sum_server,
					XFER_STATS_TOTAL, test_mode, elapsed);
		xfer_stats_thread_footer(sum_rslt, sum_rslt_sqr, n_threads,
					 test_mode);
		putchar('\n');
	}
	*iter_result = sum_rslt / n_threads;

	return 0;
}

int one_iteration(struct client_config *config, double *iter_result)
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

	collect_stats(config, iter_result);
	return 0;

err_workers:
	cancel_workers(config);
err_ctrl:
	close(config->ctrl_sd);
err:
	return 2;
}

static void print_iter_result(unsigned int iter, double result, double sum,
			      double sum_sqr,
			      const struct client_config *config)
{
	unsigned int width;
	const char *unit;
	unsigned int n;
	double mdev;

	switch(config->test_mode) {
	case MODE_TCP_STREAM:
		unit = "B/s";
		width = 13;
		break;
	case MODE_TCP_RR:
		unit = "msg/s";
		width = 9;
		break;
	default:
		return;
	}

	if (iter == XFER_STATS_TOTAL) {
		n = config->n_iter;
		printf("all%*s", (int)(width + strlen(unit) + 3), "");
	} else {
		n = iter;
		printf("%-3u %*.1lf %s,", iter, width, result, unit);
	}

	mdev = mdev_n(sum, sum_sqr, n);

	printf("  avg %*.1lf %s, mdev %*.1lf %s (%5.1lf%%)",
	       width, sum / n, unit, width, mdev, unit, 100 * mdev / (sum / n));
	putchar('\n');
}

int all_iterations(struct client_config *config)
{
	unsigned int n_iter, iter;
	unsigned int stats_mask;
	double sum, sum_sqr;
	int ret = 0;

	stats_mask = config->stats_mask;
	n_iter = config->n_iter;

	sum = sum_sqr = 0.0;
	for (iter = 0; iter < n_iter; iter++) {
		double iter_result;

		if (stats_mask & (STATS_F_THREAD | STATS_F_RAW))
			printf("iteration %u\n", iter + 1);
		ret = one_iteration(&client_config, &iter_result);
		if (ret < 0)
			break;

		iter_results[iter] = iter_result;
		sum += iter_result;
		sum_sqr += iter_result * iter_result;
		if (stats_mask & STATS_F_ITER) {
			print_iter_result(iter + 1, iter_result, sum, sum_sqr,
					  &client_config);
			if (stats_mask & (STATS_F_THREAD | STATS_F_RAW))
				putchar('\n');
		}
	}
	if (ret < 0) {
		fprintf(stderr, "*** Iteration %u failed, quitting. ***\n\n",
			iter + 1);
		n_iter = iter;
		if (!n_iter)
			return ret;
	}

	if (n_iter > 1 &&
	    (stats_mask & STATS_F_ITER) &&
	    (stats_mask & (STATS_F_THREAD | STATS_F_RAW))) {
		sum = sum_sqr = 0.0;
		for (iter = 0; iter < n_iter; iter++) {
			double result = iter_results[iter];

			sum += result;
			sum_sqr += result * result;
			print_iter_result(iter + 1, result, sum, sum_sqr,
					  &client_config);
		}
	}
	if (stats_mask & STATS_F_TOTAL)
		print_iter_result(XFER_STATS_TOTAL, 0.0, sum, sum_sqr,
				  &client_config);

	return ret;
}

int main(int argc, char *argv[])
{
	int ret;

	ret = parse_cmdline(argc, argv, &client_config);
	if (ret < 0)
		return 1;
	iter_results = calloc(client_config.n_iter, sizeof(iter_results[0]));
	if (!iter_results)
		return 2;

	printf("port: %hu\n", client_config.ctrl_port);
	printf("rcvbuf_size: %u\n", client_config.rcvbuf_size);
	printf("sndbuf_size: %u\n", client_config.sndbuf_size);
	putchar('\n');

	ret = client_init();
	if (ret < 0)
		goto out_results;
	ret = wsync_init(&client_worker_sync);
	if (ret < 0)
		goto out_results;
	ret = alloc_buffers(&client_config);
	if (ret < 0)
		goto out_ws;
	ret = all_iterations(&client_config);

	free_buffers(&client_config);
out_ws:
	wsync_destroy(&client_worker_sync);
out_results:
	free(iter_results);
	return (ret < 0) ? 2 : 0;
}
