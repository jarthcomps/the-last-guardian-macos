import Darwin

/// Memory the game can get without the Mac compressing or swapping. Outdoors The Last Guardian
/// needs about 8.5 GB; with less available the whole game (its own threads too) slows several
/// times over, down to 2 FPS (measured 2026-10-20 on a 32 GB Mac with other apps open).
enum MemoryCheck {
    static let neededBytes: UInt64 = 9 << 30

    /// The status line to show when starting the game, or nil when memory is fine.
    static func warning(availableBytes: UInt64) -> String? {
        guard availableBytes < neededBytes else { return nil }
        let gigabytes = Double(availableBytes) / Double(1 << 30)
        return String(format: "Low on memory (%.1f GB available): close other apps for a steadier "
                      + "frame rate.", gigabytes)
    }

    /// Free, inactive, speculative and purgeable memory: what the system can hand out without
    /// compressing anything.
    static func availableBytes() -> UInt64 {
        var stats = vm_statistics64()
        var count = mach_msg_type_number_t(MemoryLayout<vm_statistics64>.size / MemoryLayout<integer_t>.size)
        let result = withUnsafeMutablePointer(to: &stats) {
            $0.withMemoryRebound(to: integer_t.self, capacity: Int(count)) {
                host_statistics64(mach_host_self(), HOST_VM_INFO64, $0, &count)
            }
        }
        guard result == KERN_SUCCESS else { return .max }
        let pages = UInt64(stats.free_count) + UInt64(stats.inactive_count)
            + UInt64(stats.speculative_count) + UInt64(stats.purgeable_count)
        return pages * UInt64(vm_kernel_page_size)
    }
}
