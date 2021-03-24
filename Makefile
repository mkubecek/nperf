SED = sed
CC = gcc-11

CFLAGS = -pthread -Wall -Wextra -ggdb
LDFLAGS = -pthread
#CFLAGS = -pthread -O2 -fomit-frame-pointer
#LDFLAGS = -pthread -s

SOBJS = server/main.o server/control.o server/worker.o
COBJS = client/main.o client/worker.o
UOBJS = common.o stats.o
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
	$(CC) -o $@ $(LDFLAGS) $+

clean:
	rm -f $(TARGETS) *.o *.d */*.o */*.d
