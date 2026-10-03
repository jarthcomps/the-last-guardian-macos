<!--
SPDX-FileCopyrightText: 2026 shadPS4 Emulator Project
SPDX-License-Identifier: GPL-2.0-or-later
-->

# TLG settings reference

Every setting of the `tlg-encoder-thread` branch, with all values, what they do, and what the
branch uses for The Last Guardian. They go in `user/custom_configs/CUSA03627.readback.json`
(`%APPDATA%\shadPS4\custom_configs\` with a normal install), inside a `"GPU"` object. A missing
key means the default. JSON has no comments: a commented copy of this file to copy values from is
`CUSA03627.readback.commented.jsonc` next to the config.

Changes only apply when the game starts. All of them are off (or upstream behavior) by default.

## Game settings (in `CUSA03627.json`, `GPU` section)

| Setting | Values | What it does |
| :--- | :--- | :--- |
| `readbacks_mode` | 0 = Disabled, 1 = Relaxed, 2 = Precise | How GPU results are copied back to the game's memory. Precise is accurate and needed for Trico's feathers, climbing, the spear and lighting. Relaxed skips read protection (faster, less accurate). Disabled is fastest and breaks the game. **Use 2.** |
| `readback_linear_images_enabled` | true / false | Also reads back linear (untiled) images, one frame late. Needed for some water and lighting. **Use true.** |
| `copy_gpu_buffers`, `direct_memory_access_enabled` | true / false | Upstream options this branch doesn't tune. Leave off. |

## Readback tuning

| Setting | Values (default) | What it does |
| :--- | :--- | :--- |
| `readback_async_fences_enabled` | true / false (false) | The game's "GPU work done" signals are delivered by a background thread after the readbacks are back, so the command processor doesn't stop to wait. **Used: true.** |
| `readback_batching_enabled` | true / false (false) | Several readbacks waiting on the same GPU work share one wait. **Used: true.** |
| `readback_fence_wait_shortcut` | 0, 1, 2, 3 (1) | With async fences: lets waits in the command stream pass for fence values still held back. 0 = off, 1 = graphics queue only, 2 = graphics and compute queues, 3 = like 2 plus a GPU barrier after each wait that passed early. **Used: 3.** |
| `readback_async_guest_faults` | true / false (false) | When a game thread touches memory the GPU wrote, the command processor starts the copy and goes on; only the game thread waits. **Used: true.** |
| `readback_prefetch_mode` | 0, 1, 2 (1) | Buffer ranges the CPU faulted on are copied back in advance at fences where the GPU wrote them again. 0 = never, 1 = ranges that faulted on a read or a write, 2 = only on a read (less work, about twice the waiting). **Used: 1.** |
| `readback_prefetch_lifetime` | number of fences (600) | How many fences a prefetched range stays hot after the game last touched it. Lower = fewer downloads. **Used: 60** (about 1.4 s). |
| `readback_hot_write_pages_enabled` | true / false (false) | Pages the game rewrites right after nearly every upload stay writable. Fewer write faults, but more upload work on the command processor. Measured worse. **Used: false.** |

## Reuse of earlier work

| Setting | Values (default) | What it does |
| :--- | :--- | :--- |
| `readback_stream_reuse_enabled` | true / false (false) | Small read-only buffers copied again in the same command buffer reuse the earlier copy (about 70% of those copies). **Used: true.** |
| `rt_lookup_reuse_enabled` | true / false (false) | A draw whose render target registers equal the previous draw's reuses its target images instead of searching the texture cache. **Used: true.** |
| `stage_lookup_reuse` | 0, 1, 2 (0) | Shader lookups first check the variant picked last time. 0 = off, 1 = on, 2 = verify: both paths run and differences are logged (slow, for checking). **Used: 1.** |
| `shader_backing_reads`, `shader_backing_verify` | true / false | Disabled in code (it crashed and drew a wrong shader). Ignored whatever you set. |

## Encoder thread

| Setting | Values (default) | What it does |
| :--- | :--- | :--- |
| `cp_encoder_mode` | 0, 1, 2, 3 (0) | 0 = record Vulkan commands directly. 1 = record into lists replayed on the same thread. 2 = replay and submit on a second (encoder) thread. 3 = like 2 but waiting for each submission (diagnostic, slow). **Used: 2.** |
| `cp_full_fence_kinds` | bit mask 0-31 (31) | Which kinds of game fences wait for all earlier work before they fire: 1 = graphics end-of-pipe, 2 = graphics end-of-shader, 4 = compute release, 8 = label writes held behind a fence, 16 = end of a guest submission. Add the numbers to combine (31 = all). 0 makes Trico's feathers flicker when pulling the spear out; 4, 27 and 31 were clean. **Used: 31** (safest; 4 is the cheapest value tested clean, but flicker was reported with it). |
| `cp_encoder_queue_depth` | 1 or more (8) | How many recorded command lists the command processor may hand to the encoder before it waits for it. **Used: 32.** |
| `cp_thread_priority` | 0, 1, 2, 3 (0) | Priority of the command processor and encoder threads. 0 or 1 = default, 2 = above normal, 3 = highest. Steadier frame times, no FPS gain. **Used: 2.** |
| `cp_encoder_debug` | bit mask (0) | Diagnostics: 1 = wait for the encoder at every fence, 2 = at the end of every guest submission, 4 = fences without readbacks wait for all earlier work (always on with the encoder thread). **Used: 0.** |

## Tools (leave off for normal play)

| Setting | Values (default) | What it does |
| :--- | :--- | :--- |
| `readback_stats_enabled` | true / false (false) | The profiler: writes `log/readback_stats.csv` and `log/readback_top.txt` once per second. Costs a lot of FPS. |
| `cp_sampler_hz` | 0 or samples per second (0) | Sampling profiler (Windows): every 1/hz second it briefly stops the command processor, the encoder and the game's render, main and job threads and writes their call stacks to `log/cp_samples.txt`. 200 to 500 is typical. Costs some FPS. |

## Testing the red and green shadow tint

The tint in one area appears after playing for a while and goes away when the game is restarted, so
something is holding on to old state. Suspects in order: the three "reuse" settings
(`readback_stream_reuse_enabled`, `rt_lookup_reuse_enabled`, `stage_lookup_reuse`), then
`readback_prefetch_mode`, then the readback tuning as a group, then the encoder thread group. Turn
off one group at a time, play until the tint normally shows, and note the result.
