// SPDX-FileCopyrightText: Copyright 2024-2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <algorithm>
#include <deque>
#include <cstdlib>
#include <string_view>
#include <mutex>
#include <type_traits>
#include <vector>

#include "common/debug.h"
#include "common/env_switch.h"
#include "common/types.h"
#include "core/emulator_settings.h"
#include "video_core/buffer_cache/region_manager.h"
#include "video_core/readback_stats.h"
#include "video_core/frame_path_stats.h"

namespace VideoCore {

class MemoryTracker {
public:
    static constexpr size_t MAX_CPU_PAGE_BITS = 40;
    static constexpr size_t NUM_HIGH_PAGES = 1ULL << (MAX_CPU_PAGE_BITS - TRACKER_HIGHER_PAGE_BITS);
    static constexpr size_t MANAGER_POOL_SIZE = 32;

public:
    explicit MemoryTracker(PageManager& tracker_)
        : tracker{&tracker_},
          // SHADPS4_HOT_WRITE_PAGES=1|0 overrides the config (launcher switch).
          hot_pages_enabled{Common::EnvOverrideNamed(
              "SHADPS4_HOT_WRITE_PAGES", EmulatorSettings.IsReadbackHotWritePagesEnabled())},
          range_protection_enabled{[] {
              const char* value = std::getenv("SHADPS4_RANGE_PROTECTION");
              return value && std::string_view{value} == "1";
          }()} {}

    /// Forgets hot pages everywhere (see RegionManager::DecayHotPages).
    void DecayHotPages() {
        if (!hot_pages_enabled) {
            return;
        }
        for (auto& pool : manager_pool) {
            for (auto& manager : pool) {
                std::scoped_lock lk{manager.lock};
                manager.DecayHotPages();
            }
        }
    }
    ~MemoryTracker() = default;

    /// Hot page shadows (SHADPS4_HOT_PAGE_COMPARE). Command processor thread only.
    [[nodiscard]] HotShadow& HotShadowAt(const HotPageRef& ref) {
        return ref.manager->ShadowAt(ref.page);
    }

    [[nodiscard]] HotShadowLedger& HotLedger() noexcept {
        return hot_shadow_ledger;
    }

    /// Drops the data of the shadows of a range that was written in a buffer.
    void ForgetHotShadows(VAddr cpu_addr, u64 size) {
        IteratePages<false>(cpu_addr, size,
                            [](RegionManager* manager, u64 offset, size_t range_size) {
                                manager->ForgetShadows(manager->GetCpuAddr() + offset, range_size);
                            });
    }

    /// Drops every shadow (decay).
    void ClearHotShadows() {
        for (auto& pool : manager_pool) {
            for (auto& manager : pool) {
                manager.ClearShadows();
            }
        }
    }

    /// Returns true if a region has been modified from the CPU
    bool IsRegionCpuModified(VAddr query_cpu_addr, u64 query_size) noexcept {
        return IteratePages<true>(
            query_cpu_addr, query_size, [](RegionManager* manager, u64 offset, size_t size) {
                return manager->template IsRegionModified<Type::CPU>(offset, size);
            });
    }

    /// Returns true if a region has been modified from the GPU
    bool IsRegionGpuModified(VAddr query_cpu_addr, u64 query_size) noexcept {
        return IteratePages<false>(
            query_cpu_addr, query_size, [](RegionManager* manager, u64 offset, size_t size) {
                return manager->template IsRegionModified<Type::GPU>(offset, size);
            });
    }

    /// Returns true if a CPU modified page lies in the range, so a read upload of it copies data.
    /// Each region is checked under its lock and created where missing, as ForEachUploadRange
    /// does; when this returns false that upload copies nothing and changes no tracking state.
    [[nodiscard]] bool NeedsUpload(VAddr query_cpu_range, u64 query_size) {
        return IteratePages<true>(query_cpu_range, query_size,
                                  [](RegionManager* manager, u64 offset, size_t size) {
                                      std::scoped_lock lk{manager->lock};
                                      return manager->template IsRegionModified<Type::CPU>(
                                          offset, size);
                                  });
    }

    /// Mark region as CPU modified, notifying the device_tracker about this change
    void MarkRegionAsCpuModified(VAddr dirty_cpu_addr, u64 query_size) {
        IteratePages<false>(dirty_cpu_addr, query_size,
                            [](RegionManager* manager, u64 offset, size_t size) {
                                std::scoped_lock lk{manager->lock};
                                manager->template ChangeRegionState<Type::CPU, true>(
                                    manager->GetCpuAddr() + offset, size);
                            });
    }

    /// Unmark region as modified from the host GPU
    void UnmarkRegionAsGpuModified(VAddr dirty_cpu_addr, u64 query_size) noexcept {
        IteratePages<false>(dirty_cpu_addr, query_size,
                            [](RegionManager* manager, u64 offset, size_t size) {
                                std::scoped_lock lk{manager->lock};
                                manager->template ChangeRegionState<Type::GPU, false>(
                                    manager->GetCpuAddr() + offset, size);
                            });
    }

