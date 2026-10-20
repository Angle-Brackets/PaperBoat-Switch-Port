# PaperBoat — Switch Port
*A Nintendo Switch port of [PaperBoat](https://github.com/HarbourMasters/PaperBoat), Harbour Masters' PC port of Paper Mario 64*

Switch port by Soupa. PaperBoat itself is by the Harbour Masters team (Bass3l, JeodC, KiritoDv, Caladius).

**Latest version: switch-v1.0.3-rc1** (release candidate) · [What's new](CHANGELOG.md) · [Download](../../releases)

> This is an unofficial port. Please don't ask Harbour Masters for help with the Switch version; open an
> [issue here](../../issues) instead. We do not condone piracy: you need your own copy of the game.

## Features
- Runs natively as Switch homebrew, at 60 FPS by default with frame interpolation.
- Menus, text and the cursor animate at the full frame rate too (Settings → Graphics → *Smooth Menus and Text*).
- Separate handheld and docked render settings, switched automatically when you dock or undock.
- Shaders are compiled during loading screens and predicted ahead of time, so walking into a new area or
  battle doesn't stutter.
- Textures are preloaded during fades, and game files are read with less SD card contention, for faster loads.
- Rendering work is spread across the Switch's CPU cores.
- A **Switch** tab in the menu with performance options and a graphics benchmark.
- Builds the game files from your ROM on the console itself. No PC tools needed.

# Install

You need a Switch running custom firmware (Atmosphère) with the Homebrew Menu.

### 1. Verify your ROM dump
Only the US version is supported. SHA-1: `3837f44cda784b466c9a2d99df70d77c322b97a0`

You can check your dump at https://paperboat.equipment/. It must be a `.z64` file (big-endian).

### 2. Copy PaperBoat to your SD card
Download `PaperBoat-Switch.zip` from [Releases](../../releases) and extract it to the root of your SD card.
That gives you `switch/paperboat/paperboat.nro`.

### 3. Add your ROM
Copy your ROM into `switch/paperboat/` and name it **`baserom.z64`**.

Keep it there after setup: a future update may need to rebuild the game files from it.

### 4. Launch
Open the Homebrew Menu **through a game**: hold **R** while starting any game, then pick PaperBoat.
Don't launch from the Album. That mode gives homebrew much less memory.

On first launch PaperBoat builds the game files (`pm64.o2r`) from `baserom.z64` by itself, showing each step's
progress. This takes about a minute and a half and only happens once. Then PaperBoat restarts into the game.

**Already have PaperBoat on PC?** Copy the `pm64.o2r` it generated into `switch/paperboat/` instead of the ROM
to skip that step.

### Home screen icon (optional)
To launch PaperBoat from the Switch home screen, make a **forwarder**: a small installable title that opens
`sdmc:/switch/paperboat/paperboat.nro`. Forwarders are built with your console's own keys, so we can't ship
one. Use a forwarder creator tool with your `prod.keys`, point it at that path, and install the result.
A forwarder always gets full memory, and it keeps working after you update the `.nro`.

# Updating
**In the game:** PaperBoat checks for a new release when it starts and shows a notification if there is one.
Open the menu (**−**) → **Switch** → **Updates**, choose **Download Update**, then **Restart and Update**.
You can turn off the launch check there, or use **Check Now**. Nothing is downloaded until you choose to.

**By hand:** download the new `PaperBoat-Switch.zip` and extract it over the old one, replacing `paperboat.nro`.

Either way, your saves, settings and game files are kept. See the [changelog](CHANGELOG.md) for what each update changes.

# Playing

- **−** (Minus) opens the PaperBoat menu, which you can navigate with the controller.
- Controls follow the standard layout. Remap them under Settings → Controller.
- Saves and settings are stored in `switch/paperboat/`.

### Switch tab
| Option | What it does |
| - | - |
| Hold Loads for Shaders | Waits on loading screens until the next area's shaders are ready (avoids stutter). |
| Smooth Fade-In | Holds the screen dark until the first frames are ready. |
| Predict Next Area's Shaders | Compiles shaders for where you're likely to go next while the game is idle. |
| Preload Textures | Loads textures during fades instead of on first use. |
| Spread Threads Across Cores | Pins game, render and loader threads to separate cores (applies next launch). |
| Renderer options | Driver-overhead reductions. Leave them on unless you see graphics glitches. |
| Benchmarks | Measure frame times at different resolutions and anti-aliasing levels. |
| Updates | Shows your version, checks for new releases and installs them. |

All are on by default.

# Building

Requirements: [devkitPro](https://devkitpro.org/wiki/Getting_Started) with `switch-dev`, plus
`switch-sdl2 switch-glad switch-mesa switch-libdrm_nouveau switch-curl`, and CMake 3.24+.

```sh
git clone --recursive https://github.com/Angle-Brackets/PaperBoat-Switch-Port.git
cd PaperBoat-Switch-Port
cmake -B build-switch -S . -DCMAKE_TOOLCHAIN_FILE=$DEVKITPRO/cmake/Switch.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build-switch --target Paperboat_nro -j
```

The result is `build-switch/paperboat.nro`, with `paperboat.o2r`, `config.yml` and the extractor's `assets/`
bundled inside it.

To test on hardware, open the Homebrew Menu's NetLoader (**Y**) and run
`switch/run.sh <switch-ip>`. It sends the build with nxlink and streams the game's log to your terminal.

Releases are built by GitHub Actions (`.github/workflows/switch-release.yml`) when a `switch-v*` tag is pushed.

The Switch port uses a [fork of libultraship](https://github.com/Angle-Brackets/libultraship/tree/pm64-switch)
(branch `pm64-switch`) with Switch support. Upstream libultraship dropped Switch support.

For desktop builds, see the upstream [building instructions](docs/BUILDING.md).

<a href="https://github.com/Kenix3/libultraship/">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="./docs/poweredbylus.darkmode.png">
    <img alt="Powered by libultraship" src="./docs/poweredbylus.lightmode.png">
  </picture>
</a>

# Special Thanks

* [Harbour Masters](https://www.harbourmasters.org/) and the PaperBoat team, for PaperBoat and libultraship
* [The Paper Mario decomp team](https://github.com/pmret/papermario)
* [The Paper Mario DX team](https://github.com/bates64/papermario-dx)
* [devkitPro](https://devkitpro.org/) and the Switch homebrew community

# AI Disclosure

The Switch port was developed with the help of AI tools. Like PaperBoat, this repo follows the HarbourMasters
[AI Policy](https://github.com/HarbourMasters/code-of-conduct/blob/main/AI_POLICY.md), and accepts pull
requests that use AI within its guidelines.
