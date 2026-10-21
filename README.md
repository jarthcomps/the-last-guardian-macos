# The Last Guardian on macOS

> Note from the developer: This is 100% considered "alpha" software. I really wanted to play this game on my macbook but it's not supported in shadPS4 yet, so I used LLMs to modify it. If you run into problems while playing, please feel free to report them as Issues here on github. I'll try to take care of any problems asap, but you'll need to be very specific about what went wrong and which exact computer / MacOS version you're on.

Play **The Last Guardian** (PS4) on Mac. This repository contains a small launcher app, plus a build of the [shadPS4](https://github.com/shadps4-emu/shadPS4) emulator tuned for this one game on macOS.

> This project contains **no game files**, no PS4 firmware and no Sony software. You need your own legally obtained, decrypted dump of the game.

## Quick start

**Requirements:**
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
   - **Install from PKG files → Choose PKGs…**: pick the game PKG, then the update PKG (or Cancel to skip it), then a folder to unpack into. Unpacking takes a few minutes (be patient!) and fills in both folders for you. A progress bar shows the bytes unpacked and the estimated time left for each package.
   - Already extracted? Use **Game folder → Choose…** (the folder with `eboot.bin`) and **Update folder → Choose…** instead. An update next to the game named `<game folder>-UPDATE` is found automatically.
   - Optional: **System modules → Import…** copies `.sprx` files from a folder you pick.
   - Pick a resolution (720p is recommended) and press **Play**.

**shadPS4 settings…** opens shadPS4's own interface (big picture mode), where you can change every emulator setting, including controls. **Open data folder** shows the saves, settings, logs and shader cache. They live in `~/Library/Application Support/The Last Guardian Launcher`.

Controllers that macOS recognizes (DualSense, DualShock 4, Xbox) work through SDL. Keyboard controls can be changed in shadPS4's settings.

## Performance

On an M2 Max at 720p, the game runs at roughly **27–32 FPS** in demanding areas. It's playable, but not a locked 30. The first time each area loads you'll see some stutter while shaders compile; those are cached for later runs. On an M4 mac mini, it ran around 24fps in the opening scene.

Outdoors, after leaving the first cave, it is slower: around **22–23 FPS** on an M2 Max with launcher 1.1.5 in alternating test runs (it started near 13–15 FPS). The frame rate can still sag to single digits for a while after a few minutes of play; that happens in 1.1.4 too and is being investigated. Raising outdoors to a steady 25+ is the current focus.

## Changelist

**In plain words.** A PS4 game talks directly to the PS4's graphics chip. On a Mac every one of those instructions is translated twice: from the PS4's language to Vulkan by shadPS4, then from Vulkan to Apple's Metal by a driver called KosmicKrisp. The PS4's processor code also runs under Rosetta 2. Out of the box this ran at around 20 FPS, with Trico's feathers flickering. The work here made the translation keep the Mac's graphics chip busy in larger chunks instead of stopping and starting, fixed a timing bug that let the game read half-finished feather data, and uses a community patch that renders the game at 720p.

**For the technically curious:**
- **Engine:** an x86_64 shadPS4 build running under Rosetta 2, with Vulkan provided by KosmicKrisp (Mesa's Vulkan-on-Metal 4 driver). The source is in [`engine/`](engine), which is the `shadps4-fork` branch of this repository.
- **Readbacks:** the game reads GPU results back on the CPU every frame. The fork has a precise, asynchronous readback system: async fences, hot-range prefetch at fences, asynchronous guest page faults, and deferred page unprotection. It also adds a GPU command processor encoder thread with command recording, and fixes for Rosetta stores and PM4 address conversion.
- **Metal render passes:** two macOS 26 index-robustness workarounds in KosmicKrisp unrolled every indirect draw through a compute pass. The launcher turns them off (`MESA_KK_DISABLE_WORKAROUNDS=12,13`). The engine also skips command-stream and draw buffer barriers inside render passes when nothing in the pass writes memory (`SHADPS4_METAL_ENCODER_ORDERING=1`), so passes stay whole on Metal.
- **Feather flicker fix:** a page whose newer GPU→CPU copy was still in flight could be unprotected early, so the game read the previous feather simulation state. Such pages now stay protected until the newer copy lands (`SHADPS4_PREFETCH_STRICT_UNMARK=1`).
- **Shader header reads:** when the game's shaders read data that sits on the same memory page as unrelated GPU results, the engine reads the exact bytes from shared memory instead of waiting for the GPU, but only when those bytes are known to be current (`shader_backing_reads` in the game's readback config).
- **Outdoor CPU work (launcher 1.1.2–1.1.4):** the command processor reuses texture lookups (`SHADPS4_TEXTURE_LOOKUP_REUSE`), only rescans pending GPU→CPU copies when they change (`SHADPS4_DOWNLOAD_RESCAN_REUSE`), hashes images from shared memory (`SHADPS4_IMAGE_BACKING_READS`), uses 64 KB readback windows (`SHADPS4_READBACK_WINDOW_KB=64`), skips unchanged dynamic draw state (`SHADPS4_DYNAMIC_STATE_REUSE`), tracks page protection by range (`SHADPS4_RANGE_PROTECTION`), and the driver keeps a larger cache of upload memory (`KK_UPLOAD_CACHE_EXPANDED`). Together these took the outdoor checkpoint from about 17 to about 20–21 FPS in alternating test runs. The launcher turns them on with its Mac performance setting; the engine's defaults are unchanged.
- **No AVX2 under Rosetta (launcher 1.1.5):** Rosetta runs 256-bit AVX2 instructions as two 128-bit halves; a vectorized loop ran 38% slower as AVX2 than as SSE. macOS engine builds now target x86-64-v2 (`SHADPS4_X86_MARCH`), which took the outdoor checkpoint from 20.9 to 22.5 FPS (median of alternating runs, every pair faster).
- **Faster shader math (launcher 1.1.5, a launcher switch, on by default):** also turns off KosmicKrisp's NaN-preserving comparison workarounds 14 and 17 (`MESA_KK_DISABLE_WORKAROUNDS=12,13,14,17`), a few percent faster outdoors in most test pairs. Turn the switch off if something looks wrong.
- **Settings updates (launcher 1.1.5):** game settings added in a newer launcher are now merged into an existing install's settings instead of only being used on a first install.
- **Render state fix:** a depth-only or stencil-only attachment left the other aspect's settings uninitialized, which could produce an invalid Vulkan load operation and crash; every render state field now has a default.
- **Driver patch** ([`driver/kosmickrisp-tlg.patch`](driver/kosmickrisp-tlg.patch), against KosmicKrisp's Mesa `b628375`): splits render encoders only for barriers that actually involve the encoder's own writes, makes the pipeline cache key include the workaround settings (so changing them can't reuse stale shaders), adds the expanded upload-memory cache, and adds shader diagnostic names and encoder statistics, and can load hand-tuned Metal versions of the game's compute shaders in place of the generated code (`KK_SHADPS4_MSL_OVERRIDE_DIR`, a folder of `<key>.metal` files; off unless set). It also skips re-sending per-draw state that did not change (vertex bindings, root table, push data).
- **Diagnostics and opt-in experiments,** all off by default: per-frame latency split and GPU backlog (`SHADPS4_EOP_STATS`), guest wait statistics (`SHADPS4_WAIT_STATS`), per-source barrier counts (`SHADPS4_BARRIER_STATS`), and byte-range buffer hazard tracking (`SHADPS4_PRECISE_BUFFER_HAZARDS`; +1.8 FPS on an M4 Mac mini, no change on an M2 Max). The [engine documents](engine/documents/tlg) have the details.

## Building from source

```sh
git clone --recursive https://github.com/jarthcomps/the-last-guardian-macos
cd the-last-guardian-macos
```

1. **Engine** (x86_64, needs Homebrew `cmake` and `ninja`):
   ```sh
   cmake -S engine -B engine/build-release -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
     -DCMAKE_OSX_ARCHITECTURES=x86_64 -DCMAKE_OSX_DEPLOYMENT_TARGET=26.0 \
     -DENABLE_SYSTEM_VULKAN=ON -DENABLE_UPDATER=OFF -DENABLE_DISCORD_RPC=OFF -DTRACY_ENABLE=OFF
   cmake --build engine/build-release --target shadps4
   ```
2. **Driver:** apply `driver/kosmickrisp-tlg.patch` to `engine/externals/mesa-kosmickrisp/externals/mesa` (`git apply -p1`). Then build the wrapper for x86_64 (needs Meson, Mako, LLVM, SPIRV-Tools and SPIRV-LLVM-Translator):
   ```sh
   cmake -S engine/externals/mesa-kosmickrisp -B build-driver -G Ninja \
     -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=x86_64 -DCMAKE_OSX_DEPLOYMENT_TARGET=26.0
   cmake --build build-driver
   ```
3. **PKG extractor:** check out [ps4-pkg-tools](https://github.com/xXJSONDeruloXx/ps4-pkg-tools) at `45baeda`, apply `launcher/ps4-pkg-tools-progress.patch` with `git apply` (it reports bytes unpacked for the launcher's progress bar), and build `ps4-pkg-tool`.
4. **Launcher and app:** put `shadps4`, `libvulkan.dylib` (the Khronos Vulkan loader), `libvulkan_kosmickrisp.dylib` and `kosmickrisp_mesa_icd.json` in one folder, then run:
   ```sh
   ENGINE_DIR=/path/to/that/folder PKG_TOOL=/path/to/ps4-pkg-tool launcher/build.sh
   ```
   This writes `build/The Last Guardian.app` and `dist/The-Last-Guardian-macOS.zip`. `TLGLauncher --self-test` (inside the app) prints exactly what **Play** would run.

The `engine` branch already contains every engine change described above, so no extra patches are needed.

**Tests.** With Xcode and XcodeGen installed, `xcodegen generate --spec launcher/project.yml` and then `xcodebuild test -project launcher/LauncherTests.xcodeproj -scheme TLGLauncher -destination 'platform=macOS' -derivedDataPath build-tests` run the launcher's unit and UI tests with a small extractor fixture (no game files needed). `python3 launcher/Tests/test_performance_profile.py` checks the performance profile keeps precise readbacks and feather synchronization. The engine's own tests are CTest targets under `engine/tests`.

## Credits

This builds entirely on other people's work. Thank you to:

- **[shadPS4](https://github.com/shadps4-emu/shadPS4)** and its contributors, the PS4 emulator this is built on (GPL-2.0).
- **thearturord**, for The Last Guardian performance branch of shadPS4 this work started from: readback system, encoder thread, profiling tools.
- **[KosmicKrisp](https://docs.mesa3d.org/drivers/kosmickrisp.html)** (LunarG and the Mesa developers), the Vulkan driver on Metal that makes shadPS4 possible on macOS, and **[the Mesa 3D project](https://mesa3d.org)**. Also **shadexternals/mesa-kosmickrisp**, the shadPS4 team's packaging of it.
- **[Khronos Group](https://github.com/KhronosGroup/Vulkan-Loader)** for Vulkan and the Vulkan loader.
- **illusion** and **kalaposfos**, for the 720p and 540p resolution patches.
- **[ps4-pkg-tools](https://github.com/xXJSONDeruloXx/ps4-pkg-tools)** (GPL-2.0-or-later, commit `45baeda` plus the included progress-reporting patch), which unpacks the PKG files, built from shadPS4's former PKG code.
- The libraries shadPS4 uses, including **SDL3**, **Dear ImGui**, **FFmpeg**, **OpenAL Soft**, **Boost**, **fmt**, **glslang**, **sirit**, **Vulkan Memory Allocator**, **Zydis**, **xbyak**, **xxHash** and **zlib-ng** (full list in [`engine/externals`](engine/externals)).
- **Apple**, for Rosetta 2 and Metal.
- **Claude** (Anthropic) and **ChatGPT / Codex** (OpenAI), the dirty rotten, intellectual-property thieving AI assistants without which the porting, profiling and packaging work could have never taken place.
- And **genDESIGN, Japan Studio and Sony Interactive Entertainment**, for The Last Guardian.

## Legal

The Last Guardian is © Sony Interactive Entertainment. PlayStation and PS4 are trademarks of Sony Interactive Entertainment Inc. This project isn't affiliated with or endorsed by Sony, genDESIGN or the shadPS4 team. It ships no game data, firmware or keys of its own; the bundled PKG extractor is open-source ps4-pkg-tools with the included progress-reporting patch. Use it only with games and system files you legally own.

The launcher and engine are licensed under the GPL-2.0 (see [LICENSE](LICENSE)). The bundled KosmicKrisp driver is MIT licensed (Mesa), and the Vulkan loader is Apache-2.0.
