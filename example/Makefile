# Builds example.3dsx against the library in the folder above (make it first).
ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to>devkitARM")
endif

include $(DEVKITARM)/3ds_rules

TARGET   := example
DEPSDIR  := $(CURDIR)
LIBDIR   := $(CURDIR)/..
ARCH     := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft
CFLAGS   := -O2 -Wall -mword-relocations $(ARCH) -D__3DS__ -I$(LIBDIR)/include -I$(CTRULIB)/include
LDFLAGS  := -specs=3dsx.specs $(ARCH)
LIBS     := -L$(LIBDIR)/lib -L$(CTRULIB)/lib -l3dsupdate -lctru -lm

all: $(TARGET).3dsx

$(TARGET).3dsx: $(TARGET).elf

$(TARGET).elf: main.o $(LIBDIR)/lib/lib3dsupdate.a
	$(CC) $(LDFLAGS) main.o $(LIBS) -o $@

$(LIBDIR)/lib/lib3dsupdate.a:
	@$(MAKE) --no-print-directory -C $(LIBDIR)

clean:
	rm -f *.o *.d *.elf *.3dsx

.PHONY: all clean
