#include <stdio.h>
#include <string.h>
#include <getopt.h>
#include <stdint.h>
#include <stdbool.h>
#include <limits.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

#include "../common.h"
#include "control.h"

const char *opts = "hp:";
const struct option long_opts[] = {
	{ .name = "help",				.val = 'h' },
	{ .name = "port",		.has_arg = 1,	.val = 'p' },
	{}
};

struct server_config {
	uint16_t	port;
};

static struct server_config server_config = {
	.port		= DEFAULT_PORT,
};

static const char *help_text = "\n"
"  nperfd [ options ]\n"
"\n"
"  Network performance benchmarking utility (server side). Listens on given\n"
"  TCP port and provides server for tests initiated by client (nperf).\n"
"  For each client connection, nperfd starts a new listener on an ephemeral\n"
"  port which is then used by client data connections (the benchmark).\n"
"\n"
"  *IMPORTANT NOTE:* This utility is still in its initial development stage\n"
"  so that command line options may change in the future and client and\n"
"  server built from different source snapshots are not guaranteed to work\n"
"  together correctly.\n"
"\n"
"Options:\n"
"  -h,--help\n"
"      Display this help text.\n"
"  -p,--port <port>\n"
"      Server port to listen on (default 12543).\n"
"\n";

static int parse_cmdline(int argc, char *argv[], struct server_config *config)
{
	unsigned long val;
	int ret;
	int c;

	while ((c = getopt_long(argc, argv, opts, long_opts, NULL)) != -1) {
		switch(c) {
                case 'h':
                        fputs(help_text, stdout);
                        exit(0);
		case 'p':
			ret = parse_ulong_range("port", optarg, &val,
						0, USHRT_MAX);
			if (ret < 0)
				return -EINVAL;
			config->port = val;
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

	return 0;
}

static int server_init(void)
{
	int ret;

	ret = ignore_signal(SIGPIPE);
	if (ret < 0)
		return ret;
	ret = ignore_signal(SIGCHLD);
	if (ret < 0)
		return ret;

	return 0;
}

static int setup_ctrl(const struct server_config *config)
{
	struct sockaddr_in6 addr = {
		.sin6_family	= AF_INET6,
		.sin6_port	= htons(config->port),
		.sin6_addr	= IN6ADDR_ANY_INIT,
	};
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

	ret = bind(sd, (struct sockaddr *)&addr, sizeof(addr));
	if (ret < 0) {
		ret = -errno;
		perror("bind");
		return ret;
	}
	ret = listen(sd, 16);
	if (ret < 0) {
		ret = -errno;
		perror("listen");
		return ret;
	}

	return sd;
}

static int server_loop(int sd)
{
	pid_t cpid;
	int csd;
	int ret;

	while (true) {
		csd = accept(sd, NULL, NULL);
		if (csd < 0) {
			ret = -errno;
			perror("accept");
			return ret;
		}
		cpid = fork();
		if (cpid < 0) {
			perror("fork");
			continue;
		}
		if (cpid == 0) { /* child */
			close(sd);
			ctrl_main(csd);
			exit(0);
		}

		close(csd);
	}

	return 0;
}

int main(int argc, char *argv[])
{
	int ret;
	int sd;

	ret = parse_cmdline(argc, argv, &server_config);
	if (ret < 0)
		return 1;

	printf("port: %hu\n", server_config.port);

	ret = server_init();
	if (ret < 0)
		return 2;
	sd = setup_ctrl(&server_config);
	if (sd < 0)
		return 2;

	ret = server_loop(sd);
	if (ret < 0)
		return 3;
	if (ret > 0) /* child */
		ret = ctrl_main(sd);

	close(sd);
	return ret ? 3 : 0;
}
