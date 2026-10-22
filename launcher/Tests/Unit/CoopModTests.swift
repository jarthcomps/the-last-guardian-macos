import XCTest
@testable import TLGLauncher

/// Builds small PSARC archives like the game's: base style (names separated by newlines, zlib
/// blocks) or patch style (names separated by NUL bytes, sorted, stored uncompressed).
enum PSARCFixture {
    static func be(_ v: Int, _ n: Int) -> Data { Data((0..<n).reversed().map { UInt8(truncatingIfNeeded: v >> (8 * $0)) }) }

    static func zlib(_ data: Data) -> Data {
        let deflated = try! (data as NSData).compressed(using: .zlib) as Data
        var a: UInt32 = 1, b: UInt32 = 0
        for byte in data { a = (a + UInt32(byte)) % 65521; b = (b + a) % 65521 }
        return Data([0x78, 0x9C]) + deflated + be(Int(b << 16 | a), 4)
    }

    static func make(_ url: URL, files: [String: Data], patchStyle: Bool, compress: Bool = false) {
        let blockSize = 0x10000
        let names = patchStyle ? files.keys.sorted() : Array(files.keys).sorted()
        let manifest = Data(names.map { Data($0.utf8) }.joined(separator: [patchStyle ? 0 : 0x0A]))
        var zsizes: [Int] = []
        var blobs: [(md5: Data, zindex: Int, length: Int, payload: Data)] = []
        for (name, data) in [("", manifest)] + names.map({ ($0, files[$0]!) }) {
            var payload = data
            let zindex = zsizes.count
            if compress && !name.isEmpty {
                // each 64 KB block compressed on its own, as the game's archives are
                payload = Data()
                for start in stride(from: 0, to: data.count, by: blockSize) {
                    let packed = zlib(data.subdata(in: start..<min(start + blockSize, data.count)))
                    zsizes.append(packed.count)
                    payload += packed
                }
            } else {
                zsizes += PSARC.blockSizes(data.count, blockSize)
            }
            blobs.append((name.isEmpty ? Data(count: 16) : PSARC.pathMD5(name), zindex, data.count, payload))
        }
        let entries = [blobs[0]] + blobs.dropFirst().sorted { $0.md5.lexicographicallyPrecedes($1.md5) }
        let toc = 32 + entries.count * 30 + zsizes.count * 2
        var pos = (toc + 15) / 16 * 16
        var offsets: [Data: Int] = [:]
        for blob in blobs { offsets[blob.md5] = pos; pos += blob.payload.count }
        var out = Data("PSAR".utf8) + be(1, 2) + be(4, 2) + Data("zlib".utf8)
        out += be(toc, 4) + be(30, 4) + be(entries.count, 4) + be(blockSize, 4) + be(patchStyle ? 13 : 1, 4)
        for e in entries { out += e.md5 + be(e.zindex, 4) + be(e.length, 5) + be(offsets[e.md5]!, 5) }
        for z in zsizes { out += be(z, 2) }
        out += Data(count: (toc + 15) / 16 * 16 - toc)
        for blob in blobs { out += blob.payload }
        try! out.write(to: url)
    }
}

final class CoopModTests: XCTestCase {
    var temp: URL!
    let anchor = CoopMod.mainAnchor

    override func setUpWithError() throws {
        temp = FileManager.default.temporaryDirectory.appendingPathComponent("coop-\(UUID().uuidString)")
        try FileManager.default.createDirectory(at: temp, withIntermediateDirectories: true)
    }

    override func tearDownWithError() throws { try? FileManager.default.removeItem(at: temp) }

    func text(_ d: Data) -> String { String(decoding: d, as: UTF8.self) }

    // MARK: main.lua edit

    func testEditGoesAfterTheAnchorKeepingCRLF() throws {
        let source = Data("-- main\r\n\(anchor)\r\nrest()\r\n".utf8)
        let edited = text(try CoopMod.applyEdit(source, known: nil))
        XCTAssertEqual(edited, "-- main\r\n\(anchor)\r\n-- tlg-coop\r\n"
            + "if not pcall(Load, \"game/scripts/coop/coop_main\") then CoopLoadFailed = true end\r\nrest()\r\n")
    }

