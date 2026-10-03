CXX ?= g++
CXXFLAGS := -O2 -std=c++17 -Wall -Wextra -pedantic -Ivendor/epiworld -Ivendor/measles
EMCC ?= emcc
EMFLAGS := -O3 -std=c++17 -Ivendor/epiworld -Ivendor/measles -lembind -sMODULARIZE -sEXPORT_ES6 -sENVIRONMENT=web,worker,node -sALLOW_MEMORY_GROWTH -sEXPORT_NAME=createEpiworldModule

.PHONY: test wasm clean update-vendors

test: build/smoke
	./build/smoke

build/smoke: cpp/smoke.cpp cpp/core.hpp | build
	$(CXX) $(CXXFLAGS) $< -o $@

build:
	mkdir -p $@

wasm: | dist
	$(EMCC) $(EMFLAGS) cpp/bindings.cpp -o dist/core.js

dist:
	mkdir -p $@

clean:
	rm -rf build dist

# Keep this command explicit: review vendor/VERSIONS after updating a source.
update-vendors:
	@echo "Fetch the commits recorded in vendor/VERSIONS and copy their public headers."

