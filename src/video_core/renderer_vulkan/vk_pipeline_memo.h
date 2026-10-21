// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

namespace Vulkan {

/// Remembers the value of the last lookup that found something, together with the key it was
/// found with. A lookup with an equal key returns the same value again without searching the map
/// the value came from. The owner calls Invalidate whenever an entry the value points to can be
/// erased from that map. A null value remembers nothing.
template <typename Key, typename Value>
class PipelineLookupMemo {
public:
    /// The remembered value when `requested` equals the remembered key, else nullptr.
    [[nodiscard]] Value* Find(const Key& requested) const {
        return value && key == requested ? value : nullptr;
    }

    /// Remembers `found` as the result of `requested`. The key is copied.
    void Store(const Key& requested, Value* found) {
        key = requested;
        value = found;
    }

    void Invalidate() {
        value = nullptr;
    }

private:
    Key key{};
    Value* value{};
};

} // namespace Vulkan
