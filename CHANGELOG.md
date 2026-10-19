# Changelog

Releases of PaperBoat — Switch Port. Each version is tagged `switch-v<version>`.

The first three numbers are the PaperBoat release the port is based on. A fourth number counts Switch port
releases on that base: `switch-v1.0.3.1` is the first update after `switch-v1.0.3`.

For changes to PaperBoat itself, see the upstream [releases](https://github.com/HarbourMasters/PaperBoat/releases).

<!--
To release: add a section below headed with the tag (newest first), commit it, then push the tag.
The release workflow copies that section into the GitHub release notes.
-->

## switch-v1.0.3

First release, based on PaperBoat 1.0.3.

### Install and setup
- Runs as Switch homebrew from the Homebrew Menu (custom firmware required).
- First-time setup on the console: put your ROM in `switch/paperboat/` as `baserom.z64`, and the first launch builds
  the game files with a progress screen, then restarts into the game. No PC tools needed.
- A `pm64.o2r` made by PaperBoat on PC works too.

### Updates
- PaperBoat checks for a new release on launch and tells you about it on the title screen or file select.
- Switch tab → Updates: check, download and install updates from inside the game. Saves and settings are kept.

### Performance
- 60 FPS by default with frame interpolation.
- Shaders are compiled during loading screens, and the next area's shaders ahead of time, so new areas and
  battles don't stutter.
- Textures are preloaded during fades. Game files are read with less SD card contention, so loads are faster.
- Game, audio and render threads are spread across the Switch's CPU cores.
- Lower renderer overhead: streamed vertex buffers, fewer uniform updates and fewer draw-call breaks.

### Graphics
- Separate handheld and docked resolution and anti-aliasing, switched automatically when you dock or undock.
- Menus, text and the cursor animate at the full frame rate (Settings → Graphics → Smooth Menus and Text).
- Graphics benchmark in the Switch tab.

### Controls and menu
- **−** opens the PaperBoat menu, which you can navigate with the controller.
- A Switch tab collects the Switch-specific options. PC-only graphics options are hidden.
