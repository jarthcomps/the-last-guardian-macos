// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "video_core/texture_cache/image_upload_policy.h"

using VideoCore::ShouldHashBeforeUpload;
using VideoCore::ShouldProbeMaybeCpuDirty;

// image_gpu_modified, image_gpu_dirty, memory_gpu_modified, skip_enabled
static_assert(!ShouldHashBeforeUpload(false, false, false, false), "plain CPU image: no hash");
static_assert(!ShouldHashBeforeUpload(true, true, false, false), "GPU dirty: no hash");
static_assert(ShouldHashBeforeUpload(true, false, false, false), "upstream hashes");
static_assert(ShouldHashBeforeUpload(true, false, true, false), "upstream hashes, switch off");
static_assert(ShouldHashBeforeUpload(true, false, false, true), "CPU memory current: hash");
static_assert(!ShouldHashBeforeUpload(true, false, true, true), "GPU buffer newer: skip");
static_assert(ShouldProbeMaybeCpuDirty(true, false));
static_assert(ShouldProbeMaybeCpuDirty(false, true));
static_assert(!ShouldProbeMaybeCpuDirty(true, true));

int main() {
    return 0;
}
