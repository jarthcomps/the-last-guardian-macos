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
        XCTAssertEqual(env["MESA_KK_DISABLE_WORKAROUNDS"], "12,13")
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
}
