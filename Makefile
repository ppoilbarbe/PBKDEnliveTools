SHELL := /bin/bash

ENV_NAME := pbkdenlivetools
CONDA ?= conda

FILTERS := extendedges

KDENLIVE_BIN_DIR := $(HOME)/.local/bin

FILTER ?=

.PHONY: help env-create env-update build install uninstall test clean bump-major bump-minor $(FILTERS)

help:
	@echo "Environment:"
	@echo "  env-create   Create the '$(ENV_NAME)' conda environment from environment.yml"
	@echo "  env-update   Update the '$(ENV_NAME)' conda environment from environment.yml"
	@echo ""
	@echo "Filters (delegates to each filter's own Makefile: $(FILTERS)):"
	@echo "  build        Compile all filters"
	@echo "  install      Install all filters, their Kdenlive XML, and the launcher script"
	@echo "  uninstall    Remove all installed filters"
	@echo "  test         Run each filter's test target"
	@echo "  clean        Remove build artifacts"
	@echo "  bump-major   Bump one filter's major version: make bump-major FILTER=<name>"
	@echo "  bump-minor   Bump one filter's minor version: make bump-minor FILTER=<name>"

env-create:
	$(CONDA) env create -n $(ENV_NAME) -f environment.yml

env-update:
	$(CONDA) env update -n $(ENV_NAME) -f environment.yml --prune

build:
	@for f in $(FILTERS); do $(MAKE) -C $$f build; done

install:
	@for f in $(FILTERS); do $(MAKE) -C $$f install; done
	mkdir -p $(KDENLIVE_BIN_DIR)
	install -m 755 bin/kdenlive-with-filters $(KDENLIVE_BIN_DIR)/kdenlive-with-filters
	@echo "Installed launcher: $(KDENLIVE_BIN_DIR)/kdenlive-with-filters"
	@echo "If Kdenlive runs from an AppImage, launch it via kdenlive-with-filters"
	@echo "so it can see the user plugin directory (see README.md)."

uninstall:
	@for f in $(FILTERS); do $(MAKE) -C $$f uninstall; done
	rm -f $(KDENLIVE_BIN_DIR)/kdenlive-with-filters

test:
	@for f in $(FILTERS); do $(MAKE) -C $$f test; done

clean:
	@for f in $(FILTERS); do $(MAKE) -C $$f clean; done

bump-major:
	@if [ -z "$(FILTER)" ]; then echo "Usage: make bump-major FILTER=<filter-name>"; exit 1; fi
	$(MAKE) -C $(FILTER) bump-major

bump-minor:
	@if [ -z "$(FILTER)" ]; then echo "Usage: make bump-minor FILTER=<filter-name>"; exit 1; fi
	$(MAKE) -C $(FILTER) bump-minor
