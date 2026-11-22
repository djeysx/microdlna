#!/bin/sh

tab=$(printf '\t')
objs=$(ls -1 *.c | grep -v version | sed -e 's|\.c$|.o|' | tr '\n' ' ')
build_objs=$(echo "$objs" | sed -e 's|\([a-zA-Z0-9_]*\.o\)|\$(BUILD_DIR)/\1|g')

exec 1> Makefile

cat <<MAKEFILE
# Build type: dev (default) | release
#   make release  -> -O3, LTO, -ffunction-sections -fdata-sections, --gc-sections, sans -g
#   make           -> -O2, -g (débogage)
BUILD ?= dev
BUILD_DIR := build

BASE_CFLAGS := --std=c11 -D_GNU_SOURCE -D_LARGEFILE_SOURCE -D_FILE_OFFSET_BITS=64
CFLAGS_DEV := \$(BASE_CFLAGS) -g -O2
CFLAGS_RELEASE := \$(BASE_CFLAGS) -O3 -flto -ffunction-sections -fdata-sections
LDFLAGS_DEV := -pthread
LDFLAGS_RELEASE := -pthread -flto -Wl,--gc-sections

ifeq (\$(BUILD),release)
  CFLAGS := \$(CFLAGS_RELEASE)
  LDFLAGS := \$(LDFLAGS_RELEASE)
else
  CFLAGS := \$(CFLAGS_DEV)
  LDFLAGS := \$(LDFLAGS_DEV)
endif

DST := $build_objs version.c

.PHONY: all
all: \$(BUILD_DIR)/microdlnad \$(BUILD_DIR)/microdlnad.8

MAKEFILE

# run the compiler to find dependancies
printf 'deps:\n\t@$(CC) -MM -MG -MF - -c *.c\n' | make -f - \
    | sed -e 's|version\.o|\$(BUILD_DIR)/microdlnad|' \
    | sed -e 's|version_info\.h|gen_version.sh|' \
    | sed -e 's|^\([a-zA-Z0-9_]*\.o\)|\$(BUILD_DIR)/\1|'

cat <<MAKEFILE

\$(BUILD_DIR):
${tab}mkdir -p \$(BUILD_DIR)

\$(BUILD_DIR)/%.o: %.c | \$(BUILD_DIR)
${tab}\$(CC) -c \$(CFLAGS) \$< -o \$@

\$(BUILD_DIR)/microdlnad: \$(DST) | \$(BUILD_DIR)
${tab}./gen_version.sh > \$(BUILD_DIR)/version_info.h
${tab}\$(CC) \$(CFLAGS) -I\$(BUILD_DIR) -o \$(BUILD_DIR)/microdlnad \$(DST) \$(LDFLAGS)

\$(BUILD_DIR)/microdlnad.8: microdlna.pod | \$(BUILD_DIR)
${tab}pod2man -c multimedia -r '' microdlna.pod > \$(BUILD_DIR)/microdlnad.8

.PHONY: clean
clean:
${tab}rm -rf \$(BUILD_DIR)

.PHONY: install
install:
${tab}mkdir -p /usr/local/bin
${tab}cp \$(BUILD_DIR)/microdlnad /usr/local/bin
${tab}strip /usr/local/bin/microdlnad
${tab}mkdir -p /usr/local/share/man/man8
${tab}cp \$(BUILD_DIR)/microdlnad.8 /usr/local/share/man/man8

MAKEFILE
