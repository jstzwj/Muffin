# CLAUDE.md

## Build

This project uses Conan-generated CMake presets. Use the Release preset for local verification:

```powershell
conan install . -s build_type=Release -s compiler.cppstd=17 --build=missing
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
