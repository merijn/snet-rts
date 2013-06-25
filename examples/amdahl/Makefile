.PHONY: clean all

.PRECIOUS: lib/libamdahl.a

TARGETS = $(basename $(wildcard *.snet))

# source config options: LPEL_INCLUDES, LIBLPEL_LA
include $(SNET_MISC)/snet-config.mkf

# sac2c compiles sac sources to binary modules
SAC2C     = sac2c
S2CFLAGS  = -v1 -O0 -mt -minmtsize 0 -ccflag"$(LPEL_INCLUDES)" -target lpel -nophm -L$(SNET_LIBS)

# sac4c generates c wrapper code for external calls of sac modules
SAC4C     = sac4c
S4CFLAGS  = -v1 -O0 -mt -ccflag"$(LPEL_INCLUDES) $(LIBLPEL_LA)" -target lpel -incdir include -libdir lib -Llib -L$(SNET_LIBS) -nophm -o amdahl
S4CINCS   = `$(SAC4C) $(S4CFLAGS) -ccflags amdahl | sed 's/-pthread//'`
S4CLIBS   = `$(SAC4C) $(S4CFLAGS) -ldflags amdahl | sed 's/-pthread//'`

# snet compiler flags
SNETCFLAGS  = -DSACTYPE_SNet_SNet=20 $(S4CINCS) $(S4CLIBS) -v1 -threading lpel

all: $(TARGETS)

%: %.snet lib/libamdahl.a
	rm -f *.o *.lo
	snetc $(SNETCFLAGS) -Llib -o $@ $<

lib/lib%.a: %.sac
	mkdir -p lib include
	$(SAC2C) $(S2CFLAGS) $(LIBDIRS) $*.sac -o lib
	$(SAC4C) $(S4CFLAGS) $(LIBDIRS) $* -o $*


clean:
	rm -rf $(TARGETS) $(foreach file, $(TARGETS), $(file).c) $(foreach file, $(TARGETS), $(file).h) include lib *.o *.lo mon_*.log *.map
