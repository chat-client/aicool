.PHONY: all acl sqlite sqlite-download tools app clean rebuild webcool-regression

ACL_DIR := third-party/acl
SQLITE_DIR := third-party/sqlite
SRC_DIR := webcool
TOOLS_DIR := tools
WEBCOOL_BASE_URL ?= http://127.0.0.1:18093

all: app

tools:
	@set -e; \
	for platform in linux mac; do \
		archive="$(TOOLS_DIR)/$$platform/ffmpeg.tgz"; \
		ffmpeg="$(TOOLS_DIR)/$$platform/ffmpeg"; \
		if [ ! -f "$$ffmpeg" ] ]; then \
			if [ -f "$$archive" ]; then \
				echo "Extracting $$archive ..."; \
				tar -xzf "$$archive" -C "$(TOOLS_DIR)/$$platform"; \
			else \
				echo "Missing $$ffmpeg and $$archive"; \
				exit 1; \
			fi; \
		fi; \
	done

acl:
	$(MAKE) -C $(ACL_DIR) all_lib

sqlite:
	$(MAKE) -C $(SQLITE_DIR) build

sqlite-download:
	$(MAKE) -C $(SQLITE_DIR) download

app: tools acl sqlite
	$(MAKE) -C $(SRC_DIR) BUILD_ACL=0

clean:
	$(MAKE) -C $(SRC_DIR) clean
	$(MAKE) -C $(ACL_DIR) clean
	$(MAKE) -C $(SQLITE_DIR) clean

rebuild: clean all

webcool-regression:
	python3 webcool/tests/webcool_regression.py --base-url "$(WEBCOOL_BASE_URL)"
