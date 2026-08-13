SHELL := /bin/bash

PIXI := $(HOME)/.pixi/bin/pixi

FILTERS := extendedges

KDENLIVE_BIN_DIR := $(HOME)/.local/bin

FILTER ?=

.PHONY: help bootstrap build install uninstall test clean hooks bump-major bump-minor $(FILTERS)

help:
	@echo "Environment:"
	@echo "  bootstrap    Install pixi (if missing) and sync the pixi environment"
	@echo ""
	@echo "Filters (delegates to each filter's own Makefile: $(FILTERS)):"
	@echo "  build        Compile all filters"
	@echo "  install      Install all filters, their Kdenlive XML, and the launcher script"
	@echo "  uninstall    Remove all installed filters"
	@echo "  test         Run each filter's test target"
	@echo "  clean        Remove build artifacts"
	@echo "  hooks        Run all pre-commit hooks"
	@echo "  bump-major   Bump one filter's major version: make bump-major FILTER=<name>"
	@echo "  bump-minor   Bump one filter's minor version: make bump-minor FILTER=<name>"

$(PIXI):
	@printf "pixi not found, installing...\n"
	curl -fsSL https://pixi.sh/install.sh | sh

bootstrap: $(PIXI) ## Install pixi if absent, then sync the environment
	$(PIXI) install

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

hooks:
	pre-commit run --all-files

clean:
	@for f in $(FILTERS); do $(MAKE) -C $$f clean; done

bump-major:
	@if [ -z "$(FILTER)" ]; then echo "Usage: make bump-major FILTER=<filter-name>"; exit 1; fi
	$(MAKE) -C $(FILTER) bump-major

bump-minor:
	@if [ -z "$(FILTER)" ]; then echo "Usage: make bump-minor FILTER=<filter-name>"; exit 1; fi
	$(MAKE) -C $(FILTER) bump-minor
