CC = cc
BUILD ?= build
CPPFLAGS += -D_POSIX_C_SOURCE=200809L -Iinclude
CFLAGS ?= -O2 -g
CFLAGS += -std=c11 -Wall -Wextra -Wpedantic -Wformat=2 -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Wvla -Werror
LDLIBS += -pthread
SOURCES = src/main.c src/config.c src/io.c src/random.c src/permit.c src/simulation.c src/gallery.c src/events.c
PRIVATE_HEADERS = $(wildcard src/*.h)
HEADERS = $(wildcard include/*.h) $(PRIVATE_HEADERS)
# inih r62, BSD-3-Clause: https://github.com/benhoyt/inih
# Upstream commit: 26254ee9de7681f8825433415443e7116ff24b98; license: third_party/inih/LICENSE.txt
INI_SOURCE = third_party/inih/ini.c
INI_FLAGS = -Ithird_party/inih -DINI_MAX_LINE=4098 -DINI_ALLOW_MULTILINE=0 -DINI_ALLOW_BOM=0 -DINI_STOP_ON_FIRST_ERROR=1 -DINI_CALL_HANDLER_ON_NEW_SECTION=1 -DINI_INLINE_COMMENT_PREFIXES='"\#"'
CPPFLAGS += $(INI_FLAGS)
ALL_SOURCES = $(SOURCES) $(INI_SOURCE)
SANITIZERS ?= address,undefined
TEST_ARGS ?=
DOXYGEN ?= doxygen
ANALYZER ?= clang
CLANG_FORMAT ?= clang-format-14
FORMAT_FILES = $(SOURCES) $(HEADERS)
GALLERY_HOST_OS := $(shell uname -s)
ifeq ($(GALLERY_HOST_OS),Darwin)
CPPFLAGS += -D_DARWIN_C_SOURCE
endif

.PHONY: all clean test docs docs-local docs-container sanitize tsan test-native \
        format format-check format-local format-check-local format-container format-check-container analyze headers-check
all: $(BUILD)/gallery $(BUILD)/gallery-cond

$(BUILD):
	mkdir -p $@

$(BUILD)/gallery: $(ALL_SOURCES) $(HEADERS) third_party/inih/ini.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(ALL_SOURCES) $(LDFLAGS) $(LDLIBS) -o $@

$(BUILD)/gallery-cond: $(ALL_SOURCES) $(HEADERS) third_party/inih/ini.h | $(BUILD)
	$(CC) $(CPPFLAGS) -DGALLERY_COND=1 $(CFLAGS) $(ALL_SOURCES) $(LDFLAGS) $(LDLIBS) -o $@

$(BUILD)/gallery-test: $(ALL_SOURCES) $(HEADERS) third_party/inih/ini.h | $(BUILD)
	$(CC) $(CPPFLAGS) -DGALLERY_TESTING=1 $(CFLAGS) $(ALL_SOURCES) $(LDFLAGS) $(LDLIBS) -o $@

$(BUILD)/gallery-cond-test: $(ALL_SOURCES) $(HEADERS) third_party/inih/ini.h | $(BUILD)
	$(CC) $(CPPFLAGS) -DGALLERY_TESTING=1 -DGALLERY_COND=1 $(CFLAGS) $(ALL_SOURCES) $(LDFLAGS) $(LDLIBS) -o $@

test: all headers-check $(BUILD)/gallery-test $(BUILD)/gallery-cond-test
	python3 tests/run_tests.py $(BUILD)/gallery $(BUILD)/gallery-cond --fault $(BUILD)/gallery-test --fault $(BUILD)/gallery-cond-test $(TEST_ARGS)

test-native: $(BUILD)/gallery-cond $(BUILD)/gallery-cond-test
	python3 tests/run_tests.py $(BUILD)/gallery-cond --fault $(BUILD)/gallery-cond-test

docs:
ifeq ($(GALLERY_HOST_OS),Darwin)
	$(MAKE) docs-container
else
	$(MAKE) docs-local
endif

docs-local:
	rm -rf docs/generated
	$(DOXYGEN) - < Doxyfile
	$(DOXYGEN) - < Doxyfile.cond

# macOS doxygen is buggy
docs-container:
	docker build -q -t ihw-hw2:local .
	docker run --rm --mount type=bind,src="$(CURDIR)",dst=/work ihw-hw2:local make docs-local

format format-check:
ifeq ($(GALLERY_HOST_OS),Darwin)
	$(MAKE) $@-container
else
	$(MAKE) $@-local
endif

format-local:
	$(CLANG_FORMAT) --style=file -i $(FORMAT_FILES)

format-check-local:
	$(CLANG_FORMAT) --style=file --dry-run --Werror $(FORMAT_FILES)

format-container format-check-container:
	docker build -q -t ihw-hw2:local .
	docker run --rm --mount type=bind,src="$(CURDIR)",dst=/work ihw-hw2:local make $(patsubst %-container,%-local,$@)

headers-check:
	@set -e; \
	for backend in 0 1; do \
	    for header in $(notdir $(HEADERS)); do \
	        printf '#include "%s"\nint main(void) { return 0; }\n' "$$header" | \
	            $(CC) $(CPPFLAGS) -Isrc -DGALLERY_COND=$$backend $(CFLAGS) -fsyntax-only -x c -; \
	    done; \
	done

analyze:
	$(ANALYZER) --analyze $(CPPFLAGS) $(CFLAGS) -Xanalyzer -analyzer-output=text $(SOURCES)
	$(ANALYZER) --analyze $(CPPFLAGS) -DGALLERY_COND=1 $(CFLAGS) -Xanalyzer -analyzer-output=text $(SOURCES)

sanitize:
	$(MAKE) BUILD=build-asan CFLAGS='-O1 -g -std=c11 -Wall -Wextra -Wpedantic -Wformat=2 -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Wvla -Werror -fno-omit-frame-pointer -fsanitize=$(SANITIZERS)' LDFLAGS='-fsanitize=$(SANITIZERS)' test

tsan:
	$(MAKE) BUILD=build-tsan CFLAGS='-O1 -g -std=c11 -Wall -Wextra -Wpedantic -Wformat=2 -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Wvla -Werror -fno-omit-frame-pointer -fsanitize=thread' LDFLAGS='-fsanitize=thread' TEST_ARGS='--skip-file-limit' test

clean:
	rm -rf $(BUILD)
