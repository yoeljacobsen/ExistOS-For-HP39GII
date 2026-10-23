DEFINES=
CC := gcc
CXX := g++
LD := g++

LOCAL_SYSROOT := $(HOME)/.local/sysroot
ifneq ($(wildcard $(LOCAL_SYSROOT)/usr/lib/x86_64-linux-gnu/pkgconfig),)
export PKG_CONFIG_PATH := $(LOCAL_SYSROOT)/usr/lib/x86_64-linux-gnu/pkgconfig:$(PKG_CONFIG_PATH)
EXTRA_LDFLAGS := -Wl,-rpath,$(LOCAL_SYSROOT)/usr/lib/x86_64-linux-gnu
endif

LIBUSB := $(shell pkg-config --cflags libusb-1.0)
CFLAGS := -O3 -g -std=c99 -Wall $(shell pkg-config --cflags libusb-1.0) $(DEFINES)
CXXFLAGS := -O3 -g -Wall $(shell pkg-config --cflags libcrypto++) $(DEFINES)
LDFLAGS := $(EXTRA_LDFLAGS) $(shell pkg-config --libs libusb-1.0) $(shell pkg-config --libs libcrypto++)
BINS := elftosb sbtoelf sbloader rsrctool elftosb1

all: $(BINS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

sbtoelf: sbtoelf.o crc.o crypto.o xorcrypt.o dbparser.o elf.o misc.o sb.o sb1.o
	$(LD) -o $@ $^ $(LDFLAGS)

elftosb: elftosb.o crc.o crypto.o elf.o dbparser.o misc.o sb.o
	$(LD) -o $@ $^ $(LDFLAGS)

elftosb1: elftosb1.o xorcrypt.o elf.o misc.o sb1.o
	$(LD) -o $@ $^ $(LDFLAGS)

sbloader: sbloader.o
	$(LD) -o $@ $^ $(LDFLAGS)

rsrctool: rsrctool.o rsrc.o misc.o
	$(LD) -o $@ $^ $(LDFLAGS)

clean:
	rm -fr *.o

veryclean:
	rm -rf $(BINS)
