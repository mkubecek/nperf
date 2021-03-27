#include <pthread.h>
#include <unistd.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

#include "../common.h"
#include "worker.h"
#include "main.h"

#define WORKER_STACK_SIZE 16384

struct client_worker_data *workers_data;
union sockaddr_any test_addr;

struct worker_sync client_worker_sync = {
	.mtx	= PTHREAD_MUTEX_INITIALIZER,
	.cv	= PTHREAD_COND_INITIALIZER,
};

static void cleanup_close(void *_data)
{
	struct client_worker_data *data = _data;

	close(data->sd);
}

int worker_setup(struct client_worker_data *data)
{
	int val;
	int ret;
	int sd;

	sd = socket(test_addr.sa.sa_family, SOCK_STREAM, IPPROTO_TCP);
	if (sd < 0) {
		ret = -errno;
		perror("socket");
		return ret;
	}

	if (client_config.tcp_nodelay) {
		val = 1;
		ret = setsockopt(sd, SOL_TCP, TCP_NODELAY, &val, sizeof(val));
		if (ret < 0) {
			ret = -errno;
			perror("setsockopt(TCP_NODELAY)");
			return ret;
		}
	}
	if (client_config.rcvbuf_size) {
		val = client_config.rcvbuf_size;
		ret = setsockopt(sd, SOL_SOCKET, SO_RCVBUF, &val, sizeof(val));
		if (ret < 0) {
			ret = -errno;
			perror("setsockopt(SO_RCVBUF)");
			return ret;
		}
	}
	if (client_config.sndbuf_size) {
		val = client_config.sndbuf_size;
		ret = setsockopt(sd, SOL_SOCKET, SO_SNDBUF, &val, sizeof(val));
		if (ret < 0) {
			ret = -errno;
			perror("setsockopt(SO_SNDBUF)");
			return ret;
		}
	}

	data->sd = sd;
	return 0;
}

int worker_connect(struct client_worker_data *data)
{
	union sockaddr_any local_addr;
	socklen_t addr_len;
	int ret;

	ret = sockaddr_length(&test_addr);
	if (ret < 0)
		return ret;
	addr_len = ret;
	ret = connect(data->sd, &test_addr.sa, addr_len);
	if (ret < 0)
		return ret;

	addr_len = sizeof(local_addr);
	ret = getsockname(data->sd, &local_addr.sa, &addr_len);
	if (ret < 0) {
		ret = -errno;
		goto err_close;
	}
	ret = sockaddr_get_port(&local_addr);
	if (ret < 0)
		goto err_close;
	data->client_port = ret;

	return 0;
err_close:
	close(data->sd);
	return ret;
}

static int recv_msg(struct client_worker_data *data, bool *eof)
{
	unsigned long len = data->msg_size;
	unsigned char *p = data->buff;
	ssize_t chunk;

	*eof = false;
	while (len > 0 && !data->test_finished) {
		chunk = recv(data->sd, p, len, 0);
		if (chunk < 0) {
			if (errno == EINTR)
				continue;
			data->status = -errno;
			return data->status;
		}
		if (chunk == 0) {
			*eof = true;
			return 0;
		}

		p += chunk;
		len -= chunk;
		data->stats.rx.calls++;
		data->stats.rx.bytes += chunk;
	}

	if (!len)
		data->stats.rx.msgs++;
	return 0;
}

static int send_msg(struct client_worker_data *data)
{
	unsigned long len = data->msg_size;
	unsigned char *p = data->buff;
	ssize_t chunk;

	while (len > 0 && !data->test_finished) {
		chunk = send(data->sd, p, len, 0);
		if (chunk < 0) {
			if (errno == EINTR)
				continue;
			data->status = (errno == EPIPE) ? 0 : -errno;
			return -errno;
		}

		p += chunk;
		len -= chunk;
		data->stats.tx.calls++;
		data->stats.tx.bytes += chunk;
	}

	if (!len)
		data->stats.tx.msgs++;
	return 0;
}

int worker_run_test(struct client_worker_data *data)
{
	bool get_reply = data->reply;
	bool eof = false;
	int ret;

	data->status = 0;
	while (!eof && !data->test_finished) {
		ret = send_msg(data);
		if (ret < 0) {
			data->status = -1;
			break;
		}
		if (data->test_finished)
			break;
		if (get_reply) {
			ret = recv_msg(data, &eof);
			if (ret < 0) {
				data->status = -1;
				break;
			}
		}
	}

	return 0;
}

void *worker_main(void * _data)
{
	struct client_worker_data *data = _data;
	int ret;

	data->status = -1;
	worker_setup(data);
	pthread_cleanup_push(cleanup_close, data);
	wsync_inc_counter(&client_worker_sync);

	wsync_wait_for_state(&client_worker_sync, WS_CONNECT);
	ret = worker_connect(data);
	if (ret < 0)
		goto out;
	wsync_inc_counter(&client_worker_sync);

	wsync_wait_for_state(&client_worker_sync, WS_RUN);
	ret = worker_run_test(data);
	if (ret < 0 || data->test_finished)
		goto out;

out:
	pthread_cleanup_pop(1);
	return NULL;
}

int start_client_worker(struct client_worker_data *data)
{
	pthread_attr_t attr;
	int ret;

	ret = pthread_attr_init(&attr);
	if (ret)
		return -ret;
	ret = pthread_attr_setstacksize(&attr, WORKER_STACK_SIZE);
	if (ret)
		return -ret;
	ret = pthread_create(&data->tid, &attr, worker_main, data);

	pthread_attr_destroy(&attr);
	return -ret;
}
