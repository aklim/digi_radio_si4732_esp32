# Releasing a New Version

This project uses [Semantic Versioning](https://semver.org/) with git tags as
the single source of truth. Every `vX.Y.Z` tag pushed to `origin` triggers the
GitHub Actions workflow ([.github/workflows/release.yml](../.github/workflows/release.yml))
that builds the firmware and publishes a GitHub Release with the compiled
artifacts attached.

## Picking the version number

- **MAJOR** — incompatible hardware or wiring changes, or breaking changes to
  how the user interacts with the radio (e.g. controls reassigned).
- **MINOR** — new functionality in a backward-compatible way (e.g. adding AM
  band support alongside FM).
- **PATCH** — bug fixes and internal cleanups that don't change behaviour
  visible to the user.

## Checklist

1. Ensure `master` is green and contains everything for the release.
2. Edit [CHANGELOG.md](../CHANGELOG.md):
   - Move items from `## [Unreleased]` into a new `## [X.Y.Z] - YYYY-MM-DD`
     section.
   - Update the compare links at the bottom of the file.
3. Commit the changelog:
   ```bash
   git add CHANGELOG.md
   git commit -m "Release vX.Y.Z"
   git push origin master
   ```
4. Tag and push:
   ```bash
   git tag -a vX.Y.Z -m "Release vX.Y.Z"
   git push origin vX.Y.Z
   ```
5. Watch the workflow:
   ```bash
   gh run watch --exit-status
   # or open: https://github.com/aklim/digi_radio_si4732_esp32/actions
   ```
6. Once the workflow finishes, verify the
   [Releases page](https://github.com/aklim/digi_radio_si4732_esp32/releases)
   now shows `vX.Y.Z` with `digi_radio-vX.Y.Z-esp32dev.{bin,elf}`
   attached. The `shield_test` env is intentionally not built by the
   workflow.
7. Replace the auto-generated release notes with the matching CHANGELOG
   section — the GitHub default body is a PR-title stub and loses the
   "why" narrative that lives in the changelog:
   ```bash
   gh release edit vX.Y.Z --notes-file <(awk '/^## \[X\.Y\.Z\]/{p=1; print; next} /^## \[/{p=0} p' CHANGELOG.md)
   ```
   The awk form above is portable to BSD awk (macOS). An earlier revision
   used `awk '/start/,/end/' | head -n -1`, which silently produced an
   empty body on macOS because BSD `head` rejects `-n -1` (a GNU extension).

## If something goes wrong

- **Workflow failed before the Release was created:** fix the issue, delete
  the tag locally and on the remote, re-tag.
  ```bash
  git tag -d vX.Y.Z
  git push origin :refs/tags/vX.Y.Z
  ```
- **Release was published but with a broken binary:** delete the Release and
  the tag as above, then cut a new patch version. Never rewrite a published
  tag — downstream users may have already pulled it.

## Sanity-checking the binary locally

After a release, it's a good idea to confirm the version string made it into
the bundled artifact:

```bash
gh release download vX.Y.Z --pattern '*.elf' --dir /tmp
strings "/tmp/digi_radio-vX.Y.Z-esp32dev.elf" | grep 'FW='
# Expected:
#   FW=vX.Y.Z commit=<hash> built=YYYY-MM-DD
```
