#include <pthread.h>
#include <string.h>
#include <errno.h>
#include <malloc.h>
#include <unistd.h>
#include <limits.h>
#include <sys/mman.h>
#include <netinet/in.h>
#include <netinet/ip6.h>
#include <netinet/tcp.h>

#include "../common.h"
#include "control.h"
#include "worker.h"

#define MIN_SKTBUF 65536
#define SKTBUF_ALIGN 65536
#define MIN_LISTEN_BACKLOG 16
#define MAX_LISTEN_BACKLOG 16384

struct server_ctrl_config {
	unsigned int			mode;
	unsigned int			n_threads;
	unsigned int			msg_size;
	bool				tcp_nodelay;
	uint16_t			port;
	unsigned char			*buffers;
	unsigned long			buff_size;
	unsigned long			buffers_size;
	struct server_worker_data	*workers_data;
	int				ctrl_sd;
	int				status;
};
static struct server_ctrl_config config;

struct client_ctrl_msg client_msg;

static struct server_worker_data *worker_data(struct server_ctrl_config
					      *config, unsigned int i)
{
	return config->workers_data + i;
}


static int ctrl_get_config(struct server_ctrl_config *config)
{
	long page_size;
	int ret;

	page_size = sysconf(_SC_PAGESIZE);
	if (page_size < 0)
		return -EFAULT;

	ret = ctrl_recv_msg(config->ctrl_sd, &client_msg, sizeof(client_msg));
	if (ret < 0) {
		close(config->ctrl_sd);
		return -EINVAL;
	}

	config->mode = ntohl(client_msg.mode);
	config->n_threads = ntohl(client_msg.n_threads);
	config->msg_size = ntohl(client_msg.msg_size);
	config->tcp_nodelay = client_msg.tcp_nodelay;
	config->buff_size = ROUND_UP(config->msg_size, page_size);
	config->buffers_size = config->n_threads * config->buff_size;
	config->buffers_size +=
		ROUND_UP(config->n_threads * sizeof(struct server_worker_data),
			 page_size);

	return 0;
}

static int prepare_buffers(struct server_ctrl_config *config)
{
	unsigned int i;
	int ret;

	config->buffers = mmap(NULL, config->buffers_size,
			       PROT_READ | PROT_WRITE,
			       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (config->buffers == MAP_FAILED) {
		ret = -errno;
		fprintf(stderr, "failed to allocate buffers\n");
		return ret;
	}
	config->workers_data = (struct server_worker_data *)
	       (config->buffers + config->n_threads * config->buff_size);

	for (i = 0; i < config->n_threads; i++) {
		struct server_worker_data *wdata = worker_data(config, i);

		wdata->id = i;
		wdata->buff = config->buffers + i * config->buff_size;
		wdata->msg_size = config->msg_size;
		wdata->reply = (config->mode == MODE_TCP_RR);
	}

	return 0;
}

static void cleanup_buffers(struct server_ctrl_config *config)
{
	munmap(config->buffers, config->buffers_size);
}

static int setup_listener(struct server_ctrl_config *config)
{
	union sockaddr_any addr = {
		.sa6 = {
			.sin6_family    = AF_INET6,
			.sin6_port      = htons(0),
			.sin6_addr      = IN6ADDR_ANY_INIT,
		}
	};
	unsigned int listen_backlog = config->n_threads;
	socklen_t addr_len = sizeof(addr);
	int val;
	int ret;
	int sd;

	sd = socket(PF_INET6, SOCK_STREAM, IPPROTO_TCP);
	if (sd < 0) {
		ret = -errno;
		perror("socket");
		return ret;
	}

	val = 0;
	ret = setsockopt(sd, SOL_IPV6, IPV6_V6ONLY, &val, sizeof(val));
	if (ret < 0) {
		ret = -errno;
		perror("setsockopt(IPV6_V6ONLY)");
		return ret;
	}
	val = 1;
	ret = setsockopt(sd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));
	if (ret < 0) {
		ret = -errno;
		perror("setsockopt(SO_REUSEADDR)");
		return ret;
	}

	ret = bind(sd, (struct sockaddr *)&addr.sa, sizeof(addr.sa6));
	if (ret < 0) {
		ret = -errno;
		perror("bind");
		return ret;
	}
	ret = getsockname(sd, &addr.sa, &addr_len);
	if (ret < 0) {
		ret = -errno;
		perror("getsockname");
		return ret;
	}
	ret = sockaddr_get_port(&addr);
	if (ret < 0) {
		fprintf(stderr, "unknown address family %u\n",
			addr.sa.sa_family);
		return -EINVAL;
	}
	config->port = ret;

	if (listen_backlog < MIN_LISTEN_BACKLOG)
		listen_backlog = MIN_LISTEN_BACKLOG;
	if (listen_backlog > MAX_LISTEN_BACKLOG)
		listen_backlog = MAX_LISTEN_BACKLOG;
	ret = listen(sd, listen_backlog);
	if (ret < 0) {
		ret = -errno;
		perror("listen");
		return ret;
	}

	return sd;
}

