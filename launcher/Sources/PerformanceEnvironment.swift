/// Runtime-only optimizations for the verified Metal performance profile.
enum PerformanceEnvironment {
    static func apply(to inherited: [String: String], metalTweaks: Bool) -> [String: String] {
        var env = inherited
        if metalTweaks {
            env["MESA_KK_DISABLE_WORKAROUNDS"] = "12,13"
            env["SHADPS4_METAL_ENCODER_ORDERING"] = "1"
            env["SHADPS4_PREFETCH_STRICT_UNMARK"] = "1"
            env["SHADPS4_TEXTURE_LOOKUP_REUSE"] = "1"
            env["SHADPS4_DOWNLOAD_RESCAN_REUSE"] = "1"
            env["SHADPS4_IMAGE_BACKING_READS"] = "1"
            env["SHADPS4_READBACK_WINDOW_KB"] = "64"
        }
        return env
    }
}
