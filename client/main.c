#include <stdio.h>
#include <string.h>
#include <getopt.h>
#include <stdint.h>
#include <stdbool.h>
#include <limits.h>
#include <math.h>
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
#include "cmdline.h"

double *iter_results;

struct client_config client_config = {
	.ctrl_port	= DEFAULT_PORT,
	.test_mode	= MODE_TCP_STREAM,
	.test_length	= 10,
	.min_iter	= 1,
	.max_iter	= 1,
	.n_threads	= 1,
	.stats_mask	= UINT_MAX,
	.tcp_nodelay	= false,
};
union sockaddr_any server_addr;

/* USR1 signal is sent by control thread to all workers when the test interval
 * is over. No handler is needed but we need to clear SA_RESTART flag so that
 * long writes or reads are interrupted as quickly as possible.
 */
static int client_set_usr1_handler(void)
{
	struct sigaction action;
	int ret;

	ret = sigaction(SIGUSR1, NULL, &action);
	if (ret < 0)
		return -errno;
	action.sa_handler = SIG_IGN;
	action.sa_flags &= ~(int)SA_RESTART;
	ret = sigaction(SIGUSR1, &action, NULL);
	if (ret < 0)
		return -errno;

	return 0;
}

static int client_init(void)
{
	int ret;

	ret = client_set_usr1_handler();
	if (ret < 0)
		return ret;
	return ignore_signal(SIGPIPE);
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
	int sd = -1;
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
		freeaddrinfo(results);
		fprintf(stderr, "failed to connect to '%s'\n",
			config->server_host);
		return ret;
	}

	config->ctrl_sd = sd;
	memcpy(&server_addr, result->ai_addr,  result->ai_addrlen);
	freeaddrinfo(results);
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

static void ctrl_close(struct client_config *config)
{
	close(config->ctrl_sd);
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

static void kill_workers(struct client_config *config)
{
	unsigned int i;

	for (i = 0; i < config->n_threads; i++)
		config->workers_data[i].test_finished = 1;
	for (i = 0; i < config->n_threads; i++)
		pthread_kill(config->workers_data[i].tid, SIGUSR1);
	for (i = 0; i < config->n_threads; i++)
		pthread_join(config->workers_data[i].tid, NULL);
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
	kill_workers(&client_config);
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
	double result, sum_rslt, sum_rslt_sqr;
	double elapsed = config->elapsed;
	struct xfer_stats *server_stats;
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
						elapsed, &config->print_opts);
	}
	free(server_stats);

	if (show_thread) {
		xfer_stats_print_thread(&sum_client, &sum_server,
					XFER_STATS_TOTAL, test_mode, elapsed,
					&config->print_opts);
		xfer_stats_thread_footer(sum_rslt, sum_rslt_sqr, n_threads,
					 &config->print_opts);
		putchar('\n');
	}
	*iter_result = sum_rslt;

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
		goto err_close;
	ret = start_workers(config);
	if (ret < 0)
		goto err_close;
	ret = connect_workers(config);
	if (ret < 0)
		goto err_workers;
	ret = run_test(config);
	if (ret < 0)
		goto err_workers;

	ret = collect_stats(config, iter_result);
	ctrl_close(config);
	return ret;

err_workers:
	kill_workers(config);
err_close:
	ctrl_close(config);
err:
	return 2;
}

int all_iterations(struct client_config *config)
{
	double confid_target_hw, confid_ival_hw;
	unsigned int n_iter, iter;
	unsigned int stats_mask;
	bool confid_target_set;
	double sum, sum_sqr;
	int ret = 0;

	stats_mask = config->stats_mask;
	confid_target_hw = 0.999 * config->confid_target / 200.0;
	confid_target_set = config->confid_target_set;
	confid_ival_hw = HUGE_VAL;

	sum = sum_sqr = 0.0;
	n_iter = 0;
	for (iter = 0; iter < config->max_iter; iter++) {
		double iter_result;

		if (stats_mask & (STATS_F_THREAD | STATS_F_RAW))
			printf("iteration %u\n", iter + 1);
		ret = one_iteration(&client_config, &iter_result);
		if (ret < 0)
			break;
		n_iter++;

		iter_results[iter] = iter_result;
		sum += iter_result;
		sum_sqr += iter_result * iter_result;
		if (iter > 0)
			confid_ival_hw = confid_interval(sum, sum_sqr, iter + 1,
							 config->confid_level) /
					 (sum / (iter + 1));
		if (stats_mask & STATS_F_ITER) {
			print_iter_result(iter + 1, iter + 1, iter_result,
					  sum, sum_sqr, config->confid_level,
					  &config->print_opts);
			if (stats_mask & (STATS_F_THREAD | STATS_F_RAW))
				putchar('\n');
		}
		fflush(stdout);

		if (confid_target_set && (n_iter >= config->min_iter) &&
		    (confid_ival_hw <= confid_target_hw))
			break;
	}
	if (ret < 0) {
		fprintf(stderr, "*** Iteration %u failed, quitting. ***\n\n",
			iter + 1);
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
			print_iter_result(iter + 1, n_iter, result, sum,
					  sum_sqr, config->confid_level,
					  &config->print_opts);
		}
	}

	if (config->confid_target_set &&
	    ((n_iter < 2) || (200.0 * confid_ival_hw > config->confid_target)))
		fprintf(stderr,
			"*** Failed to reach confidence target.\n"
			"*** Confidence interval width is %.4lg%% (+/- %.4lg%%), requested %.4lg%%.\n"
			"*** The result is not reliable enough.\n",
			200.0 * confid_ival_hw, 100.0 * confid_ival_hw,
			config->confid_target);
	if (stats_mask & STATS_F_TOTAL)
		print_iter_result(XFER_STATS_TOTAL, n_iter, 0.0,
				  sum, sum_sqr, config->confid_level,
				  &config->print_opts);

	return ret;
}

int main(int argc, char *argv[])
{
	int ret;

	ret = parse_cmdline(argc, argv, &client_config);
	if (ret < 0)
		return 1;
	iter_results = calloc(client_config.max_iter, sizeof(iter_results[0]));
	if (!iter_results)
		return 2;

	printf("server: %s, port %hu\n", client_config.server_host,
	       client_config.ctrl_port);
	if (client_config.min_iter < client_config.max_iter)
		printf("iterations: %u-%u", client_config.min_iter,
		       client_config.max_iter);
	else
		printf("iterations: %u", client_config.min_iter);
	printf(", threads: %u, test length: %u\n",
	       client_config.n_threads, client_config.test_length);
	if (client_config.confid_target_set)
		printf("confidence target: %.1lf%% (+/- %.1lf%%) at %u%%\n",
		       client_config.confid_target,
		       client_config.confid_target / 2,
		       confid_level_output(client_config.confid_level));
	printf("test: %s, message size: %u\n",
	       test_mode_names[client_config.test_mode],
	       client_config.msg_size);
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
