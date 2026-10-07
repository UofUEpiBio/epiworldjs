# The native builds use clang and libc++, the standard library Emscripten
# ships: epiworld draws from std::*_distribution, whose algorithms differ
# between libc++ and libstdc++, so only then can native and WASM runs match.
# (make predefines CXX = g++, so ?= would not apply.)
ifeq ($(origin CXX),default)
CXX := clang++
endif
EMCC ?= emcc
NODE ?= node

COMMONFLAGS := -std=c++17 -ffp-contract=off -Ivendor/epiworld
CXXFLAGS := -O2 -Wall -Wextra -stdlib=libc++ $(COMMONFLAGS)
EMFLAGS := -O3 -fwasm-exceptions $(COMMONFLAGS)
EMBINDFLAGS := $(EMFLAGS) -lembind \
	-sMODULARIZE -sEXPORT_ES6 -sENVIRONMENT=web,worker,node \
	-sALLOW_MEMORY_GROWTH -sEXPORT_NAME=createEpiworldModule

EPIWORLD_REPO ?= https://github.com/UofUEpiBio/epiworld.git
EPIWORLD_REF ?= master

HEADERS := cpp/core.hpp cpp/registry.hpp $(shell find vendor/epiworld -name '*.hpp')

.PHONY: test test-native test-wasm golden wasm clean update-vendors

test: test-native test-wasm golden

test-native: build/smoke
	./build/smoke

test-wasm: dist/core.js
	$(NODE) --test test/*.test.js

# Native and WASM must print identical outputs for the same specs
golden: build/golden-native.txt build/golden-wasm.txt
	cmp build/golden-native.txt build/golden-wasm.txt

wasm: dist/core.js

build/smoke: cpp/smoke.cpp $(HEADERS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $< -o $@

build/golden: cpp/golden.cpp $(HEADERS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $< -o $@

build/golden.cjs: cpp/golden.cpp $(HEADERS)
	@mkdir -p $(@D)
	$(EMCC) $(EMFLAGS) -sENVIRONMENT=node -sALLOW_MEMORY_GROWTH $< -o $@

build/golden-native.txt: build/golden
	./build/golden > $@

build/golden-wasm.txt: build/golden.cjs
	$(NODE) build/golden.cjs > $@

dist/core.js: cpp/bindings.cpp $(HEADERS)
	@mkdir -p $(@D)
	$(EMCC) $(EMBINDFLAGS) $< -o $@

clean:
	rm -rf build dist

# Copies epiworld's headers at EPIWORLD_REF (a branch, tag, or commit; the
# repository can be a local path) and records the commit in vendor/VERSIONS.
update-vendors:
	rm -rf build/epiworld-src
	git init --quiet build/epiworld-src
	# A local path is made absolute, since git -C changes directory first
	git -C build/epiworld-src fetch --quiet --depth 1 \
		$(if $(wildcard $(EPIWORLD_REPO)),$(abspath $(EPIWORLD_REPO)),$(EPIWORLD_REPO)) $(EPIWORLD_REF)
	git -C build/epiworld-src checkout --quiet FETCH_HEAD
	rm -rf vendor/epiworld
	cp -R build/epiworld-src/include/epiworld vendor/epiworld
	printf '# Vendored header-only dependencies, written by `make update-vendors`.\nepiworld %s %s\n' \
		$(EPIWORLD_REPO) $$(git -C build/epiworld-src rev-parse HEAD) > vendor/VERSIONS
