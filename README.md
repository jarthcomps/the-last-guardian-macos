# The Last Guardian on macOS

Play your own copy of **The Last Guardian** (PS4) on an Apple Silicon Mac. This repository contains a small launcher app, plus a build of the [shadPS4](https://github.com/shadps4-emu/shadPS4) emulator tuned for this one game on macOS.

> This project contains **no game files**, no PS4 firmware and no Sony software. You need your own legally obtained, decrypted dump of the game.

## Quick install

**You need:**
- An Apple Silicon Mac (M1 or newer) running **macOS 26 (Tahoe)** or newer.
- **Rosetta 2**. The launcher offers to install it if it's missing.
- Your own copy of The Last Guardian, either as the game **PKG** file or as an already extracted folder (the one with `eboot.bin` and `sce_sys/`). The US release `CUSA03745` is tested; the other regions (`CUSA03627`, `CUSA04936`, `CUSA05152`) should work but are untested.
- Recommended: **update 1.03** (PKG or extracted folder). The 720p and 540p speed patches need it.
- About 27 GB of free space if you install from PKG files.
- Optional: PS4 **system modules** (`.sprx` files) dumped from your own console.

**Steps:**
1. Download **[The-Last-Guardian-macOS.zip](https://github.com/jarthcomps/the-last-guardian-macos/raw/main/dist/The-Last-Guardian-macOS.zip)** and unzip it.
2. Move **The Last Guardian.app** to your Applications folder.
3. Open it the first time by right-clicking the app and choosing **Open**. If macOS still refuses, go to **System Settings → Privacy & Security** and click **Open Anyway**. The app isn't notarized by Apple, which is why this step is needed once.
   - Comfortable with Terminal? This does the same thing: `xattr -dr com.apple.quarantine "/Applications/The Last Guardian.app"`
4. In the launcher:
   - **Install from PKG files → Choose PKGs…**: pick the game PKG, then the update PKG (or Cancel to skip it), then a folder to unpack into. A progress bar tracks bytes actually unpacked, including progress within large files. It shows the current package (game, then update), bytes, elapsed time, and an estimated time remaining for that package. The bar starts after package scanning and reaches 100% after the installer validates the output. Unpacking fills in both folders for you.
   - Already extracted? Use **Game folder → Choose…** (the folder with `eboot.bin`) and **Update folder → Choose…** instead. An update next to the game named `<game folder>-UPDATE` is found automatically.
   - Optional: **System modules → Import…** copies `.sprx` files from a folder you pick.
   - Pick a resolution (720p is recommended) and press **Play**.

**shadPS4 settings…** opens shadPS4's own interface (big picture mode), where you can change every emulator setting, including controls. **Open data folder** shows the saves, settings, logs and shader cache. They live in `~/Library/Application Support/The Last Guardian Launcher`.

Controllers that macOS recognizes (DualSense, DualShock 4, Xbox) work through SDL. Keyboard controls can be changed in shadPS4's settings.

## What to expect

Performance varies substantially with the area and camera. On an M2 Max at 720p, the tested pool checkpoint ran around **21–26 FPS**, while a later outdoor checkpoint averaged **18.6–20.1 FPS** in three two-minute combined-optimization runs (paired controls: 17.0–17.8 FPS). This is not a locked 30. First visits also stutter while shaders compile; those are cached for later runs. These are checkpoint measurements, not a full-game performance guarantee.

## What was changed to make it run on a Mac

**In plain words.** A PS4 game talks directly to the PS4's graphics chip. On a Mac every one of those instructions is translated twice: from the PS4's language to Vulkan by shadPS4, then from Vulkan to Apple's Metal by a driver called KosmicKrisp. The PS4's processor code also runs under Rosetta 2. Out of the box this ran at around 20 FPS, with Trico's feathers flickering. The work here made the translation keep the Mac's graphics chip busy in larger chunks instead of stopping and starting, fixed a timing bug that let the game read half-finished feather data, and uses a community patch that renders the game at 720p.

**For the technically curious:**
- **Engine:** an x86_64 shadPS4 build running under Rosetta 2, with Vulkan provided by KosmicKrisp (Mesa's Vulkan-on-Metal 4 driver). The source is in [`engine/`](engine), which is the `shadps4-fork` branch of this repository.
- **Shader header reads:** [`engine-checkpoint-readbacks.patch`](engine-checkpoint-readbacks.patch) avoids waiting for unrelated GPU data on the same memory page. It reads shared CPU backing only when the exact header bytes are current, tracks every pending copy, and rejects host pointers and nonshared mappings. Precise readbacks and feather synchronization remain enabled. The later checkpoint averaged 9.2 FPS with the shortcut disabled and 18.2 FPS enabled in separate runs; animation and camera variation limit an exact comparison.
- **Readbacks:** the game reads GPU results back on the CPU every frame. The fork has a precise, asynchronous readback system: async fences, hot-range prefetch at fences, asynchronous guest page faults, and deferred page unprotection. It also adds a GPU command processor encoder thread with command recording, and fixes for Rosetta stores and PM4 address conversion.
- **Metal render passes:** two macOS 26 index-robustness workarounds in KosmicKrisp unrolled every indirect draw through a compute pass. The launcher turns them off (`MESA_KK_DISABLE_WORKAROUNDS=12,13`). The engine also skips command-stream and draw buffer barriers inside render passes when nothing in the pass writes memory (`SHADPS4_METAL_ENCODER_ORDERING=1`), so passes stay whole on Metal.
- **Feather flicker fix:** a page whose newer GPU→CPU copy was still in flight could be unprotected early, so the game read the previous feather simulation state. Such pages now stay protected until the newer copy lands (`SHADPS4_PREFETCH_STRICT_UNMARK=1`).
- **Outdoor CPU reuse:** [`engine-outdoor-combined.patch`](engine-outdoor-combined.patch) adds descriptor-keyed texture lookup reuse, change-triggered pending-download scans, coherent image hash reads, and a 64 KB readback window. The launcher enables the combined profile with Metal tweaks; standalone engine defaults stay unchanged. Three alternating pairs improved FPS by 8.5%, 10.2%, and 12.5%. Separate verification compared more than 4.3 million texture lookups with the original path without mismatches. All effects, precise readbacks, and synchronization remain enabled. This update was promoted at the user's request without an additional gameplay soak.

- **Driver patch** ([`driver/kosmickrisp-tlg.patch`](driver/kosmickrisp-tlg.patch), against KosmicKrisp's Mesa `b628375`): splits render encoders only for barriers that actually involve the encoder's own writes, makes the pipeline cache key include the workaround settings (so changing them can't reuse stale shaders), and adds shader diagnostic names and encoder statistics.
- **Diagnostics and opt-in experiments,** all off by default: per-frame latency split and GPU backlog (`SHADPS4_EOP_STATS`), guest wait statistics (`SHADPS4_WAIT_STATS`), per-source barrier counts (`SHADPS4_BARRIER_STATS`), and byte-range buffer hazard tracking (`SHADPS4_PRECISE_BUFFER_HAZARDS`; +1.8 FPS on an M4 Mac mini, no change on an M2 Max). The [engine documents](engine/documents/tlg) have the details.

## Building from source

```sh
git clone --recursive https://github.com/jarthcomps/the-last-guardian-macos
cd the-last-guardian-macos
```

1. **Engine** (x86_64, needs Homebrew `cmake` and `ninja`):
   ```sh
   git -C engine apply ../engine-checkpoint-readbacks.patch
   git -C engine apply ../engine-outdoor-combined.patch
   cmake -S engine -B engine/build-release -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
     -DCMAKE_OSX_ARCHITECTURES=x86_64 -DCMAKE_OSX_DEPLOYMENT_TARGET=26.0 \
     -DENABLE_SYSTEM_VULKAN=ON -DENABLE_UPDATER=OFF -DENABLE_DISCORD_RPC=OFF
   cmake --build engine/build-release --target shadps4
   ```
2. **Driver:** apply `driver/kosmickrisp-tlg.patch` to `engine/externals/mesa-kosmickrisp/externals/mesa` (`git apply -p1`). Then build the wrapper for x86_64 (needs Meson, Mako, LLVM, SPIRV-Tools and SPIRV-LLVM-Translator):
   ```sh
   cmake -S engine/externals/mesa-kosmickrisp -B build-driver -G Ninja \
     -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=x86_64 -DCMAKE_OSX_DEPLOYMENT_TARGET=26.0
   cmake --build build-driver
   ```
3. **PKG extractor:** check out [ps4-pkg-tools](https://github.com/xXJSONDeruloXx/ps4-pkg-tools) at `45baeda`, apply `launcher/ps4-pkg-tools-progress.patch` with `git apply` from that checkout, and build `ps4-pkg-tool`. This patch reports uncompressed bytes written at most four times per second, flushes progress through pipes, and detects output write failures.
4. **Launcher and app:** put `shadps4`, `libvulkan.dylib` (the Khronos Vulkan loader), `libvulkan_kosmickrisp.dylib` and `kosmickrisp_mesa_icd.json` in one folder, then run:
   ```sh
   ENGINE_DIR=/path/to/that/folder PKG_TOOL=/path/to/ps4-pkg-tool launcher/build.sh
   ```
   This writes `build/The Last Guardian.app` and `dist/The-Last-Guardian-macOS.zip`. `TLGLauncher --self-test` (inside the app) prints exactly what **Play** would run.

## Credits

The launcher tests cover live pipe output, split UTF-8/lines, byte totals above 4 GB, package transitions, estimates, completion, and failure. With Xcode and XcodeGen installed:

```sh
xcodegen generate --spec launcher/project.yml
xcodebuild test -project launcher/LauncherTests.xcodeproj -scheme TLGLauncher \
  -destination 'platform=macOS' -derivedDataPath build-tests
```

This runs unit tests and native UI tests using a small extractor fixture; no game files are needed. The UI test runner uses a dedicated fixture folder under `/private/tmp/guardian-ui-tests/`. The release app does not include the test launch hooks. The extractor patch also adds a CTest `byte_progress` test. For an optional full extraction check with your own PKG, set `TLG_TEST_PKG`, `TLG_TEST_PKG_TOOL`, and `TLG_TEST_REFERENCE` (an already extracted matching folder), then run `python3 launcher/Tests/test_real_pkg_progress.py`. Set `TLG_TEST_LAUNCHER` to an app's `Contents/MacOS/TLGLauncher` to exercise its bundled extractor through `--install-pkg`. It checks live reports and compares every extracted file by SHA-256.

`python3 launcher/Tests/test_performance_profile.py` checks that the fast shader-header profile preserves precise readbacks and feather synchronization. The engine patch adds CTest targets `backing_range` and `download_overlap`; these cover address bounds, nonshared mappings, exact copy overlap and asynchronous ownership lifetime. The existing `prefetch_unmark_policy` test covers page-unmark ordering. Local validation ran these three tests as both arm64 and x86_64 binaries.

This builds entirely on other people's work. Thank you to:

- **[shadPS4](https://github.com/shadps4-emu/shadPS4)** and its contributors, the PS4 emulator this is built on (GPL-2.0).
- **thearturord**, for The Last Guardian performance branch of shadPS4 this work started from: readback system, encoder thread, profiling tools.
- **[KosmicKrisp](https://docs.mesa3d.org/drivers/kosmickrisp.html)** (LunarG and the Mesa developers), the Vulkan driver on Metal that makes shadPS4 possible on macOS, and **[the Mesa 3D project](https://mesa3d.org)**. Also **shadexternals/mesa-kosmickrisp**, the shadPS4 team's packaging of it.
- **[Khronos Group](https://github.com/KhronosGroup/Vulkan-Loader)** for Vulkan and the Vulkan loader.
- **illusion** and **kalaposfos**, for the 720p and 540p resolution patches.
- **[ps4-pkg-tools](https://github.com/xXJSONDeruloXx/ps4-pkg-tools)** (GPL-2.0-or-later, commit `45baeda` plus the included byte-progress patch), which unpacks the PKG files, built from shadPS4's former PKG code.
- The libraries shadPS4 uses, including **SDL3**, **Dear ImGui**, **FFmpeg**, **OpenAL Soft**, **Boost**, **fmt**, **glslang**, **sirit**, **Vulkan Memory Allocator**, **Zydis**, **xbyak**, **xxHash** and **zlib-ng** (full list in [`engine/externals`](engine/externals)).
- **Apple**, for Rosetta 2 and Metal.
- **Claude** (Anthropic) and **ChatGPT / Codex** (OpenAI), the AI assistants used for the porting, profiling and packaging work.
- And **genDESIGN, Japan Studio and Sony Interactive Entertainment**, for The Last Guardian.

## Legal

The Last Guardian is © Sony Interactive Entertainment. PlayStation and PS4 are trademarks of Sony Interactive Entertainment Inc. This project isn't affiliated with or endorsed by Sony, genDESIGN or the shadPS4 team. It ships no game data, firmware or keys of its own; the bundled PKG extractor is open-source ps4-pkg-tools with the included progress-reporting patch. Use it only with games and system files you legally own.

The launcher and engine are licensed under the GPL-2.0 (see [LICENSE](LICENSE)). The bundled KosmicKrisp driver is MIT licensed (Mesa), and the Vulkan loader is Apache-2.0.
