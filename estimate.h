#ifndef __NPERF_ESTIMATE_H
#define __NPERF_ESTIMATE_H

enum confid_level {
	CONFID_LEVEL_95,
	CONFID_LEVEL_99,

	__CONFID_LEVEL_CNT
};

double confid_interval(double sum, double sum_sqr, unsigned int n,
		       enum confid_level level);

#endif /* __NPERF_ESTIMATE_H */