    /// Removes all protection from a page and ensures GPU data has been flushed if requested
    void InvalidateRegion(VAddr cpu_addr, u64 size, auto&& on_flush) noexcept {
        IteratePages<false>(
            cpu_addr, size, [&on_flush](RegionManager* manager, u64 offset, size_t size) {
                const bool should_flush = [&] {
                    // Perform both the GPU modification check and CPU state change with the lock
                    // in case we are racing with GPU thread trying to mark the page as GPU
                    // modified. If we need to flush the flush function is going to perform CPU
                    // state change.
                    std::scoped_lock lk{manager->lock};
                    if (EmulatorSettings.GetReadbacksMode() != GpuReadbacksMode::Disabled &&
                        manager->template IsRegionModified<Type::GPU>(offset, size)) {
                        return true;
                    }
                    if (const u32 new_hot =
                            manager->NoteCpuWrite(manager->GetCpuAddr() + offset, size)) {
                        ReadbackStats::OnHotPages(new_hot);
                        if (FramePathStats::Enabled()) {
                            FramePathStats::Global().new_hot_pages.fetch_add(
                                new_hot, std::memory_order_relaxed);
                        }
                    }
                    manager->template ChangeRegionState<Type::CPU, true>(
                        manager->GetCpuAddr() + offset, size);
                    return false;
                }();
                if (should_flush) {
                    on_flush();
                }
            });
    }

    /// Call 'func' for each CPU modified range and unmark those pages as CPU modified. With hot_out
    /// (reads only), hot pages are not passed to func: they are appended to hot_out instead.
    void ForEachUploadRange(VAddr query_cpu_range, u64 query_size, bool is_written, auto&& func,
                            auto&& on_upload, std::vector<HotPageRef>* hot_out = nullptr) {
        IteratePages<true>(query_cpu_range, query_size,
                           [&func, is_written, hot_out](RegionManager* manager, u64 offset,
                                                        size_t size) {
                               manager->lock.lock();
                               manager->template ForEachModifiedRange<Type::CPU, true>(
                                   manager->GetCpuAddr() + offset, size, func, !is_written,
                                   hot_out);
                               if (!is_written) {
                                   manager->lock.unlock();
                               }
                           });
        on_upload();
        if (!is_written) {
            return;
        }
        IteratePages<false>(query_cpu_range, query_size,
                            [&func, is_written](RegionManager* manager, u64 offset, size_t size) {
                                manager->template ChangeRegionState<Type::GPU, true>(
                                    manager->GetCpuAddr() + offset, size);
                                manager->lock.unlock();
                            });
    }

    /// Call 'func' for each GPU modified range and unmark those pages as GPU modified
    template <bool clear>
    void ForEachDownloadRange(VAddr query_cpu_range, u64 query_size, auto&& func) {
        IteratePages<false>(query_cpu_range, query_size,
                            [&func](RegionManager* manager, u64 offset, size_t size) {
                                std::scoped_lock lk{manager->lock};
                                manager->template ForEachModifiedRange<Type::GPU, clear>(
                                    manager->GetCpuAddr() + offset, size, func);
                            });
    }

private:
    /**
     * @brief IteratePages Iterates L2 word manager page table.
     * @param cpu_address Start byte cpu address
     * @param size Size in bytes of the region of iterate.
     * @param func Callback for each word manager.
     * @return
     */
    template <bool create_region_on_fail, typename Func>
    bool IteratePages(VAddr cpu_address, size_t size, Func&& func) {
        using FuncReturn = typename std::invoke_result<Func, RegionManager*, u64, size_t>::type;
        static constexpr bool BOOL_BREAK = std::is_same_v<FuncReturn, bool>;
        std::size_t remaining_size{size};
        std::size_t page_index{cpu_address >> TRACKER_HIGHER_PAGE_BITS};
        u64 page_offset{cpu_address & TRACKER_HIGHER_PAGE_MASK};
        while (remaining_size > 0) {
            const std::size_t copy_amount{
                std::min<std::size_t>(TRACKER_HIGHER_PAGE_SIZE - page_offset, remaining_size)};
            auto* manager{top_tier[page_index]};
            if (manager) {
                if constexpr (BOOL_BREAK) {
                    if (func(manager, page_offset, copy_amount)) {
                        return true;
                    }
                } else {
                    func(manager, page_offset, copy_amount);
                }
            } else if constexpr (create_region_on_fail) {
                CreateRegion(page_index);
                manager = top_tier[page_index];
                if constexpr (BOOL_BREAK) {
                    if (func(manager, page_offset, copy_amount)) {
                        return true;
                    }
                } else {
                    func(manager, page_offset, copy_amount);
                }
            }
            page_index++;
            page_offset = 0;
            remaining_size -= copy_amount;
        }
        return false;
    }

    void CreateRegion(std::size_t page_index) {
        const VAddr base_cpu_addr = page_index << TRACKER_HIGHER_PAGE_BITS;
        if (free_managers.empty()) {
            manager_pool.emplace_back();
            auto& last_pool = manager_pool.back();
            for (size_t i = 0; i < MANAGER_POOL_SIZE; i++) {
                std::construct_at(&last_pool[i], tracker, 0);
                free_managers.push_back(&last_pool[i]);
            }
        }
        // Each manager tracks a 4_MB virtual address space.
        auto* new_manager = free_managers.back();
        new_manager->SetCpuAddress(base_cpu_addr);
        new_manager->SetHotPagesEnabled(hot_pages_enabled);
        new_manager->SetRangeProtectionEnabled(range_protection_enabled);
        new_manager->SetShadowLedger(&hot_shadow_ledger);
        free_managers.pop_back();
        top_tier[page_index] = new_manager;
    }

    PageManager* tracker;
    std::deque<std::array<RegionManager, MANAGER_POOL_SIZE>> manager_pool;
    std::vector<RegionManager*> free_managers;
    std::array<RegionManager*, NUM_HIGH_PAGES> top_tier{};
    bool hot_pages_enabled{};
    bool range_protection_enabled{};
    HotShadowLedger hot_shadow_ledger{};
};

} // namespace VideoCore
