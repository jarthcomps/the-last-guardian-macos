// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

namespace VideoCore {

/// Whether RefreshImage should hash guest memory before re-uploading a mip of a GPU-modified
/// image. Upstream hashes so an unchanged CPU copy does not overwrite what the GPU rendered into
/// the image. When the buffer cache holds newer GPU data for the same memory (a compute shader
/// wrote it), the guest memory is stale: hashing it faults, the command processor waits for a
/// readback, and the upload that follows copies from the GPU buffer anyway. With
/// SHADPS4_IMAGE_UPLOAD_SKIP_HASH=1 that hash (and the readback) is skipped and the mip is
/// re-uploaded from the GPU buffer.
constexpr bool ShouldHashBeforeUpload(bool image_gpu_modified, bool image_gpu_dirty,
                                      bool memory_gpu_modified, bool skip_enabled) noexcept {
    if (!image_gpu_modified || image_gpu_dirty) {
        return false; // upstream: no hash needed
    }
    return !(skip_enabled && memory_gpu_modified);
}

/// Whether the MaybeCpuDirty probe (hash of the first pixels) may read guest memory.
constexpr bool ShouldProbeMaybeCpuDirty(bool memory_gpu_modified, bool skip_enabled) noexcept {
    return !(skip_enabled && memory_gpu_modified);
}

} // namespace VideoCore
