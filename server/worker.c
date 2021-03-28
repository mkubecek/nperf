#include <pthread.h>
#include <stdbool.h>
#include <errno.h>
#include <unistd.h>

#include "worker.h"

#define WORKER_STACK_SIZE 16384

struct server_worker_data *workers_data;

static int recv_msg(struct server_worker_data *data, bool *eof)
{
	unsigned long len = data->msg_size;
	unsigned char *p = data->buff;
	ssize_t chunk;

	*eof = false;
	while (len > 0) {
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

	data->stats.rx.msgs++;
	return 0;
}

static int send_msg(struct server_worker_data *data)
{
	unsigned long len = data->msg_size;
	unsigned char *p = data->buff;
	ssize_t chunk;

	while (len > 0) {
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

	data->stats.tx.msgs++;
	return 0;
}

static void cleanup_close(void *_data)
{
	struct server_worker_data *data = _data;

	close(data->sd);
}

static void *worker_main(void *_data)
{
	struct server_worker_data *data = _data;
	bool do_write = data->reply;
	bool eof = false;
	int ret;

	pthread_cleanup_push(cleanup_close, data);

	while (!eof) {
		ret = recv_msg(data, &eof);
		if (ret < 0 || eof)
			break;
		if (do_write) {
			ret = send_msg(data);
			if (ret < 0)
				break;
		}
	}

	pthread_cleanup_pop(1);

	return NULL;
}

int start_worker(struct server_worker_data *data)
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
