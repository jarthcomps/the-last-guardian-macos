import XCTest
@testable import TLGLauncher

final class PerformanceEnvironmentTests: XCTestCase {
    func testEnabledProfileCombinesReuseWithoutChangingSynchronization() {
        let env = PerformanceEnvironment.apply(to: ["PATH": "/usr/bin"], metalTweaks: true)
        for flag in ["SHADPS4_TEXTURE_LOOKUP_REUSE", "SHADPS4_DOWNLOAD_RESCAN_REUSE",
                     "SHADPS4_IMAGE_BACKING_READS", "SHADPS4_METAL_ENCODER_ORDERING",
                     "SHADPS4_PREFETCH_STRICT_UNMARK", "SHADPS4_DYNAMIC_STATE_REUSE",
                     "SHADPS4_RANGE_PROTECTION", "KK_UPLOAD_CACHE_EXPANDED"] {
            XCTAssertEqual(env[flag], "1", flag)
        }
        XCTAssertEqual(env["SHADPS4_READBACK_WINDOW_KB"], "64")
        XCTAssertEqual(env["MESA_KK_DISABLE_WORKAROUNDS"], "12,13,14,17")
        XCTAssertEqual(env["PATH"], "/usr/bin")
        for flag in ["SHADPS4_TEXTURE_LOOKUP_VERIFY", "SHADPS4_IMAGE_BACKING_VERIFY",
                     "SHADPS4_PRECISE_BUFFER_HAZARDS", "SHADPS4_ENABLE_A912_COPY_HLE",
                     "SHADPS4_IMAGE_UPLOAD_SKIP_HASH", "SHADPS4_COMPUTE_DISPATCH_LABELS",
                     "KK_UPLOAD_CACHE_DIAGNOSTICS", "MESA_KK_ICB_BATCHING",
                     "MESA_KK_TESS_PACKED", "MESA_KK_COMPUTE_PROFILE"] {
            XCTAssertNil(env[flag], flag)
        }
    }

    func testDisabledProfileLeavesInheritedEnvironmentUnchanged() {
        let inherited = ["PATH": "/bin", "SHADPS4_READBACK_WINDOW_KB": "128"]
        XCTAssertEqual(PerformanceEnvironment.apply(to: inherited, metalTweaks: false), inherited)
        XCTAssertTrue(PerformanceEnvironment.apply(to: [:], metalTweaks: false).isEmpty)
    }

    func testUploadCacheOnlyEnabledByMetalProfile() {
        XCTAssertNil(PerformanceEnvironment.apply(to: [:], metalTweaks: false)["KK_UPLOAD_CACHE_EXPANDED"])
        let inherited = ["KK_UPLOAD_CACHE_EXPANDED": "0", "PATH": "/bin"]
        XCTAssertEqual(PerformanceEnvironment.apply(to: inherited, metalTweaks: false), inherited)
        XCTAssertEqual(PerformanceEnvironment.apply(to: inherited, metalTweaks: true)["KK_UPLOAD_CACHE_EXPANDED"], "1")
    }

    func testEnabledProfileOverridesStalePerformanceValues() {
        let env = PerformanceEnvironment.apply(to: ["SHADPS4_TEXTURE_LOOKUP_REUSE": "0",
                                                   "SHADPS4_READBACK_WINDOW_KB": "512",
                                                   "SHADPS4_DYNAMIC_STATE_REUSE": "0",
                                                   "SHADPS4_RANGE_PROTECTION": "0"],
                                               metalTweaks: true)
        XCTAssertEqual(env["SHADPS4_TEXTURE_LOOKUP_REUSE"], "1")
        XCTAssertEqual(env["SHADPS4_READBACK_WINDOW_KB"], "64")
        XCTAssertEqual(env["SHADPS4_DYNAMIC_STATE_REUSE"], "1")
        XCTAssertEqual(env["SHADPS4_RANGE_PROTECTION"], "1")
    }

