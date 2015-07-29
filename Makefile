ifneq ($(V),1)
		Q := @
endif

INCDIR		=
CCFLAGS		= -g -O2 $(INCDIR)
CXXFLAGS	= $(CCFLAGS)
LDFLAGS		=
LIBS		= -lm
CC			= gcc $(CCFLAGS)
LD			= gcc $(LDFLAGS)

BINS		= fitsread servecmd servestream ptee hpsdrrecv x_fir_dec sdr-widget

all: $(BINS)

clean:
	$(Q) rm -f *.o $(BINS)
	@printf "  CLEAN\n";

x_fir_dec: x_fir_dec.c
	@printf "  CC  $(subst $(shell pwd)/,,$(@))\n";
	$(Q) $(CC) -o$@ $< $(LIBS) -lvolk

sdr-widget: sdr-widget.c
	@printf "  CC  $(subst $(shell pwd)/,,$(@))\n";
	$(Q) $(CC) -o$@ $< $(LIBS) -lusb-1.0

fitsread: fitsread.c
	@printf "  CC  $(subst $(shell pwd)/,,$(@))\n";
	$(Q) $(CC) -o$@ $< $(LIBS) -lcfitsio

%: %.c
	@printf "  CC  $(subst $(shell pwd)/,,$(@))\n";
	$(Q) $(CC) -o$@ $< $(LIBS)
