SHELL := /bin/bash

PLUGIN := extendedges
BUILD_DIR := build
SO := $(BUILD_DIR)/$(PLUGIN).so

FREI0R_USER_DIR ?= $(HOME)/.local/lib/frei0r-1
KDENLIVE_EFFECTS_DIR ?= $(HOME)/.local/share/kdenlive/effects

XML_FILE := frei0r_$(PLUGIN).xml

CC ?= gcc
CFLAGS := -O2 -Wall -Wextra -fPIC -std=c11
LDFLAGS := -shared

CURRENT_MAJOR := $(shell sed -n 's/.*major_version = \([0-9]*\);.*/\1/p' $(PLUGIN).c)
CURRENT_MINOR := $(shell sed -n 's/.*minor_version = \([0-9]*\);.*/\1/p' $(PLUGIN).c)

.PHONY: help build install uninstall test clean bump-major bump-minor

help:
	@echo "Targets for the '$(PLUGIN)' filter:"
	@echo "  build        Compile the frei0r plugin"
	@echo "  install      Install the plugin and its Kdenlive effect XML"
	@echo "  uninstall    Remove installed files"
	@echo "  test         Run the filter's standalone test suite"
	@echo "  clean        Remove build artifacts"
	@echo "  bump-major   Bump the major version (resets minor to 0)"
	@echo "  bump-minor   Bump the minor version"
	@echo "               Current version: $(CURRENT_MAJOR).$(CURRENT_MINOR)"

$(SO): $(PLUGIN).c frei0r.h
	mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -I. $(LDFLAGS) -o $@ $(PLUGIN).c -lm

build: $(SO)

install: build
	mkdir -p $(FREI0R_USER_DIR) $(KDENLIVE_EFFECTS_DIR)
	install -m 644 $(SO) $(FREI0R_USER_DIR)/$(PLUGIN).so
	install -m 644 frei0r_$(PLUGIN).xml $(KDENLIVE_EFFECTS_DIR)/frei0r_$(PLUGIN).xml
	@echo "Installed:"
	@echo "  $(FREI0R_USER_DIR)/$(PLUGIN).so"
	@echo "  $(KDENLIVE_EFFECTS_DIR)/frei0r_$(PLUGIN).xml"

uninstall:
	rm -f $(FREI0R_USER_DIR)/$(PLUGIN).so
	rm -f $(KDENLIVE_EFFECTS_DIR)/frei0r_$(PLUGIN).xml

TEST_BIN := $(BUILD_DIR)/test_$(PLUGIN)

test: $(TEST_BIN)
	./$(TEST_BIN)

$(TEST_BIN): test_$(PLUGIN).c $(PLUGIN).c frei0r.h
	mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -I. -o $@ test_$(PLUGIN).c $(PLUGIN).c -lm

clean:
	rm -rf $(BUILD_DIR)

bump-major:
	@new_major=$$(( $(CURRENT_MAJOR) + 1 )); \
	sed -i "s/major_version = [0-9]\+;/major_version = $$new_major;/" $(PLUGIN).c; \
	sed -i "s/minor_version = [0-9]\+;/minor_version = 0;/" $(PLUGIN).c; \
	sed -i "/^<effect /s/version=\"[0-9]\+\.[0-9]\+\"/version=\"$$new_major.0\"/" $(XML_FILE); \
	echo "Bumped $(PLUGIN) version: $(CURRENT_MAJOR).$(CURRENT_MINOR) -> $$new_major.0"

bump-minor:
	@new_minor=$$(( $(CURRENT_MINOR) + 1 )); \
	sed -i "s/minor_version = [0-9]\+;/minor_version = $$new_minor;/" $(PLUGIN).c; \
	sed -i "/^<effect /s/version=\"$(CURRENT_MAJOR)\.[0-9]\+\"/version=\"$(CURRENT_MAJOR).$$new_minor\"/" $(XML_FILE); \
	echo "Bumped $(PLUGIN) version: $(CURRENT_MAJOR).$(CURRENT_MINOR) -> $(CURRENT_MAJOR).$$new_minor"
