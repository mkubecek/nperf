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

const char *opts = "H:l:m:M:p:s:S:t:n";
const struct option long_opts[] = {
	{ .name = "host",		.has_arg = 1,	.val = 'H' },
	{ .name = "seconds",		.has_arg = 1,	.val = 'l' },
	{ .name = "msg-length",		.has_arg = 1,	.val = 'm' },
	{ .name = "threads",		.has_arg = 1,	.val = 'M' },
	{ .name = "port",		.has_arg = 1,	.val = 'p' },
	{ .name = "rcvbuf-size",	.has_arg = 1,	.val = 's' },
	{ .name = "sndbuf-size",	.has_arg = 1,	.val = 'S' },
	{ .name = "test",		.has_arg = 1,	.val = 't' },
	{ .name = "tcp-nodelay",			.val = 'n' },
	{ .name = "listen-backlog",	.has_arg = 1,	.val = 'l' },
	{}
};

struct client_config client_config = {
	.ctrl_port	= DEFAULT_PORT,
	.test_mode	= MODE_TCP_STREAM,
	.test_length	= 10,
	.n_threads	= 1,
	.tcp_nodelay	= false,
};
union sockaddr_any server_addr;

static int parse_test_mode(const char *name)
{
	unsigned int i;

	for (i = 0; i < MODE_COUNT; i++)
		if (!strcasecmp(name, test_mode_names[i]))
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
			ret = parse_test_mode(optarg);
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

static int ctrl_connect(struct client_config *config)
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

	ret = ctrl_send_start(config);
	if (ret < 0)
		return ret;
	ret = ctrl_recv_start(config);
	if (ret < 0)
		return ret;

	return 0;
}

static int prepare_buffers(struct client_config *config)
{
        long page_size;
        unsigned int i;
        int ret;

        page_size = sysconf(_SC_PAGESIZE);
        if (page_size < 0)
                return -EFAULT;
        config->buff_size = ROUND_UP(config->msg_size, page_size);
        config->buffers_size = config->n_threads * config->buff_size;

        config->workers_data = calloc(sizeof(struct client_worker_data),
                                      config->n_threads);
        if (!config->workers_data)
                return -ENOMEM;

        config->buffers = mmap(NULL, config->buffers_size,
                               PROT_READ | PROT_WRITE,
                               MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (config->buffers == MAP_FAILED) {
                ret = -errno;
                fprintf(stderr, "failed to allocate buffers\n");
                goto err;
        }

        for (i = 0; i < config->n_threads; i++) {
                struct client_worker_data *wdata = &config->workers_data[i];

                wdata->id = i;
                wdata->buff = config->buffers + i * config->buff_size;
                wdata->msg_size = config->msg_size;
                wdata->reply = (config->test_mode == MODE_TCP_RR);
        }

        return 0;
err:
        free(config->workers_data);
        return ret;
}

static void cleanup_buffers(struct client_config *config)
{
        munmap(config->buffers, config->buffers_size);
        free(config->workers_data);
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

static int collect_stats(struct client_config *config)
{
	unsigned int test_mode = config->test_mode;
	double elapsed = config->elapsed;
	struct xfer_stats sum_client;
	unsigned int i;

	printf("test time: %.3lf\n\n", elapsed);
	xfer_stats_reset(&sum_client);
	xfer_stats_thread_header("client", test_mode);
	for (i = 0; i < config->n_threads; i++) {
		const struct xfer_stats *wstats =
			&config->workers_data[i].stats;

		xfer_stats_print_thread(wstats, elapsed, test_mode, i);
		putchar('\n');
		xfer_stats_add(&sum_client, wstats);
	}
	xfer_stats_print_thread(&sum_client, elapsed, test_mode,
				XFER_STATS_TOTAL);
	putchar('\n');

	return 0;
}

int main(int argc, char *argv[])
{
	int ret;

	ret = parse_cmdline(argc, argv, &client_config);
	if (ret < 0)
		return 1;

	printf("port: %hu\n", client_config.ctrl_port);
	printf("rcvbuf_size: %u\n", client_config.rcvbuf_size);
	printf("sndbuf_size: %u\n", client_config.sndbuf_size);

	ret = client_init();
	if (ret < 0)
		goto err;
	ret = wsync_init(&client_worker_sync);
	if (ret < 0)
		goto err;
	ret = ctrl_connect(&client_config);
	if (ret < 0)
		goto err;
	ret = prepare_buffers(&client_config);
	if (ret < 0)
		goto err;
	ret = start_workers(&client_config);
	if (ret < 0)
		goto err_cleanup;
	ret = connect_workers(&client_config);
	if (ret < 0)
		goto err_workers;
	ret = run_test(&client_config);
	if (ret < 0)
		goto err_workers;

	collect_stats(&client_config);
	cleanup_buffers(&client_config);
	return 0;

err_workers:
	cancel_workers(&client_config);
err_cleanup:
	cleanup_buffers(&client_config);
err:
	return 2;
}
