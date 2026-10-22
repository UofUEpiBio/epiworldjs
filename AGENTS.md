# Instructions for coding agents

epiworldjs compiles the C++ library [epiworld](https://github.com/UofUEpiBio/epiworld)
to WebAssembly. The headers are vendored in `vendor/` (`make update-vendors`,
which records the commit in `vendor/VERSIONS`); the bindings are in `cpp/`.
Build and test in the devcontainer (`.devcontainer/`): `make check-version test`.

## The `v1-layers` branch (epiworldjs 1.0.0)

`v1-layers` follows epiworld's `v1-layers` (contact layers, epiworld 1.0.0)
one merged phase at a time. There, epiworld's
[`contact-layers.md`](https://github.com/UofUEpiBio/epiworld/blob/v1-layers/contact-layers.md)
is the source of truth: its Section 11 (Runbook) says how to find where
things stand and what to do next. The bindings follow epiworld and what
epiworldR settled for the same phase; anything removed from the public API
throws an `Error` that names its replacement.
