# Releasing epiworldjs

Releases go to npm as [`epiworldjs`](https://www.npmjs.com/package/epiworldjs);
jsDelivr and unpkg then serve them, e.g.
`https://cdn.jsdelivr.net/npm/epiworldjs/dist/epiworld-model.js`.

## Versions

As in epiworldR and epiworldpy, versions are epiworld's `X.Y.Z` followed by
`-N`, a counter for this package: `0.18.0-0` is the first epiworldjs release
with epiworld 0.18.0, `0.18.0-1` the next one with the same epiworld, and
`0.19.0-0` the first with epiworld 0.19.0. `make check-version` (run in CI)
requires `X.Y.Z` to match the vendored epiworld.

npm reads `X.Y.Z-N` as a pre-release, so the workflow publishes with
`--tag latest` (installs and CDNs then pick it up as usual). Caret ranges do
not cross epiworld versions: `^0.18.0-0` matches `0.18.0-1` but not
`0.19.0-0`.

## When to bump

[please-bump](https://github.com/gvegayon/please-bump) checks every PR
(`.github/please-bump.yaml`). A PR that changes what ships in the package
(`src/`, `cpp/`, `vendor/`, `Makefile`, `package.json`) must bump the version
once the version on `main` has been released (has a GitHub release); several
PRs can then share the new, unreleased version. `package.json` and
`package-lock.json` must agree (`npm install --package-lock-only` updates the
lockfile). A PR can waive the check with the `no-version-bump` label or
`[please-bump skip]` in its description.

## Regular releases

1. Bump `version` in `package.json` in a PR, and merge it.
2. Tag the merge commit and push the tag:

   ```sh
   git tag v0.18.0-1 && git push origin v0.18.0-1
   ```

`.github/workflows/publish.yml` checks that the tag matches `package.json`,
builds the WebAssembly module, runs the native, Node and golden tests, and
publishes with npm's trusted publishing (no token; the package gets a
provenance statement), and creates the GitHub release for the tag. A version
that is already on npm is not published again.

## One-time setup (done once, by the npm owner)

1. Publish the first version by hand. `dist/` is not in git, so build it
   first (`make wasm`, with Emscripten 3.1.74), then:

   ```sh
   npm login
   npm publish --provenance=false --tag latest   # provenance only works from CI
   ```

2. On npmjs.com, open the package's **Settings → Trusted publishing** and add
   a GitHub Actions publisher: organization `UofUEpiBio`, repository
   `epiworldjs`, workflow `publish.yml`, environment `npm`.
   Under **Allowed actions**, tick **Allow `npm publish`**: without it the
   publisher can only stage versions and the workflow fails with
   `403 OIDC permission denied`. The environment must also exist in the
   repository's settings and match exactly.
3. Push the `v0.18.0-0` tag; the workflow runs the tests, skips the publish
   (0.18.0-0 is already on npm) and creates the GitHub release. Later tags
   publish on their own.
