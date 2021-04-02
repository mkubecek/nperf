SED = sed
CC = gcc

CFLAGS = -pthread -Wall -Wextra -g
LDFLAGS = -pthread

SOBJS = server/main.o server/control.o server/worker.o
COBJS = client/main.o client/worker.o client/cmdline.o stats.o estimate.o
UOBJS = common.o
OBJS = $(SOBJS) $(COBJS) $(UOBJS)

TARGETS = nperfd nperf

all: $(TARGETS)

%.o: %.c
	$(CC) -c -o $@ $(CFLAGS) $<

%.d: %.c
	@$(CC) -MM $< | $(SED) -e 's|^\([^:]*\)\.o *:|$(dir $@)\1.o $(dir $@)\1.d:|' > $@

include $(OBJS:.o=.d)

nperfd: $(SOBJS) $(UOBJS)
	$(CC) -o $@ $(LDFLAGS) $+

nperf: $(COBJS) $(UOBJS)
	$(CC) -o $@ $(LDFLAGS) $+ -lm

clean:
	rm -f $(TARGETS) *.o *.d */*.o */*.d
