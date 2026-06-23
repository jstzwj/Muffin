# libavif (vendored)

AVIF decoder backing `src/render/ImageDecoder.cpp`'s `decodeAvif()`. Built directly by Muffin's
CMake — **not** via Conan.

## Why vendored (and why libyuv is gone)

The ConanCenter `libavif/1.4.1` recipe **hard-requires `libyuv`**, whose only source is
`chromium.googlesource.com` — unreachable from GitHub Actions IPs (timeouts / 503), breaking
`conan install` on macOS where libyuv has no matching prebuilt.

So libavif is built from source here with:

- `AVIF_CODEC_DAV1D=SYSTEM` — decode via **dav1d** (still from Conan; reliable VideoLAN source).
- `AVIF_LIBYUV=OFF` — don't link an **external** libyuv. libavif then compiles its own tiny
  **bundled** libyuv subset (`third_party/libyuv/`, 5 `.c` files — see below) for its internal
  scaling. This is committed libavif source, **not** the googlesource-fetched libyuv, so there is
  no `chromium.googlesource.com` fetch at all.
- `AVIF_CODEC_AOM=OFF` — decode-only, no aom.
- `AVIF_BUILD_APPS/TESTS/EXAMPLES=OFF`, static (`BUILD_SHARED_LIBS=OFF`).

Result: no **external** libyuv, no aom, no googlesource — the AVIF stack only needs dav1d from
outside; libavif's internal libyuv subset ships as committed source.

The integration lives in the top-level `CMakeLists.txt` (search for `third_party/libavif`),
mirroring the `nuspell` subproject pattern. Muffin links the `avif_static` target.

> **dav1d must be linked explicitly by the consumer.** `avif_static` is a merged static archive
> (`merge_static_libs`) that only folds in libavif's `AVIF_LOCAL` deps; a `SYSTEM` dav1d from
> Conan is **not** merged into the archive and **not** forwarded by the archive target. So
> `dav1d::dav1d` is linked directly on `MuffinUi` (PRIVATE — it still propagates to every
> `Muffin.exe` / test consumer because `MuffinUi` is STATIC). Without this, libavif's dav1d
> symbols are undefined at the final link (seen on macOS arm64; would also hit Linux).

## Local patches to upstream

- **`CMakeLists.txt` dav1d dl-link (Unix)** — upstream's dav1d block does
  `target_link_libraries(dav1d::dav1d INTERFACE ${CMAKE_DL_LIBS})` on Linux for dlsym.
  Because we set `AVIF_CODEC_DAV1D=SYSTEM`, `dav1d::dav1d` is an **IMPORTED** target from
  Conan, and CMake rejects modifying an imported target's link interface
  ("Cannot specify link libraries ... which is not built by this project") — Linux CI
  failed at configure time. Patched to check the target's `IMPORTED` property and, when
  imported, link `dl` onto `avif_obj` instead of the imported dav1d target. The block is
  already `if(UNIX AND NOT APPLE)` so Windows/macOS were never affected. Look for the
  `_muffin_dav1d_imported` guard.

## Provenance

| | |
|---|---|
| upstream | https://github.com/AOMediaCodec/libavif |
| version  | v1.4.1 (archive of tag `v1.4.1`) |
| acquired | `https://github.com/AOMediaCodec/libavif/archive/refs/tags/v1.4.1.tar.gz` |

## What was stripped from the upstream archive

The full archive is ~21 MB (415 files); this tree is ~1.8 MB (90 files). Removed:

- `tests/` — 18 MB of golden/test-data (only built with `AVIF_BUILD_TESTS=ON`)
- `apps/` — avifenc/avifdec (`AVIF_BUILD_APPS=OFF`)
- `examples/`, `android_jni/`, `ext/` — apps/tests/LOCAL-codec only
- `.github/`, upstream `.gitignore`, `.clang-format`, `.cmake-format.py` — libavif's own CI /
  formatting / git config, irrelevant to us (we have our own CI).

Kept (build-essential): `CMakeLists.txt`, `cmake/` (incl. `Modules/Finddav1d.cmake`), `include/`,
`src/`, `contrib/` (`add_subdirectory(contrib)` is unconditional upstream — leaves the tiny
gdk-pixbuf loader sources in place, which don't build on our platforms), `third_party/libyuv/`
(libavif's bundled libyuv subset, compiled internally with `AVIF_LIBYUV=OFF`), `doc/`, `LICENSE`.

`.gitattributes` forces LF checkout so the tree is identical across platforms.

## Refreshing (if you bump libavif)

1. `curl -L -o libavif-<ver>.tar.gz https://github.com/AOMediaCodec/libavif/archive/refs/tags/v<ver>.tar.gz`
2. Extract, strip the dirs/files listed above (keep `third_party/libyuv/`!), drop over
   `third_party/libavif/`.
3. Bump the version note above; confirm the `AVIF_*` option names still match upstream's
   `CMakeLists.txt` (they can change between releases), and that `AVIF_LIBYUV=OFF` still compiles
   the bundled `third_party/libyuv/` subset (lines referencing it move between releases).
4. Rebuild; confirm `find_package(dav1d)` still resolves to Conan's dav1d.
5. Re-apply any **Local patches to upstream** (above) — re-diff against upstream and
   confirm they still apply (or are no longer needed because upstream fixed them).
