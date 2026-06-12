# CLAUDE.md

## Build

This project uses Conan-generated CMake presets. Use the Release preset for local verification:

```powershell
conan install . -s build_type=Release -s compiler.cppstd=20 --build=missing
cmake --preset conan-default
cmake --build --preset conan-release
```

Do not use plain `cmake --build build` for verification on this workspace. The generated Visual Studio project may default to Debug, while the Conan Qt environment is configured for Release, which can fail with missing Qt headers such as `QString`.

## Test

Run the full test suite with:

```powershell
ctest --preset conan-release --output-on-failure
```

Before reporting a change as verified, prefer running both:

```powershell
cmake --build --preset conan-release
ctest --preset conan-release --output-on-failure
```

## Package And Run

`cmake --build --preset conan-release` updates `build\Release\Muffin.exe`, but it does not update `build\dist\Muffin.exe`.

To refresh the distributable app bundle, run the dist target after building:

```powershell
cmake --build --preset conan-release
cmake --build --preset conan-release --target dist
```

The packaged executable is:

```powershell
.\build\dist\Muffin.exe
```

If `build\dist\Muffin.exe` is older than `build\Release\Muffin.exe`, run the dist target again before testing the packaged app.

## Application Icons

The committed raster icons in `resources/app-icons/` (PNGs at 16–1024px, `muffin.ico`, `muffin.icns`) are embedded by CMake automatically — the `.ico` into the Windows `.exe` via `cmake/muffin.rc.in`, the `.icns` into the macOS `.app` bundle, and a 256px PNG as the runtime window icon (all platforms, primary on Linux). No GitHub Actions change is needed; the build picks them up on every platform.

They are generated from `logo.svg`. Regenerate after editing the logo:

```powershell
python scripts/generate_icons.py
```

This requires PySide6 (uses `QSvgRenderer`, the same Qt SVG engine the app links) and Pillow. Both ship in CI; locally `pip install PySide6 Pillow`. Do not hand-edit the generated files.

## Translation (i18n)

### lupdate namespace context bug

`lupdate` fails to track namespace prefixes across `namespace muffin { }` wrappers in `.cpp` files. It generates context `ClassName` instead of the correct `muffin::ClassName`, causing translations to never match at runtime (where `tr()` uses `metaObject()->className()` = `muffin::ClassName`).

**Rule:** All `.cpp` files in `MUFFIN_TRANSLATABLE_SOURCES` that contain `tr()` calls MUST use fully qualified method names (e.g. `muffin::MainWindow::setupFileMenu()`) instead of `namespace muffin { }` wrapping. Free functions and anonymous namespaces that don't use `tr()` may remain inside `namespace muffin { }` or at file scope.

When adding a new `.cpp` file with `tr()` calls to the translatable sources, do NOT wrap it in `namespace muffin { }`. Use `muffin::ClassName::method()` for all method definitions instead.

### Updating translations

After modifying translatable strings:

```powershell
cmake --build --preset conan-release --target update_translations
cmake --build --preset conan-release --target release_translations
cmake --build --preset conan-release --target dist
```

Use subagents to modify `.ts` files in parallel — avoid bulk Python/sed scripts for XML manipulation as they are error-prone (losing closing tags, removing valid entries, etc.).
