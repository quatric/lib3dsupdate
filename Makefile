ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to>devkitARM")
endif

PREFIX  := $(DEVKITARM)/bin/arm-none-eabi-
CC      := $(PREFIX)gcc
AR      := $(PREFIX)ar
NM      := $(PREFIX)nm
OBJCOPY := $(PREFIX)objcopy
CTRULIB ?= $(DEVKITPRO)/libctru

MBEDTLS := vendor/mbedtls
MINIZ   := vendor/miniz
# Inflate-only miniz with stdio; zlib-style names off so apps that link zlib are unaffected.
MZFLAGS := -DMINIZ_NO_TIME -DMINIZ_NO_DEFLATE_APIS -DMINIZ_NO_ZLIB_APIS \
           -DMINIZ_NO_ZLIB_COMPATIBLE_NAMES
ARCH    := -D__3DS__ -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft -mword-relocations \
           -ffunction-sections -fdata-sections
CFLAGS  := -O2 -Wall -Wextra $(ARCH) -Iinclude -Iconfig -I$(MBEDTLS)/include -I$(MINIZ) -I$(CTRULIB)/include $(MZFLAGS) \
           -DMBEDTLS_CONFIG_FILE=\"mbedtls_3dsupdate.h\"
TLSFLAGS := -Os $(ARCH) -Iconfig -I$(MBEDTLS)/include -I$(MBEDTLS)/library -I$(CTRULIB)/include \
           -DMBEDTLS_CONFIG_FILE=\"mbedtls_3dsupdate.h\"

SRCS     := $(wildcard source/*.c)
OBJS     := $(patsubst source/%.c,build/%.o,$(SRCS))
TLS_SRCS := $(wildcard $(MBEDTLS)/library/*.c)
TLS_OBJS := $(patsubst $(MBEDTLS)/library/%.c,build/mbedtls/%.o,$(TLS_SRCS))
MZ_OBJS  := build/miniz.o
LIB      := lib/lib3dsupdate.a

all: $(LIB)

# mbedTLS and miniz are linked into the archive under a private prefix
# (mbedtls_* -> ctrupdate_mbedtls_*, mz_* -> ctrupdate_mz_*), so apps that
# bring their own copies, built with a different configuration, link against
# both without clashes.
$(LIB): $(OBJS) $(TLS_OBJS) $(MZ_OBJS)
	@mkdir -p lib build/renamed
	@$(NM) $^ | awk '$$NF ~ /^(mbedtls_|psa_|tf_psa_|mz_|tdefl_|tinfl_|miniz_)/ { print $$NF " ctrupdate_" $$NF }' | sort -u > build/renamed.syms
	@for o in $^; do $(OBJCOPY) --redefine-syms=build/renamed.syms $$o build/renamed/$$(echo $$o | tr / _); done
	@rm -f $@
	$(AR) rcs $@ build/renamed/*.o

build/%.o: source/%.c include/3dsupdate.h source/3dsupdate_internal.h source/3dsupdate_certs.h config/mbedtls_3dsupdate.h
	@mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

build/mbedtls/%.o: $(MBEDTLS)/library/%.c config/mbedtls_3dsupdate.h
	@mkdir -p build/mbedtls
	@$(CC) $(TLSFLAGS) -c $< -o $@

build/miniz.o: $(MINIZ)/miniz.c $(MINIZ)/miniz.h
	@mkdir -p build
	@$(CC) -O2 $(ARCH) $(MZFLAGS) -I$(MINIZ) -I$(CTRULIB)/include -Wno-pragmas -w -c $< -o $@

install: $(LIB)
	@mkdir -p $(DESTDIR)$(DEVKITPRO)/portlibs/3ds/lib $(DESTDIR)$(DEVKITPRO)/portlibs/3ds/include
	cp $(LIB) $(DESTDIR)$(DEVKITPRO)/portlibs/3ds/lib/
	cp include/3dsupdate.h $(DESTDIR)$(DEVKITPRO)/portlibs/3ds/include/

clean:
	rm -rf build lib

.PHONY: all install clean
