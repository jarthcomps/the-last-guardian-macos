/// Runtime-only optimizations for the Metal performance profile.
enum PerformanceEnvironment {
    /// `fastShaderMath` also drops the driver's NaN-preserving comparisons (workarounds 14 and
    /// 17): a few percent faster outdoors, with a small chance of drawing differences.
    static func apply(to inherited: [String: String], metalTweaks: Bool,
                      fastShaderMath: Bool = true) -> [String: String] {
        var env = inherited
        if metalTweaks {
            env["MESA_KK_DISABLE_WORKAROUNDS"] = fastShaderMath ? "12,13,14,17" : "12,13"
            env["SHADPS4_METAL_ENCODER_ORDERING"] = "1"
            env["SHADPS4_PREFETCH_STRICT_UNMARK"] = "1"
            env["SHADPS4_TEXTURE_LOOKUP_REUSE"] = "1"
            env["SHADPS4_DOWNLOAD_RESCAN_REUSE"] = "1"
            env["SHADPS4_IMAGE_BACKING_READS"] = "1"
            env["SHADPS4_READBACK_WINDOW_KB"] = "64"
            env["SHADPS4_DYNAMIC_STATE_REUSE"] = "1"
            env["SHADPS4_RANGE_PROTECTION"] = "1"
            env["KK_UPLOAD_CACHE_EXPANDED"] = "1"
        }
        return env
    }
}
