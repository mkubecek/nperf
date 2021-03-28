#ifndef __NPERF_CLIENT_CMDLINE_H
#define __NPERF_CLIENT_CMDLINE_H

struct client_config;

int parse_cmdline(int argc, char *argv[], struct client_config *config);

#endif /* __NPERF_CLIENT_CMDLINE_H */
