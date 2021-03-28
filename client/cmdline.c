#include <stdint.h>
#include <limits.h>
#include <stdio.h>
#include <errno.h>
#include <getopt.h>

#include "cmdline.h"
#include "main.h"
#include "../common.h"

#define MAX_THREADS	16384
#define MAX_ITERATIONS	INT_MAX

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

enum {
	LOPT_EXACT = UCHAR_MAX + 1,
	LOPT_BINARY,
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
	{ .name = "binary",				.val = LOPT_BINARY },
	{ .name = "exact",				.val = LOPT_EXACT },
	{}
};

static int name_lookup(const char *name, const char *const names[],
		       unsigned int names_count)
{
	unsigned int i;

	for (i = 0; i < names_count; i++)
		if (!strcasecmp(name, names[i]))
			return i;

	return -ENOENT;
}

int parse_cmdline(int argc, char *argv[], struct client_config *config)
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
		case LOPT_BINARY:
			config->print_opts.binary_prefix = true;
			break;
		case LOPT_EXACT:
			config->print_opts.exact = true;
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

	print_opts_setup(&config->print_opts, config->test_mode);

	return 0;
}
