FROM emscripten/emsdk:3.1.74

# The native smoke test needs a host compiler next to Emscripten
RUN apt-get update \
    && apt-get install -y --no-install-recommends g++ make \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /workspaces/epiworldjs
