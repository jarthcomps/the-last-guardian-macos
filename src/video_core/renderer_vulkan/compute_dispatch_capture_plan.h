#pragma once

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace Vulkan::ComputeCapture {

inline constexpr std::uint64_t MaxCloneBytes = 512ULL * 1024 * 1024;
inline constexpr std::uint64_t MaxAggregateCaptureBytes = 2ULL * 1024 * 1024 * 1024;
inline constexpr std::string_view CaptureArmMarkerName = "capture.armed";

enum class TimestampIntervalState {
    Valid,
    UnsupportedPeriod,
    Empty,
    NonMonotonic,
    InvalidDuration,
};

struct TimestampInterval {
    TimestampIntervalState state{};
    double duration_ns{};
};

[[nodiscard]] inline TimestampInterval EvaluateTimestampInterval(std::uint64_t start,
                                                                  std::uint64_t end,
                                                                  double period_ns) {
    if (!std::isfinite(period_ns) || period_ns <= 0.0) {
        return {TimestampIntervalState::UnsupportedPeriod, 0.0};
    }
    if (end == start) {
        return {TimestampIntervalState::Empty, 0.0};
    }
    if (end < start) {
        return {TimestampIntervalState::NonMonotonic, 0.0};
    }
    const double duration = static_cast<double>(end - start) * period_ns;
    if (!std::isfinite(duration) || duration <= 0.0 ||
        duration > static_cast<double>(std::numeric_limits<std::uint64_t>::max())) {
        return {TimestampIntervalState::InvalidDuration, 0.0};
    }
    return {TimestampIntervalState::Valid, duration};
}

[[nodiscard]] inline std::string_view TimestampIntervalReason(TimestampIntervalState state) {
    switch (state) {
    case TimestampIntervalState::Valid:
        return {};
    case TimestampIntervalState::UnsupportedPeriod:
        return "timestamp period is unavailable";
    case TimestampIntervalState::Empty:
        return "GPU timestamp interval is empty";
    case TimestampIntervalState::NonMonotonic:
        return "GPU timestamp interval is non-monotonic or wrapped";
    case TimestampIntervalState::InvalidDuration:
        return "GPU timestamp interval duration is invalid";
    }
    return "GPU timestamp interval state is unknown";
}

[[nodiscard]] inline bool IsCaptureArmed(const std::filesystem::path& directory) {
    std::error_code error;
    return std::filesystem::is_regular_file(directory / CaptureArmMarkerName, error) && !error;
}

/// Capture is on only when the SHADPS4_CAPTURE_COMPUTE_DIR directory is set and armed. An empty
/// (unset) directory is never looked up on disk.
[[nodiscard]] inline bool IsCaptureEnabled(const std::filesystem::path& directory) {
    return !directory.empty() && IsCaptureArmed(directory);
}

/// SHADPS4_CAPTURE_COMPUTE_LIMIT: captures per target, from 1 to 8 (1 when unset or empty).
[[nodiscard]] inline std::uint32_t CaptureCountLimit(const char* value) {
    if (!value || !*value) {
        return 1U;
    }
    const long parsed = std::strtol(value, nullptr, 10);
    return static_cast<std::uint32_t>(std::clamp(parsed, 1L, 8L));
}

/// Same check for a directory given as text (an empty view means unset).
[[nodiscard]] inline bool CaptureEnabledIn(std::string_view directory) {
    return IsCaptureEnabled(std::filesystem::path{directory});
}

/// The busiest compute shaders outdoors (Metal traces, 2026-10-20), for the replay benchmark.
inline constexpr std::array<std::uint64_t, 22> CaptureTargetHashes{
    0x1d99a308, 0xa3c4525b, 0x1171c360, 0xee29a0b1, 0xeb218825, 0xa912f7d4,
    0xd3bcee20, 0x9c667237, 0x7a83ef95, 0x8da019a4, 0xcdb2ad36, 0x08d09057,
    0xe15f3156, 0x82c47ba4, 0x9369a7f3, 0x96d32d22, 0x3cc5f11c, 0x81fa8341,
    0xbdcd8c92, 0x4a263042, 0x01a5ccff, 0x9878bcc6,
};

[[nodiscard]] inline bool CaptureTargetAllowed(std::span<const std::uint64_t> targets,
                                               std::uint64_t guest_hash,
                                               std::string_view selected_hash = {}) {
    if (std::find(targets.begin(), targets.end(), guest_hash) == targets.end()) {
        return false;
    }
    if (selected_hash.empty()) {
        return true;
    }
    // One hash, or several separated by commas (one capture run for many shaders).
    while (!selected_hash.empty()) {
        const auto comma = selected_hash.find(',');
        const auto item = selected_hash.substr(0, comma);
        selected_hash = comma == std::string_view::npos ? std::string_view{}
                                                        : selected_hash.substr(comma + 1);
        if (item.size() != 8) {
            return false;
        }
        std::uint32_t parsed{};
        const auto [end, error] =
            std::from_chars(item.data(), item.data() + item.size(), parsed, 16);
        if (error != std::errc{} || end != item.data() + item.size()) {
            return false;
        }
        if (parsed == guest_hash) {
            return true;
        }
    }
    return false;
}

struct DescriptorRange {
    std::uint32_t binding{};
    std::uint64_t guest_begin{};
    std::uint64_t guest_size{};
    std::uint64_t guest_offset{};
    std::uint64_t source_identity{};
    std::uint64_t source_offset{};
    std::uint64_t descriptor_range{};
    bool guest_backed{true};
    bool written{};
};

struct AliasGroup {
    std::uint64_t source_identity{};
    std::uint64_t source_offset{};
    std::uint64_t size{};
    std::vector<std::uint32_t> bindings;
    bool written{};
};

struct AliasPlan {
    bool supported{};
    std::string rejection_reason;
    std::uint64_t clone_bytes{};
    std::vector<AliasGroup> groups;
    std::vector<std::uint32_t> group_for_binding;
    std::vector<std::uint64_t> clone_offset_for_binding;
};

[[nodiscard]] inline AliasPlan BuildAliasPlan(const std::vector<DescriptorRange>& ranges,
                                               std::uint64_t clone_limit = MaxCloneBytes) {
    AliasPlan plan{};
    plan.group_for_binding.resize(ranges.size(), UINT32_MAX);
    plan.clone_offset_for_binding.resize(ranges.size());

    for (const auto& range : ranges) {
        if (!range.guest_backed) {
            continue;
        }
        if (range.guest_size == 0 || range.descriptor_range == 0 ||
            range.guest_begin > UINT64_MAX - range.guest_size ||
            range.source_offset > UINT64_MAX - range.descriptor_range ||
            range.source_offset > UINT64_MAX - range.guest_offset ||
            range.guest_offset > range.descriptor_range ||
            range.guest_size > range.descriptor_range - range.guest_offset) {
            plan.rejection_reason = "invalid descriptor interval";
            return plan;
        }
    }

    for (std::size_t i = 0; i < ranges.size(); ++i) {
        const auto& lhs = ranges[i];
        if (!lhs.guest_backed) {
            continue;
        }
        for (std::size_t j = i + 1; j < ranges.size(); ++j) {
            const auto& rhs = ranges[j];
            if (!rhs.guest_backed || rhs.guest_size == 0) {
                continue;
            }
            const std::uint64_t lhs_end = lhs.guest_begin + lhs.guest_size;
            const std::uint64_t rhs_end = rhs.guest_begin + rhs.guest_size;
            if (lhs.guest_begin >= rhs_end || rhs.guest_begin >= lhs_end) {
                continue;
            }
            if (lhs.source_identity != rhs.source_identity) {
                plan.rejection_reason = "overlapping guest ranges use different backing buffers";
                return plan;
            }
            const auto overlap_begin = std::max(lhs.guest_begin, rhs.guest_begin);
            const auto lhs_at_overlap =
                lhs.source_offset + lhs.guest_offset + (overlap_begin - lhs.guest_begin);
            const auto rhs_at_overlap =
                rhs.source_offset + rhs.guest_offset + (overlap_begin - rhs.guest_begin);
            if (lhs_at_overlap != rhs_at_overlap) {
                plan.rejection_reason = "overlapping guest ranges have inconsistent backing offsets";
                return plan;
            }
            const auto lhs_source_end = lhs.source_offset + lhs.descriptor_range;
            const auto rhs_source_end = rhs.source_offset + rhs.descriptor_range;
            if (lhs_at_overlap >= lhs_source_end || lhs_at_overlap >= rhs_source_end) {
                plan.rejection_reason = "overlapping guest ranges do not overlap in the bound buffers";
                return plan;
            }
        }
    }

    std::vector<bool> assigned(ranges.size());
    for (std::size_t i = 0; i < ranges.size(); ++i) {
        const auto& first = ranges[i];
        if (!first.guest_backed || assigned[i]) {
            continue;
        }
        std::uint64_t source_begin = first.source_offset;
        std::uint64_t source_end = first.source_offset + first.descriptor_range;
        std::vector<std::size_t> component{i};
        assigned[i] = true;

        bool expanded = true;
        while (expanded) {
            expanded = false;
            for (std::size_t j = 0; j < ranges.size(); ++j) {
                const auto& candidate = ranges[j];
                if (!candidate.guest_backed || assigned[j] ||
                    candidate.source_identity != first.source_identity) {
                    continue;
                }
                const auto candidate_end = candidate.source_offset + candidate.descriptor_range;
                const auto group_source_end = source_end;
                if (candidate.source_offset >= group_source_end || source_begin >= candidate_end) {
                    continue;
                }
                assigned[j] = true;
                component.push_back(j);
                source_begin = std::min(source_begin, candidate.source_offset);
                source_end = std::max(source_end, candidate_end);
                expanded = true;
            }
        }

        if (source_end < source_begin || source_end - source_begin > clone_limit - plan.clone_bytes) {
            plan.rejection_reason = "clone byte budget exceeded";
            return plan;
        }
        const auto group_index = static_cast<std::uint32_t>(plan.groups.size());
        AliasGroup group{.source_identity = first.source_identity,
                         .source_offset = source_begin,
                         .size = source_end - source_begin};
        for (const auto index : component) {
            group.bindings.push_back(ranges[index].binding);
            group.written |= ranges[index].written;
            plan.group_for_binding[index] = group_index;
            plan.clone_offset_for_binding[index] = ranges[index].source_offset - source_begin;
        }
        plan.clone_bytes += group.size;
        plan.groups.push_back(std::move(group));
    }

    plan.supported = true;
    return plan;
}

[[nodiscard]] inline bool WithinAggregateBudget(std::uint64_t already_captured,
                                                std::uint64_t next_capture) {
    return next_capture <= MaxAggregateCaptureBytes &&
           already_captured <= MaxAggregateCaptureBytes - next_capture;
}

[[nodiscard]] inline bool OutputsEqual(const std::vector<std::uint8_t>& lhs,
                                       const std::vector<std::uint8_t>& rhs,
                                       std::uint64_t& differing_bytes) {
    differing_bytes = 0;
    const auto common = std::min(lhs.size(), rhs.size());
    for (std::size_t i = 0; i < common; ++i) {
        differing_bytes += lhs[i] != rhs[i];
    }
    differing_bytes += std::max(lhs.size(), rhs.size()) - common;
    return differing_bytes == 0;
}

} // namespace Vulkan::ComputeCapture