    func testLFStaysLFAndAMissingFinalNewlineIsAdded() throws {
        XCTAssertFalse(text(try CoopMod.applyEdit(Data("a\n\(anchor)\nb\n".utf8), known: nil)).contains("\r"))
        let tail = text(try CoopMod.applyEdit(Data("a\n\(anchor)".utf8), known: nil))
        XCTAssertTrue(tail.hasPrefix("a\n\(anchor)\n-- tlg-coop\n"))
    }

    func testUnknownScriptAndRepeatedAnchorAreRefused() {
        XCTAssertThrowsError(try CoopMod.applyEdit(Data("\(anchor)\n".utf8)))  // hash not pinned
        XCTAssertThrowsError(try CoopMod.applyEdit(Data("\(anchor)\n\(anchor)\n".utf8), known: nil))
        XCTAssertThrowsError(try CoopMod.applyEdit(Data("nothing\n".utf8), known: nil))
    }

    // MARK: archives

    func layout(updateFiles: [String: Data] = ["game/scripts/other.lua": Data("other".utf8),
                                               "game/data/patched.bin": Data([1, 2, 3])]) -> (URL, URL) {
        let game = temp.appendingPathComponent("CUSA03745")
        let update = temp.appendingPathComponent("CUSA03745-UPDATE")
        try! FileManager.default.createDirectory(at: game, withIntermediateDirectories: true)
        try! FileManager.default.createDirectory(at: update, withIntermediateDirectories: true)
        PSARCFixture.make(game.appendingPathComponent("trico_boot.psarc"),
                          files: [CoopMod.mainScript: Data("-- main\r\n\(anchor)\r\nrest()\r\n".utf8),
                                  "game/scripts/base.lua": Data(repeating: 65, count: 70_000)],
                          patchStyle: false, compress: true)
        PSARCFixture.make(update.appendingPathComponent("trico_bootp.psarc"), files: updateFiles, patchStyle: true)
        let scripts = temp.appendingPathComponent("lua")
        try! FileManager.default.createDirectory(at: scripts, withIntermediateDirectories: true)
        try! Data("-- entry".utf8).write(to: scripts.appendingPathComponent("coop_main.lua"))
        try! Data("-- input".utf8).write(to: scripts.appendingPathComponent("coop_input.lua"))
        return (game, scripts)
    }

    func testReaderHandlesBothArchiveStyles() throws {
        let (game, _) = layout()
        let base = try PSARC(url: game.appendingPathComponent("trico_boot.psarc"))
        XCTAssertEqual(base.separator, 0x0A)
        XCTAssertEqual(try base.read("game/scripts/base.lua"), Data(repeating: 65, count: 70_000))
        XCTAssertEqual(try base.read("GAME/SCRIPTS/BASE.LUA")?.count, 70_000)   // upper-cased MD5 lookup
        let update = try PSARC(url: temp.appendingPathComponent("CUSA03745-UPDATE/trico_bootp.psarc"))
        XCTAssertEqual(update.separator, 0)
        XCTAssertEqual(try update.read("game/scripts/other.lua"), Data("other".utf8))
        XCTAssertNil(try update.read("missing.lua"))
    }

