# The native builds use clang and libc++, the standard library Emscripten
# ships: epiworld draws from std::*_distribution, whose algorithms differ
# between libc++ and libstdc++, so only then can native and WASM runs match.
# (make predefines CXX = g++, so ?= would not apply.)
ifeq ($(origin CXX),default)
CXX := clang++
endif
EMCC ?= emcc
NODE ?= node

COMMONFLAGS := -std=c++17 -ffp-contract=off -Ivendor/epiworld -Ivendor
CXXFLAGS := -O2 -Wall -Wextra -stdlib=libc++ $(COMMONFLAGS)
EMFLAGS := -O3 -fwasm-exceptions $(COMMONFLAGS)
EMBINDFLAGS := $(EMFLAGS) -lembind \
	-sMODULARIZE -sEXPORT_ES6 -sENVIRONMENT=web,worker,node \
	-sALLOW_MEMORY_GROWTH -sEXPORT_NAME=createEpiworldModule

EPIWORLD_REPO ?= https://github.com/UofUEpiBio/epiworld.git
EPIWORLD_REF ?= master
MEASLES_REPO ?= https://github.com/UofUEpiBio/measles.git
MEASLES_REF ?= main

HEADERS := cpp/core.hpp cpp/registry.hpp $(shell find vendor -name '*.hpp')

.PHONY: test test-native test-wasm golden wasm clean update-vendors update-epiworld update-measles

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

# $(call vendor,name,repo,ref,dir): copies `dir` of `repo` at `ref` (a branch,
# tag, or commit; the repository can be a local path) to vendor/<name> and
# records the commit in vendor/VERSIONS.
define vendor
	rm -rf build/$(1)-src
	git init --quiet build/$(1)-src
	# A local path is made absolute, since git -C changes directory first
	git -C build/$(1)-src fetch --quiet --depth 1 \
		$(if $(wildcard $(2)),$(abspath $(2)),$(2)) $(3)
	git -C build/$(1)-src checkout --quiet FETCH_HEAD
	rm -rf vendor/$(1)
	cp -R build/$(1)-src/$(4) vendor/$(1)
	cp build/$(1)-src/LICENSE.md vendor/$(1)/LICENSE.md
	grep -v '^$(1) ' vendor/VERSIONS > vendor/VERSIONS.tmp || true
	printf '%s %s %s\n' $(1) $(2) $$(git -C build/$(1)-src rev-parse HEAD) >> vendor/VERSIONS.tmp
	mv vendor/VERSIONS.tmp vendor/VERSIONS
endef

update-vendors: update-epiworld update-measles

update-epiworld:
	$(call vendor,epiworld,$(EPIWORLD_REPO),$(EPIWORLD_REF),include/epiworld)

update-measles:
	$(call vendor,measles,$(MEASLES_REPO),$(MEASLES_REF),inst/include/measles)