    func testFastShaderMathSwitchOnlyChangesDriverWorkarounds() {
        let fast = PerformanceEnvironment.apply(to: [:], metalTweaks: true, fastShaderMath: true)
        let safe = PerformanceEnvironment.apply(to: [:], metalTweaks: true, fastShaderMath: false)
        XCTAssertEqual(fast["MESA_KK_DISABLE_WORKAROUNDS"], "12,13,14,17")
        XCTAssertEqual(safe["MESA_KK_DISABLE_WORKAROUNDS"], "12,13")
        var rest = fast
        rest["MESA_KK_DISABLE_WORKAROUNDS"] = "12,13"
        XCTAssertEqual(rest, safe)
        XCTAssertTrue(PerformanceEnvironment.apply(to: [:], metalTweaks: false,
                                                   fastShaderMath: true).isEmpty)
    }

    func testShaderOverrideDirOnlyWithExperimentalSwitch() {
        let dir = "/tmp/shader-overrides"
        let on = PerformanceEnvironment.apply(to: [:], metalTweaks: true, experimental: true,
                                              shaderOverrideDir: dir)
        XCTAssertEqual(on["KK_SHADPS4_MSL_OVERRIDE_DIR"], dir)
        let off = PerformanceEnvironment.apply(to: [:], metalTweaks: true, experimental: false,
                                               shaderOverrideDir: dir)
        XCTAssertNil(off["KK_SHADPS4_MSL_OVERRIDE_DIR"])
        let noTweaks = PerformanceEnvironment.apply(to: [:], metalTweaks: false, experimental: true,
                                                    shaderOverrideDir: dir)
        XCTAssertNil(noTweaks["KK_SHADPS4_MSL_OVERRIDE_DIR"])
        let noDir = PerformanceEnvironment.apply(to: [:], metalTweaks: true, experimental: true)
        XCTAssertNil(noDir["KK_SHADPS4_MSL_OVERRIDE_DIR"])
    }

    func testShaderRecipesOnlyWithExperimentalSwitch() {
        let file = "/tmp/shader-recipes.txt"
        let on = PerformanceEnvironment.apply(to: [:], metalTweaks: true, experimental: true,
                                              shaderRecipes: file)
        XCTAssertEqual(on["KK_SHADPS4_MSL_RECIPES"], file)
        let off = PerformanceEnvironment.apply(to: [:], metalTweaks: true, experimental: false,
                                               shaderRecipes: file)
        XCTAssertNil(off["KK_SHADPS4_MSL_RECIPES"])
        XCTAssertNil(PerformanceEnvironment.apply(to: [:], metalTweaks: true,
                                                  experimental: true)["KK_SHADPS4_MSL_RECIPES"])
    }

    func testExperimentalSwitchAddsOnlyItsOwnFlags() {
        let base = PerformanceEnvironment.apply(to: ["PATH": "/bin"], metalTweaks: true)
        let experimental = PerformanceEnvironment.apply(to: ["PATH": "/bin"], metalTweaks: true,
                                                        experimental: true)
        XCTAssertFalse(PerformanceEnvironment.experimentalSwitches.isEmpty)
        for (key, value) in PerformanceEnvironment.experimentalSwitches {
            XCTAssertNil(base[key], key)
            XCTAssertEqual(experimental[key], value, key)
        }
        var rest = experimental
        for key in PerformanceEnvironment.experimentalSwitches.keys {
            rest[key] = nil
        }
        XCTAssertEqual(rest, base)
        // Nothing experimental without the Mac performance profile.
        XCTAssertEqual(PerformanceEnvironment.apply(to: ["PATH": "/bin"], metalTweaks: false,
                                                    experimental: true), ["PATH": "/bin"])
    }

    func testCopyShortcutSwitchIsSeparateAndOffByDefault() {
        let base = PerformanceEnvironment.apply(to: [:], metalTweaks: true)
        XCTAssertNil(base["SHADPS4_ENABLE_A912_COPY_HLE"])
        let on = PerformanceEnvironment.apply(to: [:], metalTweaks: true, copyShortcut: true)
        XCTAssertEqual(on["SHADPS4_ENABLE_A912_COPY_HLE"], "1")
        XCTAssertEqual(on["SHADPS4_A912_IMAGE_SOURCE_COPY"], "1")
        XCTAssertNil(on["SHADPS4_HOT_WRITE_PAGES"]) // independent of Experimental speedups
        XCTAssertNil(PerformanceEnvironment.apply(to: [:], metalTweaks: false,
                                                  copyShortcut: true)["SHADPS4_ENABLE_A912_COPY_HLE"])
        XCTAssertFalse(LauncherSettings().copyShaderShortcut)
    }

