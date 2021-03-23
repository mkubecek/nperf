#ifndef __NPERF_WSYNC_H
#define __NPERF_WSYNC_H

#include <pthread.h>

struct worker_sync {
	pthread_mutex_t	mtx;
	pthread_cond_t	cv;
	unsigned int	counter;
	unsigned int	state;
};

static inline int wsync_init(struct worker_sync *ws)
{
	pthread_condattr_t attr;
	int ret;

	ret = pthread_condattr_init(&attr);
	if (ret)
		return -ret;
	ret = pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
	if (ret)
		return -ret;
	ret = pthread_cond_init(&ws->cv, &attr);
	if (ret)
		return -ret;
	ret = pthread_condattr_destroy(&attr);

	return -ret;
}

static inline void wsync_destroy(struct worker_sync *ws)
{
	pthread_cond_destroy(&ws->cv);
}

static inline void wsync_set_state(struct worker_sync *ws, unsigned int state)
{
	pthread_mutex_lock(&ws->mtx);
	ws->state = state;
	pthread_cond_broadcast(&ws->cv);
	pthread_mutex_unlock(&ws->mtx);
}

static inline void wsync_wait_for_state(struct worker_sync *ws,
					unsigned int state)
{
	pthread_mutex_lock(&ws->mtx);
	while (ws->state != state)
		pthread_cond_wait(&ws->cv, &ws->mtx);
	pthread_mutex_unlock(&ws->mtx);
}

static inline void wsync_reset_counter(struct worker_sync *ws)
{
	pthread_mutex_lock(&ws->mtx);
	ws->counter = 0;
	pthread_mutex_unlock(&ws->mtx);
}

static inline void wsync_inc_counter(struct worker_sync *ws)
{
	pthread_mutex_lock(&ws->mtx);
	ws->counter++;
	pthread_cond_broadcast(&ws->cv);
	pthread_mutex_unlock(&ws->mtx);
}

static inline void wsync_wait_for_counter(struct worker_sync *ws,
					  unsigned int count)
{
	pthread_mutex_lock(&ws->mtx);
	while (ws->counter < count)
		pthread_cond_wait(&ws->cv, &ws->mtx);
	pthread_mutex_unlock(&ws->mtx);
}

static inline int wsync_sleep(struct worker_sync *ws, unsigned int timeout)
{
	struct timespec ts;
	int ret;

	ret = clock_gettime(CLOCK_MONOTONIC, &ts);
	if (ret < 0)
		return ret;
	ts.tv_sec += timeout;

	pthread_mutex_lock(&ws->mtx);
	while (!ret)
		ret = pthread_cond_timedwait(&ws->cv, &ws->mtx, &ts);
	pthread_mutex_unlock(&ws->mtx);
	return (ret == ETIMEDOUT) ? 0 : -ret;
}

#endif /* __NPERF_WSYNC_H */
