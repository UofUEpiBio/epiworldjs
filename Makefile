CXX ?= g++
EMCC ?= emcc

CXXFLAGS := -O2 -std=c++17 -Wall -Wextra -Ivendor/epiworld
EMFLAGS := -O3 -std=c++17 -fwasm-exceptions -Ivendor/epiworld -lembind \
	-sMODULARIZE -sEXPORT_ES6 -sENVIRONMENT=web,worker,node \
	-sALLOW_MEMORY_GROWTH -sEXPORT_NAME=createEpiworldModule

EPIWORLD_REPO ?= https://github.com/UofUEpiBio/epiworld.git
EPIWORLD_REF ?= master

HEADERS := cpp/core.hpp $(shell find vendor/epiworld -name '*.hpp')

.PHONY: test test-native test-wasm wasm clean update-vendors

test: test-native test-wasm

test-native: build/smoke
	./build/smoke

test-wasm: dist/core.js
	node --test test/

wasm: dist/core.js

build/smoke: cpp/smoke.cpp $(HEADERS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $< -o $@

dist/core.js: cpp/bindings.cpp $(HEADERS)
	@mkdir -p $(@D)
	$(EMCC) $(EMFLAGS) $< -o $@

clean:
	rm -rf build dist

# Copies epiworld's headers at EPIWORLD_REF (a branch, tag, or commit; the
# repository can be a local path) and records the commit in vendor/VERSIONS.
update-vendors:
	rm -rf build/epiworld-src
	git init --quiet build/epiworld-src
	git -C build/epiworld-src fetch --quiet --depth 1 $(EPIWORLD_REPO) $(EPIWORLD_REF)
	git -C build/epiworld-src checkout --quiet FETCH_HEAD
	rm -rf vendor/epiworld
	cp -R build/epiworld-src/include/epiworld vendor/epiworld
	printf '# Vendored header-only dependencies, written by `make update-vendors`.\nepiworld %s %s\n' \
		$(EPIWORLD_REPO) $$(git -C build/epiworld-src rev-parse HEAD) > vendor/VERSIONS
