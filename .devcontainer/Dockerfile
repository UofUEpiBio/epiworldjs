FROM emscripten/emsdk:3.1.74

# The native builds (smoke and golden tests) use clang with libc++, the same
# standard library as Emscripten, so that native and WASM runs can match
RUN apt-get update \
    && apt-get install -y --no-install-recommends clang libc++-dev libc++abi-dev make \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /workspaces/epiworldjs
