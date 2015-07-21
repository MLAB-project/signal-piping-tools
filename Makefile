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

BINS		= fitsread servecmd servestream ptee hpsdrrecv

all: $(BINS)

clean:
	$(Q) rm -f *.o $(BINS)
	@printf "  CLEAN\n";

fitsread: fitsread.c
	@printf "  CC  $(subst $(shell pwd)/,,$(@))\n";
	$(Q) $(CC) -o$@ $< $(LIBS) -lcfitsio

%: %.c
	@printf "  CC  $(subst $(shell pwd)/,,$(@))\n";
	$(Q) $(CC) -o$@ $< $(LIBS)
