APP := bank
TMBUILD ?= tinystm

include ../common/$(TMBUILD)/Makefile.common

STAMP_LIB = ../../../stamp/apps/lib

BUILDDIR = ../../$(TMBUILD)

TARGET = $(BUILDDIR)/$(APP)

CPU_MAX_FREQ=$(shell cat /sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq)

DEFINES += -DCPU_MAX_FREQ=$(CPU_MAX_FREQ) -DNUMBER_OF_TRANSACTIONS=3 

CFLAGS += -std=c++11 -g -w -I$(STAMP_LIB)

SRCS += bank.c \
	$(STAMP_LIB)/mt19937ar.c \
	$(STAMP_LIB)/random.c \
	$(STAMP_LIB)/thread.c
OBJS = $(patsubst %.c, $(BUILDDIR)/%.o,$(subst $(STAMP_LIB)/, , $(SRCS)))

ifeq ($(CACHE_ALIGN_POOL),1)
DEFINES += -DCACHE_ALIGN_POOL=1
endif

ifeq ($(NDEBUG),1)
DEFINES += -DNDEBUG=1 -D_GNU_SOURCE
endif

all: TM_BUILD_DIR $(TARGET)

TM_BUILD_DIR:
	@mkdir -p $(BUILDDIR)

$(BUILDDIR)/%.o:	$(STAMP_LIB)/%.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEFINES) -c -o $@ $<

$(BUILDDIR)/%.o:	%.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEFINES) -c -o $@ $<

$(TARGET): $(OBJS) $(TMLIB)
	$(LD) -o $@ $^ $(LDFLAGS)

clean:
	$(RM) $(TARGET) $(BUILDDIR)/*.o
