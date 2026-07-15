CC      = g++

CFLAGS  = -c -g -W -Wall -Wcast-qual -Wcast-align \
          -Wno-long-long -Wpointer-arith -Werror -Wshadow -O2 \
          -D_REENTRANT -D_POSIX_PTHREAD_SEMANTICS -D_USE_FAST_MACRO

PROG    = webcool
.DEFAULT_GOAL := all

# On macOS, build a universal binary (arm64 + x86_64) by default.
# Disable with: make MACOS_UNIVERSAL=0
MACOS_UNIVERSAL ?=
MACOS_ARCHS     :=
COREML_BUILD_TARGET :=
COREML_CLEAN_TARGET :=

###########################################################
# 系统类型检测
SYSLIB    = -ldl -lpthread -lz
UNIXNAME  = $(shell uname -sm)
OSTYPE    = $(shell uname -p)
RPATH     = linux64

ifeq ($(findstring g++, $(CC)), g++)
ifneq ($(findstring Darwin, $(UNIXNAME)), Darwin)
	GCC_VERSION := $(shell gcc --version 2>/dev/null | grep ^gcc | cut -d' ' -f3)
	GCC_MAJOR   := $(shell echo "$(GCC_VERSION)" | cut -d'.' -f1)
	GCC_MINOR   := $(shell echo "$(GCC_VERSION)" | cut -d'.' -f2)
	GCC_VER     := $(shell [ "$(GCC_MAJOR)" -gt 4 -o \( "$(GCC_MAJOR)" -eq 4 -a "$(GCC_MINOR)" -ge 7 \) ] 2>/dev/null && echo true)
	ifeq ($(GCC_VER), true)
		CFLAGS += -std=c++11 -DACL_USE_CPP11
	endif
endif
endif

OS_ENV := $(shell uname -a)
ifeq ($(findstring WSL, $(OS_ENV)), WSL)
	SYSLIB += -liconv
endif
ifeq ($(findstring ubuntu, $(OS_ENV)), ubuntu)
	SYSLIB += -liconv
endif
ifeq ($(findstring Alpine, $(shell uname -a)), Alpine)
	SYSLIB += -lucontext
endif

# Darwin (macOS)
ifeq ($(findstring Darwin, $(UNIXNAME)), Darwin)
	CFLAGS  += -DMACOSX -Wno-invalid-source-encoding -Wno-invalid-offsetof -std=c++11 -DACL_USE_CPP11
	SYSLIB  += -liconv -rdynamic
	RPATH    = macos
	ifeq ($(MACOS_UNIVERSAL),)
		MACOS_UNIVERSAL := 1
	endif
	ifeq ($(MACOS_UNIVERSAL),1)
		MACOS_ARCHS := -arch arm64 -arch x86_64
		CFLAGS += $(MACOS_ARCHS)
	endif
	SWIFTC ?= xcrun swiftc
	COREML_SWIFT_SRC := coreml/coreml_realesrgan.swift
	COREML_RUNNER := ../tools/mac/coreml-realesrgan
	COREML_SWIFT_FLAGS := -swift-version 5 -O -target arm64-apple-macosx13.0 \
		-framework CoreML -framework CoreImage -framework CoreVideo -framework AVFoundation
	COREML_BUILD_TARGET := $(COREML_RUNNER)
	RED_EYE_SWIFT_SRC := coreml/red_eye_correct.swift
	RED_EYE_RUNNER := ../tools/mac/red-eye-correct
	RED_EYE_SWIFT_FLAGS := -swift-version 5 -O \
		-framework Vision -framework CoreImage -framework ImageIO -framework CoreGraphics
	COREML_BUILD_TARGET += $(RED_EYE_RUNNER)
	COREML_CLEAN_TARGET := $(COREML_RUNNER) $(RED_EYE_RUNNER)
endif

# Linux
ifeq ($(findstring Linux, $(UNIXNAME)), Linux)
	CFLAGS  += -D_REENTRANT
	SYSLIB  += -lcrypt -lrt
	ifeq ($(findstring i686, $(OSTYPE)), i686)
		RPATH = linux32
	endif
	ifeq ($(findstring x86_64, $(OSTYPE)), x86_64)
		RPATH = linux64
	endif
	has_io_uring=$(HAS_IO_URING)
	ifeq ($(has_io_uring), yes)
		SYSLIB += -luring-ffi
	endif
endif

# FreeBSD
ifeq ($(findstring FreeBSD, $(UNIXNAME)), FreeBSD)
	CFLAGS  += -DFREEBSD -D_REENTRANT
	SYSLIB   = -lcrypt -lpthread -lz
	RPATH    = freebsd
endif

###########################################################
# ACL 根目录（相对本 Makefile 的位置）
ACL_PATH ?= ../third-party/acl
ACL_BUILD_TARGET ?= all_lib
#BUILD_ACL ?= 1

CFLAGS += -I. \
	-I$(ACL_PATH)/lib_acl/include \
	-I$(ACL_PATH)/lib_acl_cpp/include \
	-I$(ACL_PATH)/lib_protocol/include \
	-I$(ACL_PATH)/lib_fiber/c/include \
	-I$(ACL_PATH)/lib_fiber/cpp/include

