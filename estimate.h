#ifndef __NPERF_ESTIMATE_H
#define __NPERF_ESTIMATE_H

#include <errno.h>

enum confid_level {
	CONFID_LEVEL_95,
	CONFID_LEVEL_99,

	__CONFID_LEVEL_CNT
};

static inline int confid_level_input(unsigned int level)
{
	switch(level) {
	case 95:
		return CONFID_LEVEL_95;
	case 99:
		return CONFID_LEVEL_99;
	default:
		return -EINVAL;
	}
}

static inline unsigned int confid_level_output(enum confid_level level)
{
	switch(level) {
	case CONFID_LEVEL_95:
		return 95;
	case CONFID_LEVEL_99:
		return 99;
	default:
		return 0;
	}
}

double confid_interval(double sum, double sum_sqr, unsigned int n,
		       enum confid_level level);

#endif /* __NPERF_ESTIMATE_H */
