CC ?= cc
CFLAGS ?= -O2 -fPIC

SQLITE_VERSION_NUM ?= 3460100
SQLITE_YEAR ?= 2024
SQLITE_TAR := sqlite-autoconf-$(SQLITE_VERSION_NUM).tar.gz

ROOT := $(CURDIR)
SRC_DIR := $(ROOT)/src
LIB_DIR := $(ROOT)/lib
PKG := $(ROOT)/sqlite-autoconf.tar.gz

UNAME_S := $(shell uname -s)

# macOS universal binary (arm64 + x86_64). Disable with: make MACOS_UNIVERSAL=0
MACOS_UNIVERSAL ?=
MACOS_ARCHS     :=

ifeq ($(UNAME_S),Darwin)
	SHARED_FLAG = -dynamiclib
	ifeq ($(MACOS_UNIVERSAL),)
		MACOS_UNIVERSAL := 1
	endif
	ifeq ($(MACOS_UNIVERSAL),1)
		MACOS_ARCHS := -arch arm64 -arch x86_64
	endif
else
	SHARED_FLAG = -shared
endif

SQLITE_CFLAGS := $(CFLAGS) $(MACOS_ARCHS) \
	-DSQLITE_THREADSAFE=1 -DSQLITE_ENABLE_COLUMN_METADATA

SQLITE_SO := $(LIB_DIR)/sqlite3.so

.PHONY: all download build clean show-arch

all: build

download:
	@mkdir -p "$(SRC_DIR)" "$(LIB_DIR)"
	@set -e; \
	if [ ! -f "$(PKG)" ]; then \
		curl -L -o "$(PKG)" "https://www.sqlite.org/$(SQLITE_YEAR)/$(SQLITE_TAR)"; \
		rm -rf "$(SRC_DIR)"/*; \
		tar -xzf "$(PKG)" --strip-components=1 -C "$(SRC_DIR)"; \
	else \
		echo "Sqlite already exist."; \
	fi

build: $(SQLITE_SO)

$(SQLITE_SO): $(SRC_DIR)/sqlite3.c
	@mkdir -p "$(LIB_DIR)"
	$(CC) $(SQLITE_CFLAGS) -c "$(SRC_DIR)/sqlite3.c" -o "$(SRC_DIR)/sqlite3.o"
	$(CC) $(MACOS_ARCHS) $(SHARED_FLAG) -o "$@" "$(SRC_DIR)/sqlite3.o"
	@echo "built: $@"
ifeq ($(UNAME_S),Darwin)
ifeq ($(MACOS_UNIVERSAL),1)
	@lipo -info "$@" 2>/dev/null || true
endif
endif

show-arch: $(SQLITE_SO)
	@lipo -info "$(SQLITE_SO)" 2>/dev/null || file "$(SQLITE_SO)"

clean:
	rm -f "$(SRC_DIR)/sqlite3.o" "$(SQLITE_SO)"