    func testInstallBuildsTheOverlayAndUninstallRemovesIt() throws {
        let (game, scripts) = layout()
        let target = try CoopMod.install(game: game, scriptFolder: scripts, knownMain: nil)
        XCTAssertEqual(target.path, temp.appendingPathComponent("CUSA03745-mods/trico_bootp.psarc").path)
        let arc = try PSARC(url: target)
        XCTAssertEqual(try arc.read("game/scripts/coop/coop_main.lua"), Data("-- entry".utf8))
        XCTAssertEqual(try arc.read("game/scripts/coop/coop_input.lua"), Data("-- input".utf8))
        XCTAssertEqual(try arc.read("game/scripts/other.lua"), Data("other".utf8))   // shipped entry kept
        XCTAssertTrue(text(try arc.read(CoopMod.mainScript)!).contains("coop_main\") then CoopLoadFailed"))
        XCTAssertEqual(arc.separator, 0)
        XCTAssertEqual(arc.names, arc.names.sorted())
        XCTAssertTrue(CoopMod.isInstalled(game: game))
        XCTAssertTrue(CoopMod.isCurrent(game: game, scriptFolder: scripts))
        // the scripts change: no longer current; reinstalling replaces the copy
        try Data("-- entry 2".utf8).write(to: scripts.appendingPathComponent("coop_main.lua"))
        XCTAssertFalse(CoopMod.isCurrent(game: game, scriptFolder: scripts))
        try CoopMod.uninstall(game: game)
        try CoopMod.install(game: game, scriptFolder: scripts, knownMain: nil)
        XCTAssertEqual(try PSARC(url: target).read("game/scripts/coop/coop_main.lua"), Data("-- entry 2".utf8))
        XCTAssertTrue(try CoopMod.uninstall(game: game))
        XCTAssertFalse(FileManager.default.fileExists(atPath: temp.appendingPathComponent("CUSA03745-mods").path))
        XCTAssertFalse(try CoopMod.uninstall(game: game))
    }

    func testAnotherModsArchiveIsLeftAlone() throws {
        let (game, scripts) = layout()
        let mods = temp.appendingPathComponent("CUSA03745-mods")
        try FileManager.default.createDirectory(at: mods, withIntermediateDirectories: true)
        try Data("someone else's".utf8).write(to: mods.appendingPathComponent("trico_bootp.psarc"))
        XCTAssertThrowsError(try CoopMod.install(game: game, scriptFolder: scripts, knownMain: nil))
        XCTAssertThrowsError(try CoopMod.uninstall(game: game))
        XCTAssertEqual(try Data(contentsOf: mods.appendingPathComponent("trico_bootp.psarc")), Data("someone else's".utf8))
    }

    func testNeedsTheUpdateAndTheEntryScript() throws {
        let (game, scripts) = layout()
        try FileManager.default.removeItem(at: scripts.appendingPathComponent("coop_main.lua"))
        XCTAssertThrowsError(try CoopMod.install(game: game, scriptFolder: scripts, knownMain: nil))
        try FileManager.default.removeItem(at: temp.appendingPathComponent("CUSA03745-UPDATE"))
        XCTAssertThrowsError(try CoopMod.install(game: game, scriptFolder: scripts, knownMain: nil))
    }

    // MARK: patch and settings

    func testPatchGetsTheSecondControllerLineOnce() throws {
        let resolution = "<?xml version=\"1.0\"?>\n<Patch>\n<Metadata Title=\"The Last Guardian\" Name=\"720p\">"
            + "<PatchList /></Metadata>\n</Patch>\n"
        let once = try CoopMod.combinePatch(resolution)
        XCTAssertTrue(once.contains("Address=\"0x010f808c\" Value=\"e9030000\""))
        XCTAssertTrue(once.contains("Name=\"720p\""))
        let twice = try CoopMod.combinePatch(once)
        XCTAssertEqual(twice.components(separatedBy: "Co-op: open the second controller").count, 2)
        XCTAssertThrowsError(try CoopMod.combinePatch("<NotAPatch/>"))
    }

    func testCoopSettingDefaultsOffAndIsKept() throws {
        let old = try JSONDecoder().decode(LauncherSettings.self, from: Data("{\"resolution\":\"540p\"}".utf8))
        XCTAssertFalse(old.coop)
        var on = LauncherSettings()
        on.coop = true
        let back = try JSONDecoder().decode(LauncherSettings.self, from: JSONEncoder().encode(on))
        XCTAssertTrue(back.coop)
    }

    @MainActor
    func testCoopStartsTheEngineInCoopMode() throws {
        setenv("TLG_LAUNCHER_DATA_DIR", temp.appendingPathComponent("data").path, 1)
        defer { unsetenv("TLG_LAUNCHER_DATA_DIR") }
        let launcher = Launcher()
        XCTAssertNil(launcher.environment()[CoopMod.environmentKey])
        launcher.settings.coop = true
        XCTAssertEqual(launcher.environment()[CoopMod.environmentKey], "1")
    }
}
