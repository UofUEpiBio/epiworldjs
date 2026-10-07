# Releasing epiworldjs

Releases go to npm as [`epiworldjs`](https://www.npmjs.com/package/epiworldjs);
jsDelivr and unpkg then serve them, e.g.
`https://cdn.jsdelivr.net/npm/epiworldjs/dist/epiworld-model.js`.

## Regular releases

1. Bump `version` in `package.json` in a PR, and merge it.
2. Tag the merge commit and push the tag:

   ```sh
   git tag v0.2.0 && git push origin v0.2.0
   ```

`.github/workflows/publish.yml` checks that the tag matches `package.json`,
builds the WebAssembly module, runs the native, Node and golden tests, and
publishes with npm's trusted publishing (no token; the package gets a
provenance statement). A version that is already on npm is skipped.

## One-time setup (done once, by the npm owner)

1. Publish the first version by hand. `dist/` is not in git, so build it
   first (`make wasm`, with Emscripten 3.1.74), then:

   ```sh
   npm login
   npm publish --provenance=false   # provenance only works from CI
   ```

2. On npmjs.com, open the package's **Settings → Trusted publishing** and add
   a GitHub Actions publisher: organization `UofUEpiBio`, repository
   `epiworldjs`, workflow `publish.yml`, environment `npm`.
3. Push the `v0.1.0` tag; the workflow runs the tests and skips the publish,
   since 0.1.0 is already on npm. Later tags publish on their own.
