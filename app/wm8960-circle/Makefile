#
# Makefile
#

include ../../Config.mk

CIRCLEHOME = ../..

# Set for ARM Circle (correct architecture name)
NEWLIB_ARCH ?= arm-none-circle
NEWLIBDIR = ../../../../install/$(NEWLIB_ARCH)

# Your objects (including the Faust-generated files)
OBJS = $(patsubst %.c, %.o, $(wildcard *.c)) $(patsubst %.cpp, %.o, $(wildcard *.cpp))

include $(CIRCLEHOME)/Rules.mk

# Critical circle-stdlib includes and flags
CFLAGS += -I "$(NEWLIBDIR)/include" -I $(STDDEF_INCPATH) -I ../../include

# Add sound library to the libs (you need this for audio)
LIBS := $(NEWLIBDIR)/lib/libm.a $(NEWLIBDIR)/lib/libc.a $(NEWLIBDIR)/lib/libcirclenewlib.a \
    $(CIRCLEHOME)/lib/sound/libsound.a \
    $(CIRCLEHOME)/addon/SDCard/libsdcard.a \
    $(CIRCLEHOME)/lib/usb/libusb.a \
    $(CIRCLEHOME)/lib/input/libinput.a \
    $(CIRCLEHOME)/addon/fatfs/libfatfs.a \
    $(CIRCLEHOME)/lib/fs/libfs.a \
    $(CIRCLEHOME)/lib/net/libnet.a \
    $(CIRCLEHOME)/lib/sched/libsched.a \
    $(CIRCLEHOME)/lib/libcircle.a

-include $(DEPS)