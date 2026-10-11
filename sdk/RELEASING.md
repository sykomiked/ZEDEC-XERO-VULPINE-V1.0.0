<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Releasing the ZXV SDKs

This is the checklist a person follows to release the SDKs. Automation only
**packages**: the [`sdk-release`](../.github/workflows/sdk-release.yml)
workflow builds every artifact and creates a **draft** GitHub release.
Everything that makes a release visible to the outside world is a manual
step that only the repository owner takes:

- publishing the draft release,
- pushing the Go module tag,
- uploading to any package registry (PyPI, Maven Central, NuGet, npm,
  crates.io, RubyGems, pub.dev).

The workflow holds no registry credentials and must never be given any.

Scripts and the release-notes template live in [`release/`](release/).

## 1. Before you start

- [ ] The commit you will release is on the default branch, and every SDK
      workflow is green on it: `SDKs` (`sdk.yml`), `SDK frameworks`,
      `SDK mobile`, `SDK embedded`, `SDK engines` and `SDK pillars`.
      The release workflow packages; it does not re-run the language test
      suites (it does run the libzxv C tests on each binary it ships).
- [ ] Choose the version `X.Y.Z` (or `X.Y.Z-rc.N`). Anything below 1.0.0
      or with a suffix is marked as a pre-release.
