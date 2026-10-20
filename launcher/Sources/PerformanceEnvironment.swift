/// Runtime-only optimizations for the Metal performance profile.
enum PerformanceEnvironment {
    /// `fastShaderMath` also drops the driver's NaN-preserving comparisons (workarounds 14 and
    /// 17): a few percent faster outdoors, with a small chance of drawing differences.
    /// Switches still being tested in play (launcher toggle "Experimental speedups", off by
    /// default). Each is also off in the engine and driver unless set here.
    /// Measured outdoors (alternating 120 s runs, 2026-10-19): with the profile-guided engine,
    /// about +9 to +12 % together. Hot write pages stop most guest page faults; fence batching
    /// cuts ~180 command buffer submissions per frame to ~40; precise hazards drop the barriers
    /// of one long compute chain; the keep-alive stops the GPU clocking down in the gaps.
    static let experimentalSwitches: [String: String] = [
        "SHADPS4_HOT_WRITE_PAGES": "1",
        "SHADPS4_FENCE_SUBMIT_BATCH": "32",
        "SHADPS4_FENCE_HOLD_INFLIGHT": "4",
        "SHADPS4_PRECISE_BUFFER_HAZARDS": "1",
        "SHADPS4_PRECISE_HAZARD_SHADERS": "1d99a308,96d32d22,ee29a0b1,a3c4525b,eb218825",
        "KK_SHADPS4_GPU_KEEPALIVE": "2",
    ]

    /// The engine's shortcut for the game's copy shader a912f7d4 (it copies data with a plain
    /// GPU copy instead of running the shader). Busier while Trico's lightning fires; separate
    /// launcher toggle for testing in play.
    static let copyShortcutSwitches: [String: String] = [
        "SHADPS4_ENABLE_A912_COPY_HLE": "1",
        "SHADPS4_A912_IMAGE_SOURCE_COPY": "1",
    ]

    /// `shaderOverrideDir`: folder of hand-tuned Metal versions of the game's busiest compute
    /// shaders (`<key>.metal`, loaded by the driver in place of the generated code with the same
    /// key; checked byte-identical against the generated code). Part of the experimental set.
    static func apply(to inherited: [String: String], metalTweaks: Bool,
                      fastShaderMath: Bool = true,
                      experimental: Bool = false,
                      copyShortcut: Bool = false,
                      shaderOverrideDir: String? = nil) -> [String: String] {
        var env = inherited
        if metalTweaks && experimental {
            env.merge(experimentalSwitches) { _, new in new }
            if let shaderOverrideDir {
                env["KK_SHADPS4_MSL_OVERRIDE_DIR"] = shaderOverrideDir
            }
        }
        if metalTweaks && copyShortcut {
            env.merge(copyShortcutSwitches) { _, new in new }
        }
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