static int ctrl_send_start(struct server_ctrl_config *config)
{
	struct server_start_msg msg = {
		.length		= htonl(sizeof(msg)),
		.version	= htonl(CTRL_VERSION),
		.test_id	= client_msg.test_id,
	};
	int ret;

	msg.port = htons(config->port);
	ret = ctrl_send_msg(config->ctrl_sd, &msg, sizeof(msg));
	if (ret < 0)
		return -EFAULT;

	return 0;
}

static int ctrl_run_test(int sd, struct server_ctrl_config *config)
{
	unsigned int n_threads = config->n_threads;
	union sockaddr_any client_addr = {};
	socklen_t addr_len;
	unsigned int i, n;
	int csd;
	int ret;

	n = 0;
	while (n < n_threads) {
		struct server_worker_data *wdata;

		addr_len = sizeof(client_addr);
		csd = accept(sd, &client_addr.sa, &addr_len);
		if (csd < 0) {
			perror("accept");
			continue;
		}

		wdata = worker_data(config, n);
		ret = sockaddr_get_port(&client_addr);
		if (ret < 0) {
			close(csd);
			goto failed;
		}
		wdata->client_port = ret;
		wdata->sd = csd;
		ret = start_worker(wdata);
		if (ret < 0) {
			close(csd);
			goto failed;
		}

		n++;
	}

	for (i = 0; i < n_threads; i++)
		pthread_join(worker_data(config, i)->tid, NULL);

	return 0;
failed:
	close(sd);
	for (i = 0; i < n; i++) {
		pthread_cancel(worker_data(config, i)->tid);
	}
	return -EFAULT;
}

static int ctrl_send_end(struct server_ctrl_config *config)
{
	struct server_end_msg msg = {
		.length		= htonl(sizeof(msg)),
		.version	= htonl(CTRL_VERSION),
		.test_id	= client_msg.test_id,
		.status		= htonl(config->status),
		.thread_length	= htonl(sizeof(struct server_thread_info)),
		.n_threads	= htonl(config->n_threads),
	};
	struct server_thread_info tinfo;
	int sd = config->ctrl_sd;
	unsigned int i;
	int ret;

	ret = ctrl_send_msg(sd, &msg, sizeof(msg));
	if (ret < 0)
		return -EFAULT;

	for (i = 0; i < config->n_threads; i++) {
		const struct server_worker_data *wd = worker_data(config, i);

		memset(&tinfo, '\0', sizeof(tinfo));
		xfer_stats_hton(&wd->stats, &tinfo.stats);
		tinfo.client_port = htons(wd->client_port);

		ret = ctrl_send_msg(sd, &tinfo, sizeof(tinfo));
		if (ret < 0)
			return ret;
	}

	return 0;
}

int ctrl_main(int ctrl_sd)
{
	int ret;
	int sd;

	config.ctrl_sd = ctrl_sd;
	ret = ctrl_get_config(&config);
	if (ret < 0)
		goto out_close;
	ret = prepare_buffers(&config);
	if (ret < 0)
		goto out_close;
	sd = setup_listener(&config);
	if (sd < 0) {
		ret = sd;
		goto out_buffers;
	}
	ret = ctrl_send_start(&config);
	if (ret < 0) {
		close(sd);
		goto out_close;
	}
	ret = ctrl_run_test(sd, &config);
	close(sd);
	if (ret < 0)
		goto out_close;
	ret = ctrl_send_end(&config);

out_buffers:
	cleanup_buffers(&config);
out_close:
	close(ctrl_sd);
	return ret;
}
