include commands.mk

OPTS    := -O3
CFLAGS  := -std=c99 $(OPTS) -fPIC -Wall
LDFLAGS :=

SRC  = $(wildcard *.c)
OBJ  = $(foreach obj, $(SRC:.c=.o), $(notdir $(obj)))
DEP  = $(SRC:.c=.d)

PREFIX  ?= /usr/local
BIN     ?= /bin

SAR_VERSION := $(shell cat VERSION)
CFLAGS += -DVERSION="\"$(SAR_VERSION)\""

ifndef DISABLE_DEBUG
CFLAGS += -ggdb
else
CFLAGS += -DNDEBUG=1
endif

commit = $(shell ./hash.sh)
ifneq ($(commit), UNKNOWN)
CFLAGS += -DCOMMIT="\"$(commit)\""
CFLAGS += -DPARTIAL_COMMIT="\"$(shell echo $(commit) | cut -c1-8)\""
endif

ifneq "$(wildcard config.h)" ""
CFLAGS += -DHAVE_CONFIG=1
endif

system = $(shell uname -o)
ifneq ($(system), FreeBSD)
CFLAGS += -D_BSD_SOURCE=1 -D_POSIX_C_SOURCE=200809L -D_LARGEFILE64_SOURCE=1
else
CFLAGS += -D_BSD_SOURCE=1
endif

CFLAGS += -DUSE_CRC32_C=1

# In FreeBSD systems, sometimes the correct cputype is not picked up.
# We check the log and enable it when it is available.
SSE42_SUPPORT=$(shell $(CC) -march=native -dM -E - < /dev/null | grep SSE4_2)
ifeq ($(SSE42_SUPPORT),)
  SSE42_SUPPORT=$(shell if [ -f /var/run/dmesg.boot ] ; then grep SSE4\.2 /var/run/dmesg.boot ; fi)
endif
ifneq ($(SSE42_SUPPORT),)
	CFLAGS += -msse4.2
endif 

.PHONY: all clean

all: sar

sar: $(OBJ)
	$(CC) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) -Wp,-MMD,$*.d -c $(CFLAGS) -o $@ $<

clean:
	$(RM) $(DEP)
	$(RM) $(OBJ)
	$(RM) $(CATALOGS)
	$(RM) sar

install:
	$(MKDIR) -p $(DESTDIR)/$(PREFIX)/$(BIN)
	$(INSTALL_PROGRAM) sar $(DESTDIR)/$(PREFIX)/$(BIN)

uninstall:
	$(RM) $(DESTDIR)/$(PREFIX)/sar

-include $(DEP)