- [ ] If the ABI changed, the bump follows
      [the stability policy](docs/STABILITY.md#how-the-abi-grows): new
      functions need a new minor version.

## 2. Bump the versions

The language and mobile SDKs move in **lockstep** with libzxv: every file
below declares `X.Y.Z`.

| SDK | File | Field |
|---|---|---|
| libzxv | `sdk/libzxv/include/zxv_api.h` | `ZXV_ABI_VERSION_MAJOR`, `_MINOR`, `_PATCH` |
| Python | `sdk/python/pyproject.toml` | `[project] version` |
| Python | `sdk/python/src/zxv/__init__.py` | `__version__` |
| Java | `sdk/java/pom.xml` | `<version>` |
| .NET (Zxv.Native, Zxv.Sdk, Zxv.AspNetCore) | `sdk/dotnet/Directory.Build.props` | `<Version>` |
| TypeScript | `sdk/typescript/package.json` and `package-lock.json` | `version` (`npm version X.Y.Z --no-git-tag-version` updates both) |
| Rust | `sdk/rust/Cargo.toml` | `[workspace.package] version` |
| Rust | `sdk/rust/zxv/Cargo.toml` | `zxv-sys = { ..., version = "X.Y.Z" }` |
| Rust | `sdk/rust/Cargo.lock` | refresh with `cargo update -w` in `sdk/rust` |
| Ruby | `sdk/ruby/lib/zxv/version.rb` | `VERSION` (`X.Y.Z.pre.rc.N` or `X.Y.Z-rc.N` for a pre-release) |
| C++ | `sdk/cpp/CMakeLists.txt` | `project(zxv_cpp VERSION X.Y.Z ...)` |
| Kotlin (JVM) | `sdk/mobile/kotlin/zxv/build.gradle.kts` | `version` |
| Dart | `sdk/mobile/dart/pubspec.yaml` | `version` |

Dependencies on a lockstep package must accept `X.Y.Z`:

| Package | File | Field |
|---|---|---|
| Spring starter | `sdk/frameworks/spring-zxv/pom.xml` | `<zxv.version>` (exact) |
| NestJS kit | `sdk/frameworks/nest-zxv/package.json` | `peerDependencies["@zedec/zxv"]` (a range such as `^1.1.0`) |
| django-zxv | `sdk/frameworks/django-zxv/pyproject.toml` | `zxv-sdk>=...` in `dependencies` |

These are versioned on their own line; bump them when they change:

| Package | File(s) |
|---|---|
| Spring starter | `sdk/frameworks/spring-zxv/pom.xml` `<version>` and the `<parent><version>` in `zxv-spring-boot-starter/pom.xml` and `zxv-spring-boot-sample/pom.xml` |
| django-zxv | `sdk/frameworks/django-zxv/pyproject.toml` and `src/django_zxv/__init__.py` |
| NestJS kit | `sdk/frameworks/nest-zxv/package.json` and `package-lock.json` |
| Unity package | `sdk/engines/unity-zxv/com.zedec.zxv/package.json` |
| Plugin kits | the `*_VERSION_MAJOR/MINOR/PATCH` macros in `sdk/kits/*/include/*.h` |

Go, SwiftPM and Composer have no version field: their version is the git
tag. The Android AAR has no version set in `android/build.gradle.kts`; the
release file name carries it.

Then check, and fix until it passes:

```sh
python3 sdk/release/check_versions.py --version X.Y.Z
```

`--strict` also requires the independently versioned packages to match.

## 3. Record the ABI baseline

Every published release adds its baseline
([STABILITY.md](docs/STABILITY.md#before-1x-what-counts-as-released)):

```sh
make -C sdk/libzxv all
python3 sdk/libzxv/tools/abi_check.py snapshot sdk/libzxv/include/zxv_api.h \
    sdk/libzxv/abi/libzxv-X.Y.Z.json
for base in sdk/libzxv/abi/libzxv-*.json; do
  python3 sdk/libzxv/tools/abi_check.py check sdk/libzxv/include/zxv_api.h "$base" sdk/libzxv/build/libzxv.so
done
```

A pre-release `X.Y.Z-rc.N` uses the baseline of `X.Y.Z`; if the header
changes between release candidates, take the snapshot again.

## 4. Merge

- [ ] Open a pull request with the version bumps and the baseline, let CI
      pass, and merge it.

## 5. Run the release workflow

Either:

- **Actions → SDK release (draft) → Run workflow**, on the default branch,
  with `version` = `X.Y.Z`. The draft points at that commit; its tag
  `sdk-vX.Y.Z` is only created when the draft is published. Or:
- push a tag (owner only): `git tag -s sdk-vX.Y.Z <commit> && git push origin sdk-vX.Y.Z`.

The `preflight` job stops the run if the version is malformed, if
`zxv_api.h` disagrees, if any manifest disagrees (`check_versions.py`), if
`sdk/libzxv/abi/libzxv-X.Y.Z.json` is missing or differs from the header, or
if the ABI breaks any earlier baseline. The last job refuses to replace an
existing release or tag.

## 6. Review the draft

Open **Releases** and review the draft before anyone else sees it:

- [ ] Every expected file is attached (the table in the notes lists them).
- [ ] Download the assets and check them: `sha256sum -c SHA256SUMS`.
- [ ] `BUILDINFO.txt` in each `libzxv-X.Y.Z-<platform>.tar.gz` says the tests
      ran (the macOS x86_64 build is tested only when Rosetta 2 was there).
- [ ] Read `dart-pub-publish-dry-run.txt`.
- [ ] Read the notes, in particular "Honest limits" and "Platforms not
      tested". Edit the text if this release needs more.
- [ ] Spot-check the SBOM (`zxv-sdk-X.Y.Z.spdx.json`).

If anything is wrong, delete the draft (`gh release delete sdk-vX.Y.Z --yes`;
a draft has no tag yet), fix it on the branch, and run the workflow again.

## 7. Publish the draft (owner only)

In the web UI, **Edit → Publish release**, or:

```sh
gh release edit sdk-vX.Y.Z --draft=false
```

Then push the Go module tag (a module in a subdirectory needs a prefixed
tag; `proxy.golang.org` picks it up from there):

```sh
git fetch --tags
git tag sdk/go/zxv/vX.Y.Z sdk-vX.Y.Z^{}
git push origin sdk/go/zxv/vX.Y.Z
```

## 8. Registries (owner only, separately)

Nothing below is automated, on purpose. Each step uses the owner's own
account and credentials, from a clean checkout of the tag with the release
assets downloaded:

```sh
git checkout sdk-vX.Y.Z
gh release download sdk-vX.Y.Z -D dist && (cd dist && sha256sum -c SHA256SUMS)
```

Do a dry run or a staging upload first wherever the registry offers one.
For a pre-release, use the registry's pre-release channel (npm `--tag next`;
the others infer it from the version string).

**PyPI** (`zxv-sdk`, `django-zxv`). PyPI rejects plain `linux_*` wheel tags,
so repair the Linux wheels to `manylinux` first (macOS wheels go as they
are):

```sh
python -m pip install twine==6.1.0 auditwheel==6.4.2
auditwheel repair dist/zxv_sdk-X.Y.Z-py3-none-linux_x86_64.whl -w dist/pypi   # on x86_64 Linux
auditwheel repair dist/zxv_sdk-X.Y.Z-py3-none-linux_aarch64.whl -w dist/pypi  # on arm64 Linux
cp dist/zxv_sdk-X.Y.Z.tar.gz dist/zxv_sdk-X.Y.Z-*macosx*.whl dist/pypi/
twine check dist/pypi/*
twine upload --repository testpypi dist/pypi/*    # rehearsal
twine upload dist/pypi/*
twine upload dist/django_zxv-*                    # if django-zxv changed
```

**Maven Central** (`org.zedec:zxv`, the Kotlin jar and AAR, the Spring
starter). Central requires `<url>`, `<scm>` and `<developers>` in each pom
and a GPG signature on every file; the poms do not have the first three yet,
so add them before the first upload. Build the bundle from the release
files (`zxv-X.Y.Z.jar` already carries libzxv for four platforms) and the
pom at the tag:

```sh
d=bundle/org/zedec/zxv/X.Y.Z && mkdir -p "$d"
cp dist/zxv-X.Y.Z.jar dist/zxv-X.Y.Z-sources.jar dist/zxv-X.Y.Z-javadoc.jar "$d/"
cp sdk/java/pom.xml "$d/zxv-X.Y.Z.pom"
for f in "$d"/*; do gpg --armor --detach-sign "$f"; md5sum "$f" | cut -d' ' -f1 > "$f.md5"; sha1sum "$f" | cut -d' ' -f1 > "$f.sha1"; done
(cd bundle && zip -r ../zxv-X.Y.Z-bundle.zip org)
```

Upload `zxv-X.Y.Z-bundle.zip` at central.sonatype.com (**Publish
component**), check the validation, then press **Publish**. Repeat for
`org.zedec:spring-zxv` (parent pom) and `org.zedec:zxv-spring-boot-starter`,
and for the Kotlin jar and AAR once they have coordinates and poms of their
own.

**NuGet** (dependency order):

```sh
for p in Zxv.Native Zxv.Sdk Zxv.AspNetCore; do
  dotnet nuget push "dist/$p.X.Y.Z.nupkg" --api-key "$NUGET_API_KEY" --source https://api.nuget.org/v3/index.json
done
```

**npm** (scoped, so public access must be explicit):

```sh
npm publish dist/zedec-zxv-X.Y.Z.tgz --access public --dry-run
npm publish dist/zedec-zxv-X.Y.Z.tgz --access public            # pre-release: add --tag next
```

`@zedec/zxv-nestjs` is `"private": true`, so npm refuses it until that line
is removed in a deliberate commit.

**crates.io** (from the tag checkout; `zxv-sys` first):

```sh
cd sdk/rust
cargo publish -p zxv-sys --dry-run && cargo publish -p zxv-sys
cargo publish -p zxv --dry-run && cargo publish -p zxv
```

**RubyGems** (the gemspec requires MFA):

```sh
gem push dist/zxv-X.Y.Z.gem
```

**pub.dev**: `sdk/mobile/dart/pubspec.yaml` says `publish_to: none`, which
blocks uploads. Publishing means removing that line in a reviewed commit,
adding a `CHANGELOG.md` and a `LICENSE` to `sdk/mobile/dart`, then:

```sh
cd sdk/mobile/dart && dart pub publish --dry-run && dart pub publish
```

**Not registry-ready yet** (owner decisions, not steps):

- Packagist (Composer) and SwiftPM read the repository root, but
  `composer.json` and `Package.swift` live in `sdk/php` and `sdk/swift`. Both
  need a split repository (or a root-level manifest) before they can be
  listed. Until then users take the source archives from the release.
- The Unity package, the Godot extension, the kits, the pillars and the
  embedded kits are distributed as source archives only.
