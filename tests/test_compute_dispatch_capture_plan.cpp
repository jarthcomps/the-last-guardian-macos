#include <cassert>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <set>
#include <vector>

#include "video_core/renderer_vulkan/compute_dispatch_capture_plan.h"

using namespace Vulkan::ComputeCapture;

int main() {
    {
        // One shared list drives both the capture hook and the replacement-kernel gate; it
        // must hold the 22 hottest outdoor compute shaders, each once.
        const std::set<std::uint64_t> unique(CaptureTargetHashes.begin(), CaptureTargetHashes.end());
        assert(unique.size() == CaptureTargetHashes.size());
        assert(CaptureTargetHashes.size() == 22);
        for (const std::uint64_t hash : {0x1171c360ULL, 0xa912f7d4ULL, 0x9878bcc6ULL, 0x1d99a308ULL}) {
            assert(std::ranges::find(CaptureTargetHashes, hash) != CaptureTargetHashes.end());
        }
    }
    {
        const auto valid = EvaluateTimestampInterval(100, 120, 0.5);
        assert(valid.state == TimestampIntervalState::Valid);
        assert(valid.duration_ns == 10.0);

        const auto unsupported = EvaluateTimestampInterval(100, 120, 0.0);
        assert(unsupported.state == TimestampIntervalState::UnsupportedPeriod);
        assert(unsupported.duration_ns == 0.0);

        const auto empty = EvaluateTimestampInterval(100, 100, 1.0);
        assert(empty.state == TimestampIntervalState::Empty);
        assert(empty.duration_ns == 0.0);

        const auto wrapped = EvaluateTimestampInterval(120, 100, 1.0);
        assert(wrapped.state == TimestampIntervalState::NonMonotonic);
        assert(wrapped.duration_ns == 0.0);
        assert(TimestampIntervalReason(wrapped.state).find("wrapped") != std::string_view::npos);
    }
    {
        constexpr std::uint64_t targets[]{0x1d99a308, 0xa3c4525b};
        assert(CaptureTargetAllowed(targets, 0x1d99a308));
        assert(CaptureTargetAllowed(targets, 0x1d99a308, "1d99a308"));
        assert(CaptureTargetAllowed(targets, 0x1d99a308, "1D99A308"));
        assert(!CaptureTargetAllowed(targets, 0xa3c4525b, "1d99a308"));
        assert(!CaptureTargetAllowed(targets, 0x1171c360));
        assert(!CaptureTargetAllowed(targets, 0x1d99a308, "1d99a30g"));
        assert(!CaptureTargetAllowed(targets, 0x1d99a308, "1d99a3080"));
        // Comma-separated selections.
        assert(CaptureTargetAllowed(targets, 0xa3c4525b, "1d99a308,a3c4525b"));
        assert(CaptureTargetAllowed(targets, 0x1d99a308, "1d99a308,a3c4525b"));
        assert(!CaptureTargetAllowed(targets, 0x1171c360, "1d99a308,1171c360"));
        assert(!CaptureTargetAllowed(targets, 0xa3c4525b, "1d99a308,a3c452"));
    }
    {
        const auto directory = std::filesystem::temp_directory_path() /
            ("shadps4-compute-capture-arm-" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(directory);
        assert(!IsCaptureArmed(directory));
        std::filesystem::create_directories(directory / CaptureArmMarkerName);
        assert(!IsCaptureArmed(directory));
        std::filesystem::remove_all(directory / CaptureArmMarkerName);
        std::ofstream(directory / CaptureArmMarkerName) << "ready\n";
        assert(IsCaptureArmed(directory));
        std::filesystem::remove_all(directory);
    }
    {
        const auto plan = BuildAliasPlan({
            {.binding = 2, .guest_begin = 0x1000, .guest_size = 0x100, .source_identity = 7,
             .source_offset = 0x2000, .descriptor_range = 0x100, .written = true},
            {.binding = 3, .guest_begin = 0x1080, .guest_size = 0x100, .source_identity = 7,
             .source_offset = 0x2080, .descriptor_range = 0x100},
        });
        assert(plan.supported);
        assert(plan.groups.size() == 1);
        assert(plan.groups[0].source_offset == 0x2000);
        assert(plan.groups[0].size == 0x180);
        assert(plan.groups[0].written);
        assert(plan.group_for_binding[0] == plan.group_for_binding[1]);
        assert(plan.clone_offset_for_binding[0] == 0);
        assert(plan.clone_offset_for_binding[1] == 0x80);
    }
    {
        const auto plan = BuildAliasPlan({
            {.binding = 0, .guest_begin = 0x1000, .guest_size = 0x80, .guest_offset = 0x10,
             .source_identity = 9, .source_offset = 0x2000, .descriptor_range = 0x90},
            {.binding = 1, .guest_begin = 0x1010, .guest_size = 0x80,
             .source_identity = 9, .source_offset = 0x2020, .descriptor_range = 0x80},
        });
        assert(plan.supported);
        assert(plan.groups.size() == 1);
        assert(plan.clone_offset_for_binding[0] == 0);
        assert(plan.clone_offset_for_binding[1] == 0x20);
    }
    {
        const auto plan = BuildAliasPlan({
            {.binding = 1, .guest_begin = 0x1000, .guest_size = 0x80, .source_identity = 4,
             .source_offset = 0x800, .descriptor_range = 0x80},
            {.binding = 2, .guest_begin = 0x2000, .guest_size = 0x80, .source_identity = 4,
             .source_offset = 0x1800, .descriptor_range = 0x80},
            {.binding = 9, .guest_begin = 0, .guest_size = 0, .source_identity = 0,
             .source_offset = 0, .descriptor_range = 0xA0, .guest_backed = false},
        });
        assert(plan.supported);
        assert(plan.groups.size() == 2);
        assert(plan.clone_bytes == 0x100);
        assert(plan.group_for_binding[2] == UINT32_MAX);
    }
    {
        const auto plan = BuildAliasPlan({
            {.binding = 0, .guest_begin = 0x1000, .guest_size = 0x100, .source_identity = 1,
             .source_offset = 0x2000, .descriptor_range = 0x100},
            {.binding = 1, .guest_begin = 0x1080, .guest_size = 0x100, .source_identity = 2,
             .source_offset = 0x2080, .descriptor_range = 0x100},
        });
        assert(!plan.supported);
        assert(plan.rejection_reason == "overlapping guest ranges use different backing buffers");
    }
    {
        const auto plan = BuildAliasPlan({
            {.binding = 0, .guest_begin = 0x1000, .guest_size = 0x100, .source_identity = 1,
             .source_offset = 0x2000, .descriptor_range = 0x100},
            {.binding = 1, .guest_begin = 0x1080, .guest_size = 0x100, .source_identity = 1,
             .source_offset = 0x2084, .descriptor_range = 0x100},
        });
        assert(!plan.supported);
        assert(plan.rejection_reason == "overlapping guest ranges have inconsistent backing offsets");
    }
    {
        const auto plan = BuildAliasPlan({
            {.binding = 0, .guest_begin = 0x1000, .guest_size = 0x80, .source_identity = 3,
             .source_offset = 0x2000, .descriptor_range = 0x100},
            {.binding = 1, .guest_begin = 0x1080, .guest_size = 0x80, .source_identity = 3,
             .source_offset = 0x2080, .descriptor_range = 0x80},
        });
        assert(plan.supported);
        assert(plan.groups.size() == 1);
        assert(plan.groups[0].source_offset == 0x2000);
        assert(plan.groups[0].size == 0x100);
        assert(plan.clone_offset_for_binding[1] == 0x80);
    }
    {
        const auto plan = BuildAliasPlan({
            {.binding = 7, .guest_begin = 0x3000, .guest_size = 0x100, .source_identity = 8,
             .source_offset = 0x2100, .descriptor_range = 0x100},
            {.binding = 2, .guest_begin = 0x1000, .guest_size = 0x100, .source_identity = 8,
             .source_offset = 0x2000, .descriptor_range = 0x100},
            {.binding = 5, .guest_begin = 0x2000, .guest_size = 0x100, .source_identity = 8,
             .source_offset = 0x2080, .descriptor_range = 0x100},
        });
        assert(plan.supported);
        assert(plan.groups.size() == 1);
        assert(plan.groups[0].size == 0x200);
        assert(plan.group_for_binding[0] == plan.group_for_binding[1]);
        assert(plan.group_for_binding[1] == plan.group_for_binding[2]);
        assert(plan.clone_offset_for_binding[1] == 0);
        assert(plan.clone_offset_for_binding[2] == 0x80);
        assert(plan.clone_offset_for_binding[0] == 0x100);
    }
    {
        const auto plan = BuildAliasPlan({
            {.binding = 0, .guest_begin = 0x1000, .guest_size = 0x100, .source_identity = 1,
             .source_offset = 0x2000, .descriptor_range = 0x100},
        }, 0xFF);
        assert(!plan.supported);
        assert(plan.rejection_reason == "clone byte budget exceeded");
        assert(WithinAggregateBudget(MaxAggregateCaptureBytes - 16, 16));
        assert(!WithinAggregateBudget(MaxAggregateCaptureBytes - 15, 16));
    }
    {
        const auto plan = BuildAliasPlan({
            {.binding = 0, .guest_begin = 0x1000, .guest_size = 0x10,
             .source_identity = 1, .source_offset = 0, .descriptor_range = 0x10},
            {.binding = 1, .guest_begin = UINT64_MAX - 3, .guest_size = 8,
             .source_identity = 1, .source_offset = 0x100, .descriptor_range = 8},
        });
        assert(!plan.supported);
        assert(plan.rejection_reason == "invalid descriptor interval");
    }
    {
        std::uint64_t differences{};
        assert(OutputsEqual({1, 2, 3}, {1, 2, 3}, differences));
        assert(differences == 0);
        assert(!OutputsEqual({1, 0, 3}, {1, 2, 3}, differences));
        assert(differences == 1);
        assert(!OutputsEqual({1}, {1, 2}, differences));
        assert(differences == 1);
    }
}
