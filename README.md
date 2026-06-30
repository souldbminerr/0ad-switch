# 0 A.D. Switch

Port of 0 A.D. to Switch homebrew.

- Full Release 28 gameplay support
- JIT SpiderMonkey port (needs to be downloaded/compiled from souldbminerr/spidermonkey-NX)
- Mod support (not fully complete yet!)
- boost/fmt/etc. ports

## Building

The toolchain is host-agnostic (devkitPro's `aarch64-none-elf` cross-compiler), but
the build must be on Linux/WSL

- **Windows**: use **WSL** (an Arch Linux distro, `wsl -d archlinux`). Examples below
  use this form. msys2 also has the toolchain, but it is unreliable so use WSL.
- **Linux (native)**: run scripts directly with bash (drop `wsl -d archlinux -e` prefix). 
  Install the Switch `portlibs` with `dkp-pacman`, so
  you can skip the `setup-wsl-portlibs.sh` mirroring step.

### Prerequisites
1. devkitPro/libnx installed

2. SpiderMonkey-NX prebuilt static libs (`libjs_static.a`, `libjsrust.a`,
   `libswitchextra.a`), build from
   [souldbminerr/spidermonkey-NX](https://github.com/souldbminerr/spidermonkey-NX) and place in `libs/`
3. premake5 built for Linux and placed at `0ad-src/libraries/source/premake-core/bin/premake5`
4. boost — drop `boost_1_83_0.tar.gz` into `deps/src/` and run the dependency
   scripts for your system.

### Steps

```sh
wsl -d archlinux -e bash 0ad-src/build/switch/generate.sh
wsl -d archlinux -e bash 0ad-src/build/switch/build-engine.sh
wsl -d archlinux -e bash 0ad-src/build/switch/link-nro.sh
```

The result is `0ad-src/binaries/system/pyrogenesis.nro`. Copy it (plus the 0 A.D.
game data) to your Switch's SD card to run.

### Rebuilds

`build-engine.sh` takes project names to rebuild parts of engine
`build-engine.sh gui graphics`, followed by `link-nro.sh`. 

> **Note:** the 0 A.D. binary art assets (textures, models, audio, fonts) are
> upstreamed data and are *not* included in this repo. Obtain the 0 A.D.
> Release 28 game data separately.
