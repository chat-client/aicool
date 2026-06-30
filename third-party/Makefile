.PHONY = all acl-lib sqlite-lib

OSNAME      := $(shell uname -s)
ACL_FLAG    :=
SQLITE_FLAG :=

ifeq ($(findstring Darwin, $(OSNAME)), Darwin)
	ACL_FLAG    := ENV_CC="clang -arch arm64 -arch x86_64" ENV_CPP="clang++ -arch arm64 -arch x86_64"
	SQLITE_FLAG := MACOS_UNIVERSAL=1
endif

all: acl-lib sqlite-lib

acl-lib:
	$(MAKE) -C acl all_lib $(ACL_FLAG)

sqlite-lib:
	$(MAKE) -C sqlite $(SQLITE_FLAG)

clean:
	$(MAKE) -C acl clean
	$(MAKE) -C sqlite clean
