.PHONY = all acl-lib sqlite-lib acl-strip-fat-archives

OSNAME       := $(shell uname -s)
ACL_FLAG     :=
SQLITE_FLAG  :=
ACL_PREBUILD :=

ifeq ($(findstring Darwin, $(OSNAME)), Darwin)
	ACL_FLAG     := ENV_CC="clang -arch arm64 -arch x86_64" ENV_CPP="clang++ -arch arm64 -arch x86_64"
	SQLITE_FLAG  := MACOS_UNIVERSAL=1
	# macOS ar(1) cannot update existing fat static libraries.
	ACL_PREBUILD := acl-strip-fat-archives
endif

all: acl-lib sqlite-lib

acl-strip-fat-archives:
	@find acl -path '*/lib/*.a' -delete 2>/dev/null || true

acl-lib: $(ACL_PREBUILD)
	$(MAKE) -C acl all_lib $(ACL_FLAG)

sqlite-lib:
	$(MAKE) -C sqlite $(SQLITE_FLAG)

clean:
	$(MAKE) -C acl clean
	$(MAKE) -C sqlite clean
