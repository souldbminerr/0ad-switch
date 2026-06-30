# 0 A.D. Switch dependencies

Builds/installs 0 A.D.'s external libraries into the devkitPro **portlibs**
(`/opt/devkitpro/portlibs/switch`) so Pyrogenesis can be cross-compiled for the
Switch (aarch64 / newlib / libnx).

SpiderMonkey is built separately (see `spidermonkey-NX/`); it is not handled here.

## Usage (run from msys2)

```sh
./build-deps.sh          # all of the below
./build-deps.sh pacman   # libxml2, enet, libsodium, openal-soft (devkitPro pkgs)
./build-deps.sh fmt      # fmt 10.2.1  -> libfmt.a
./build-deps.sh boost    # boost 1.83 headers (header-only on this target)
./build-deps.sh icu      # ICU 74.2    -> libicuuc/i18n/data/io.a (delegates to WSL)
```

## What comes from where

| Library        | Source                              | Notes |
|----------------|-------------------------------------|-------|
| libxml2, enet, libsodium, openal-soft | devkitPro `pacman`     | prebuilt switch-* packages |
| fmt 10.2.1     | `src/fmt-10.2.1.tar.gz`             | CMake + `Switch.cmake` toolchain |
| boost 1.83     | `src/boost_1_83_0.tar.gz`          | headers only — 0 A.D. links no boost lib on this target |
| ICU 74.2       | `src/icu4c-74_2-src.tgz`           | host tools (Linux gcc) + devkitA64 target, static data |

## Why ICU needs WSL

ICU's build first compiles host-native data/build tools (`genrb`, `pkgdata`, ...)
and then cross-compiles the target reusing them via `--with-cross-build`. The host
tools want a normal Linux toolchain, so `icu-cross.sh` runs in WSL (devkitPro is
installed natively there too) and installs the resulting static libs into the
msys2 portlibs at the end. It is presented to ICU's configure as
`aarch64-unknown-linux-gnu` (so it selects `config/mh-linux`); the real compiler
is devkitA64 via `CC`/`CXX`.

The big sources in `src/` are vendored to pin exact versions.
