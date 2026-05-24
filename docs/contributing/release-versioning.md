# Release Versioning

Aevox versions are derived from the active branch. Tags identify published release points, but the
release branch remains the support line for patch releases.

## Version Formats

| Location | Format | Example | Meaning |
|---|---|---|---|
| `main` | `X.Y.0-alpha.<count>` | `0.3.0-alpha.0` | Development toward the next minor release |
| Release branch | `release/X.Y.Z` | `release/0.3.0` | Stabilization branch for one release line |
| Release branch builds | `X.Y.Z-beta.<count>` | `0.3.0-beta.4` | Pre-release stabilization builds |
| Stable release | `X.Y.Z` | `0.3.0` | Published stable release |
| Patch release | `X.Y.N` | `0.3.1` | Bug-fix release from the same release line |

The current `main` baseline is `0.3.0-alpha.0`. After `release/0.3.0` is created and stable
`0.3.0` is published, `main` moves to `0.4.0-alpha.0`.

The `ci-tests` pipeline derives the `main` suffix automatically on every push by counting commits
at the checked-out `HEAD` and passing `AEVOX_VERSION_PRERELEASE=alpha.<count>` to CMake.

## Release Branches

Release branches use the exact format `release/X.Y.Z`. For example:

```bash
git switch main
git pull --ff-only
git switch -c release/0.3.0
```

Commits on the release branch produce beta versions such as `0.3.0-beta.1` and
`0.3.0-beta.2`. The release workflow validates the install tree and external consumer build before
any stable tag is published.

The release pipeline derives the beta suffix automatically on every `release/X.Y.Z` branch push by
counting commits since the release branch diverged from `origin/main` and passing
`AEVOX_VERSION_PRERELEASE=beta.<count>` to CMake.

## Stable Releases

Stable releases are cut from the release branch:

```bash
git switch release/0.3.0
git tag v0.3.0
git push origin release/0.3.0 v0.3.0
```

The tag publishes artifacts, but the branch remains the maintained release line.

## Patch Releases

Patch releases stay on the same release line:

```bash
git switch release/0.3.0
git switch -c hotfix/0.3.1-router-fix
# apply fix, open PR, merge back to release/0.3.0
git tag v0.3.1
git push origin release/0.3.0 v0.3.1
```

Repeat the same flow for `0.3.2`, `0.3.3`, and later patches. Carry every applicable fix back to
`main`.

## Installation Contract

Release artifacts must include headers, the compiled library, and CMake package files. Consumers use:

```cmake
find_package(aevox REQUIRED)
target_link_libraries(my_server PRIVATE aevox::aevox_core)
```

Linux and Windows installation are validated in CI by building a minimal external consumer against
the installed package.