    func testMemoryWarningOnlyBelowWhatTheGameNeeds() {
        XCTAssertNil(MemoryCheck.warning(availableBytes: 14 << 30))
        XCTAssertNil(MemoryCheck.warning(availableBytes: MemoryCheck.neededBytes))
        XCTAssertEqual(MemoryCheck.warning(availableBytes: 3 << 29),
                       "Low on memory (1.5 GB available): close other apps for a steadier frame rate.")
        XCTAssertGreaterThan(MemoryCheck.availableBytes(), 0)
    }

    func testOlderSettingsFileKeepsPathsAndGetsNewDefaults() throws {
        let old = #"{"gamePath":"/Games/TLG","resolution":"540p","fullscreen":true,"showFPS":false,"metalTweaks":true}"#
        let settings = try JSONDecoder().decode(LauncherSettings.self, from: Data(old.utf8))
        XCTAssertEqual(settings.gamePath, "/Games/TLG")
        XCTAssertEqual(settings.resolution, "540p")
        XCTAssertTrue(settings.fullscreen)
        XCTAssertTrue(settings.fastShaderMath)
        XCTAssertTrue(settings.experimentalSpeedups)
        var off = settings
        off.fastShaderMath = false
        let round = try JSONDecoder().decode(LauncherSettings.self,
                                             from: JSONEncoder().encode(off))
        XCTAssertFalse(round.fastShaderMath)
    }

    func testSpeedupsTurnOnOnceForSettingsFromBefore117() throws {
        XCTAssertTrue(LauncherSettings().experimentalSpeedups)
        XCTAssertTrue(LauncherSettings().fastShaderMath)
        let from116 = #"{"metalTweaks":true,"fastShaderMath":true,"experimentalSpeedups":false}"#
        let migrated = try JSONDecoder().decode(LauncherSettings.self, from: Data(from116.utf8))
        XCTAssertTrue(migrated.experimentalSpeedups)
        XCTAssertEqual(migrated.settingsVersion, 2)
        var off = migrated
        off.experimentalSpeedups = false
        let kept = try JSONDecoder().decode(LauncherSettings.self, from: JSONEncoder().encode(off))
        XCTAssertFalse(kept.experimentalSpeedups) // a choice made in 1.1.7 or later is kept
    }

    func testConfigMergeAddsNewKeysAndKeepsUserValues() throws {
        let defaults: [String: Any] = ["GPU": ["readbacks_mode": 2, "new_setting": true],
                                       "Top": "x"]
        let user: [String: Any] = ["GPU": ["readbacks_mode": 1, "user_only": 5]]
        let merged = ConfigMerge.addingMissingKeys(defaults: defaults, to: user)
        let gpu = try XCTUnwrap(merged["GPU"] as? [String: Any])
        XCTAssertEqual(gpu["readbacks_mode"] as? Int, 1)
        XCTAssertEqual(gpu["new_setting"] as? Bool, true)
        XCTAssertEqual(gpu["user_only"] as? Int, 5)
        XCTAssertEqual(merged["Top"] as? String, "x")

        let dir = FileManager.default.temporaryDirectory
            .appendingPathComponent(UUID().uuidString)
        try FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        let target = dir.appendingPathComponent("user.json")
        let base = dir.appendingPathComponent("defaults.json")
        try JSONSerialization.data(withJSONObject: user).write(to: target)
        try JSONSerialization.data(withJSONObject: defaults).write(to: base)
        XCTAssertTrue(try ConfigMerge.update(target: target, defaults: base))
        XCTAssertFalse(try ConfigMerge.update(target: target, defaults: base))
    }
}
