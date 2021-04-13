#include <stdint.h>
#include <limits.h>
#include <stdio.h>
#include <errno.h>
#include <getopt.h>
#include <stdlib.h>

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

const char *opts = "hH:i:I:l:m:M:p:s:S:t:nv:";
const struct option long_opts[] = {
	{ .name = "help",				.val = 'h' },
	{ .name = "host",		.has_arg = 1,	.val = 'H' },
	{ .name = "iterate",		.has_arg = 1,	.val = 'i' },
	{ .name = "confidence",		.has_arg = 1,	.val = 'I' },
	{ .name = "seconds",		.has_arg = 1,	.val = 'l' },
	{ .name = "msg-size",		.has_arg = 1,	.val = 'm' },
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

static const char *help_text = "\n"
"  nperf [ options ]\n"
"\n"
"  Network performance benchmarking utility (client side). Runs requested\n"
"  test against a server running an instance of nperfd and displays result\n"
"  of the test.\n"
"\n"
"  *IMPORTANT NOTE:* This utility is still in its initial development stage\n"
"  so that command line options may change in the future and client and\n"
"  server built from different source snapshots are not guaranteed to work\n"
"  together correctly.\n"
"\n"
"Options:\n"
"  -h,--help\n"
"      Display this help text.\n"
"  -H,--host <host>\n"
"      Server to run test against (hostname, IPv4 or IPv6 address).\n"
"  -i,--iterate <num>[,<num>]\n"
"      Number of test iterations (default 1). If two values are provided,\n"
"      they are minimum and maximum number of iterations to achieve the\n"
"      confidence interval width (see -I option).\n"
"  -I,--confidence <num>[,<float>]\n"
"      Confidence level for displayed confidence intervals (default 95%).\n"
"      Optional second argument is confidence interval width (default 10%).\n"
"  -l,--seconds <num>\n"
"      Length of one iteration in seconds.\n"
"  -m,--msg-size <size>\n"
"      Message length in bytes (default depends on test).\n"
"  -M,--threads <num>\n"
"      Number of threads (parallel connections) to open (default 1).\n"
"  -p,--port <port>\n"
"      Server port to connect to (default 12543).\n"
"  -s,--rcvbuf-size <size>\n"
"      Receive buffer size (SO_RCVBUF socket option).\n"
"  -S,--sndbuf-size <size>\n"
"      Send buffer size (SO_SNDBUF socket option).\n"
"  -t,--test\n"
"      Test mode (see below, default TCP_STREAM).\n"
"  -n,--tcp-nodelay\n"
"      Set TCP_NODELAY socket option on test connections.\n"
"  -v,--verbose { result | iter | thread | all | raw }\n"
"      Output verbosity level (see below).\n"
"  -v,--verbose <num>\n"
"      Numeric mask of output to display (see below).\n"
"  --binary\n"
"      Power of 2 multiples for human readable output (default power of 10)\n"
"  --exact  \n"
"      Show unsimplified (exact) values in results (default human readable)\n"
"\n"
"  Option arguments shown as <size> above accept a numeric value, optionally\n"
"  followed by a suffix k/m/g/t/K/M/G/T. Lower case variants mean powers of\n"
"  ten (e.g. \"4k\" is 4000), upper case variants mean powers of two (e.g.\n"
"  \"4K\" is 4096).\n"
"\n"
"Test types:\n"
"  TCP_STREAM  client sends data to server as fast as possible, no replies\n"
"              default message size is 1MB\n"
"  TCP_RR      client sends one message, server replies with one message, ...\n"
"              default message size is 1B\n"
"\n"
"Verbosity mask bits:\n"
"  1   Overall result (one line, average over all iterations).\n"
"  2   One line summary of each iteration.\n"
"  4   Per thread summary of each iteration (one line per thread).\n"
"  8   Raw counter values (Rx/Tx, messages/syscalls/bytes, client/server).\n"
"\n"
"Verbosity levels:\n"
"  result  Overall result only (1).\n"
"  iter    ... + iteration summary (3).\n"
"  thread  ... + per thread summary (7).\n"
"  all     ... + raw counter data (15).\n"
"  raw     Raw data only (8).\n"
"\n";

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
	unsigned long val, val2;
	const char *arg;
	double dval;
	int ret;
	int c;

	while ((c = getopt_long(argc, argv, opts, long_opts, NULL)) != -1) {
		switch(c) {
		case 'h':
			fputs(help_text, stdout);
			exit(0);
		case 'H':
			config->server_host = optarg;
			break;
		case 'i':
			ret = parse_ulong_range_delim("iterations", optarg,
						      &val, 1, MAX_ITERATIONS,
						      ',', &arg);
			if (ret < 0)
				return -EINVAL;
			if (!*arg) {
				config->min_iter = config->max_iter = val;
				break;
			}
			ret = parse_ulong_range("iterations", ++arg, &val2,
						1, MAX_ITERATIONS);
			if (ret < 0)
				return -EINVAL;
			config->min_iter = (val < val2) ? val : val2;
			config->max_iter = (val < val2) ? val2 : val;
			break;
		case 'I':
			ret = parse_ulong_range_delim("confidence", optarg,
						      &val, 0, 100, ',', &arg);
			if (ret < 0)
				return -EINVAL;
			ret = confid_level_input(val);
			if (ret < 0) {
				fprintf(stderr, "only confidence level 95 or 99 are supported\n");
				return -EINVAL;
			}
			config->confid_level = ret;
			if (!*arg)
				break;
			ret = parse_double_range("confidence", ++arg, &dval,
						 1e-6, 100.0);
			if (ret < 0)
				return -EINVAL;
			config->confid_target = dval;
			config->confid_target_set = true;
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
			fputs("\nUsage:", stdout);
			fputs(help_text, stdout);
			return -EINVAL;
		default:
			fprintf(stderr, "unknown option '-%c'\n", c);
			return -EINVAL;
		}
	}

	if (config->confid_target_set && (config->min_iter < 3)) {
		fputs("Use of confidence target requires at least 3 iterations (use -i option).\n",
		      stderr);
		return -EINVAL;
	}
	if (!config->confid_target_set &&
	    config->min_iter != config->max_iter) {
		config->confid_target = 10.0;
		config->confid_target_set = true;
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
			fprintf(stderr, "test mode %u not supported\n",
				config->test_mode);
			return -EINVAL;
		}
	}

	if (config->stats_mask == UINT_MAX) {
		if (config->max_iter == 1) {
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
