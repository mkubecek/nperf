#include <string.h>
#include <limits.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <signal.h>

#include "common.h"

const char *const test_mode_names[MODE_COUNT] =
{
	[MODE_TCP_STREAM]	= "TCP_STREAM",
	[MODE_TCP_RR]		= "TCP_RR",
	[MODE_UDP_STREAM]	= "UDP_STREAM",
	[MODE_UDP_RR]		= "UDP_RR",
};

int parse_ulong(const char *name, const char *str, unsigned long *val)
{
	unsigned long factor = 1;
	char *eptr;

	if (!*str)
		goto invalid;
	*val = strtoul(str, &eptr, 10);

	switch(*eptr++) {
	case '\0':
		eptr--;
		break;
	case 'k':
		factor = 1000UL;
		break;
	case 'K':
		factor = 1UL << 10;
		break;
	case 'm':
		factor = 1000000UL;
		break;
	case 'M':
		factor = 1UL << 20;
		break;
	case 'g':
		factor = 1000000000UL;
		break;
	case 'G':
		factor = 1UL << 30;
		break;
	case 't':
		factor = 1000000000000UL;
		break;
	case 'T':
		factor = 1UL << 40;
		break;
	default:
		goto invalid;
	}
	if (*eptr)
		goto invalid;

	if (*val > ULONG_MAX / factor) {
		fprintf(stderr, "value '%s' of %s too large\n", str, name);
		return -EINVAL;
	}
	*val *= factor;
	return 0;

invalid:
	fprintf(stderr, "invalid value '%s' of %s\n", str, name);
	return -EINVAL;
}

int parse_ulong_range(const char *name, const char *str, unsigned long *val,
		      unsigned long min_val, unsigned long max_val)
{
	int ret;

	ret = parse_ulong(name, str, val);
	if (ret < 0)
		return ret;
	if (*val < min_val) {
		fprintf(stderr, "value %lu of %s is lower than %lu\n",
			*val, name, min_val);
		return -EINVAL;
	}
	if (*val > max_val) {
		fprintf(stderr, "value %lu of %s is higher than %lu\n",
			*val, name, max_val);
		return -EINVAL;
	}

	return 0;
}

int ignore_signal(int signum)
{
	struct sigaction action;
	int ret;

	ret = sigaction(signum, NULL, &action);
	if (ret < 0)
		return -errno;
	action.sa_handler = SIG_IGN;
	ret = sigaction(signum, &action, NULL);
	if (ret < 0)
		return -errno;

	return 0;
}

struct __common_ctrl_header {
	uint32_t	length;
	uint32_t	version;
};

int send_block(int sd, const void *buff, unsigned int length)
{
	const unsigned char *bp = buff;
	unsigned int rest = length;
	ssize_t chunk;

	while (rest > 0) {
		chunk = send(sd, bp, rest, 0);
		if (chunk < 0) {
			if (errno == EINTR)
				continue;
			return -errno;
		}

		bp += chunk;
		rest -= chunk;
	}

	return 0;
}

int recv_block(int sd, void *buff, unsigned int length)
{
	unsigned int rest = length;
	unsigned char *bp = buff;
	ssize_t chunk;

	while (rest > 0) {
		chunk = recv(sd, bp, rest, MSG_WAITALL);
		if (chunk < 0) {
			if (errno == EINTR)
				continue;
			return -errno;
		}
		if (!chunk)
			return -EINVAL;

		bp += chunk;
		rest -= chunk;
	}

	return 0;
}

int ctrl_send_msg(int sd, const void *buff, unsigned int length)
{
	return send_block(sd, buff, length);
}

int ctrl_recv_msg(int sd, void *buff, unsigned int length)
{
	const struct __common_ctrl_header *hdr = buff;
	unsigned int rest = length;
	unsigned char *bp = buff;
	ssize_t chunk;

	while (rest > length - sizeof(*hdr)) {
		chunk = recv(sd, bp, rest, 0);
		if (chunk < 0) {
			if (errno == EINTR)
				continue;
			return -errno;
		}
		if (!chunk)
			return -EINVAL;

		bp += chunk;
		rest -= chunk;
	}

	if (ntohl(hdr->length) != length || ntohl(hdr->version) != CTRL_VERSION)
		return -EINVAL;

	while (rest > 0) {
		chunk = recv(sd, bp, rest, MSG_WAITALL);
		if (chunk < 0)
			return -errno;

		bp += chunk;
		rest -= chunk;
	}

	return 0;
}