LDFLAGS = \
	-L$(ACL_PATH)/lib_fiber/lib   -lfiber_cpp \
	-Wl,-rpath,$(ACL_PATH)/lib_fiber/lib \
	-L$(ACL_PATH)/lib_acl_cpp/lib -lacl_cpp \
	-Wl,-rpath,$(ACL_PATH)/lib_acl_cpp/lib \
	-L$(ACL_PATH)/lib_protocol/lib -lprotocol \
	-Wl,-rpath,$(ACL_PATH)/lib_protocol/lib \
	-L$(ACL_PATH)/lib_acl/lib     -lacl \
	-Wl,-rpath,$(ACL_PATH)/lib_acl/lib \
	-lfiber \
	$(SYSLIB)

ifeq ($(findstring Darwin, $(UNIXNAME)), Darwin)
	ifeq ($(MACOS_UNIVERSAL),1)
		LDFLAGS += $(MACOS_ARCHS)
	endif
endif

###########################################################
SRC = $(wildcard *.cpp)  $(wildcard common/*.cpp)  $(wildcard win32/*.cpp) \
	$(wildcard template/*.cpp) $(wildcard action/*.cpp) \
	$(wildcard action/admin/*.cpp) $(wildcard action/localdisk/*.cpp) \
	$(wildcard action/convert/*.cpp) $(wildcard action/file/*.cpp) \
	$(wildcard action/folder/*.cpp) \
	$(wildcard action/tag/*.cpp)
OBJ = $(patsubst %.cpp, %.o, $(SRC))

acl_libs:
	$(MAKE) -C $(ACL_PATH) $(ACL_BUILD_TARGET) $(ACL_MAKE_ENV)

mac-universal-acl:
	$(MAKE) -C $(ACL_PATH) clean
	$(MAKE) -C $(ACL_PATH) all_lib $(ACL_MAKE_ENV)

ifeq ($(findstring Darwin, $(UNIXNAME)), Darwin)
	ifeq ($(MACOS_UNIVERSAL),1)
		# ACL contains C sources; do not use g++ as ENV_CC on macOS.
		MACOS_C_FOR_ACL   := clang $(MACOS_ARCHS)
		MACOS_CXX_FOR_ACL := $(CC) $(MACOS_ARCHS)
		ACL_MAKE_ENV = ENV_CC="$(MACOS_C_FOR_ACL)" ENV_CPP="$(MACOS_CXX_FOR_ACL)"
	else
		ACL_MAKE_ENV =
	endif
else
	ACL_MAKE_ENV =
endif

ifeq ($(BUILD_ACL),1)
PROG_DEPS = acl_libs $(OBJ)
else
PROG_DEPS = $(OBJ)
endif

$(PROG): $(PROG_DEPS)
	$(CC) $(OBJ) $(LDFLAGS) -o $@
	@echo ""
	@echo "构建成功：$(PROG)"
	@if command -v lipo >/dev/null 2>&1 && [ -f "$(PROG)" ]; then \
		echo "架构：$$(lipo -archs $(PROG) 2>/dev/null || echo unknown)"; \
	fi
	@echo ""

%.o: %.cpp
	$(CC) $(CFLAGS) $< -o $@

ifeq ($(findstring Darwin, $(UNIXNAME)), Darwin)
$(COREML_RUNNER): $(COREML_SWIFT_SRC)
	@mkdir -p $(dir $@)
	env CLANG_MODULE_CACHE_PATH=/tmp/webcool-clang-module-cache \
		SWIFT_MODULECACHE_PATH=/tmp/webcool-swift-module-cache \
		$(SWIFTC) $(COREML_SWIFT_FLAGS) $< -o $@
	chmod 0755 $@
	@echo "构建成功：$(COREML_RUNNER) (arm64)"

coreml-runner: $(COREML_RUNNER)

$(RED_EYE_RUNNER): $(RED_EYE_SWIFT_SRC)
	@mkdir -p $(dir $@)
	env CLANG_MODULE_CACHE_PATH=/tmp/webcool-clang-module-cache \
		SWIFT_MODULECACHE_PATH=/tmp/webcool-swift-module-cache \
		$(SWIFTC) $(RED_EYE_SWIFT_FLAGS) -target arm64-apple-macosx13.0 $< -o $@.arm64
	env CLANG_MODULE_CACHE_PATH=/tmp/webcool-clang-module-cache \
		SWIFT_MODULECACHE_PATH=/tmp/webcool-swift-module-cache \
		$(SWIFTC) $(RED_EYE_SWIFT_FLAGS) -target x86_64-apple-macosx13.0 $< -o $@.x86_64
	lipo -create $@.arm64 $@.x86_64 -output $@
	rm -f $@.arm64 $@.x86_64
	chmod 0755 $@
	@echo "构建成功：$(RED_EYE_RUNNER) (arm64 x86_64)"

red-eye-runner: $(RED_EYE_RUNNER)
endif

.PHONY: all clean rebuild acl_libs mac-universal-acl coreml-runner red-eye-runner

all: $(COREML_BUILD_TARGET) $(PROG)

clean cl:
	rm -f $(PROG) $(OBJ) $(COREML_CLEAN_TARGET)

rebuild rb: clean all
