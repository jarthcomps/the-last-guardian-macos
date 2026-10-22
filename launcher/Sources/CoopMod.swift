import CryptoKit
import Foundation

/// The two-player co-op mod (experimental): a Swift port of `coop/tlg_coop/install.py` so the
/// launcher does not depend on python3.
///
/// The mod is a rebuilt copy of the update's `trico_bootp.psarc` in the engine's `<game>-mods`
/// overlay folder. It keeps every shipped entry byte for byte and adds the co-op scripts under
/// `game/scripts/coop/`, plus `game/scripts/states/main.lua` with one line that loads them. The
/// game's own script is read from the player's archives at install time and edited at a fixed
/// anchor, so no game code is shipped; the edit is refused for an unknown script version.
enum CoopMod {
    static let version = "0.1.0"
    static let marker = "tlg-coop.json"
    static let patchArchive = "trico_bootp.psarc"
    static let baseArchive = "trico_boot.psarc"
    static let scriptPrefix = "game/scripts/coop/"
    static let environmentKey = "SHADPS4_TLG_COOP"

    static let mainScript = "game/scripts/states/main.lua"
    static let mainAnchor = "if SysInfo.ps4 then Load(\"game/scripts/common/InitialPreload\") end"
    static let mainInsert =
        "-- tlg-coop\nif not pcall(Load, \"game/scripts/coop/coop_main\") then CoopLoadFailed = true end\n"
    /// CUSA03745 base game (the update does not replace this script).
    static let knownMainSHA256: Set<String> = [
        "fa3da4c0b380fe0acc338e0ce90d9a8609ce245d788da2ad2019dcf705381a7f",
    ]

    struct Failure: LocalizedError {
        let message: String
        var errorDescription: String? { message }
    }

    struct Layout {
        let game: URL
        let update: URL
        let mods: URL
        var target: URL { mods.appendingPathComponent(CoopMod.patchArchive) }
        var markerFile: URL { mods.appendingPathComponent(CoopMod.marker) }

        init(game: URL) {
            var game = game
            if game.lastPathComponent.hasSuffix("-UPDATE") {
                game = game.deletingLastPathComponent()
                    .appendingPathComponent(String(game.lastPathComponent.dropLast("-UPDATE".count)))
            }
            self.game = game
            update = game.deletingLastPathComponent().appendingPathComponent(game.lastPathComponent + "-UPDATE")
            mods = game.deletingLastPathComponent().appendingPathComponent(game.lastPathComponent + "-mods")
        }
    }

    static func sha256(_ data: Data) -> String {
        SHA256.hash(data: data).map { String(format: "%02x", $0) }.joined()
    }

    /// `source` with `insert` on the line after the single line equal to `anchor` (line endings
    /// kept as the file has them).
    static func applyEdit(_ source: Data, anchor: String = mainAnchor, insert: String = mainInsert,
                          known: Set<String>? = knownMainSHA256) throws -> Data {
        if let known, !known.contains(sha256(source)) {
            throw Failure(message: "\(mainScript): unknown version of the game script; refusing to edit it")
        }
        guard String(data: source, encoding: .utf8) != nil else {
            throw Failure(message: "\(mainScript) is not UTF-8")
        }
        // Lines split on bytes ("\r\n" is one Character in a Swift String), endings kept.
        let bytes = [UInt8](source)
        var lines: [[UInt8]] = []
        var lineStart = 0
        for (i, b) in bytes.enumerated() where b == 0x0A {
            lines.append(Array(bytes[lineStart...i]))
            lineStart = i + 1
        }
        if lineStart < bytes.count { lines.append(Array(bytes[lineStart...])) }
        func stripped(_ line: [UInt8]) -> [UInt8] {
            var l = line
            while let last = l.last, last == 0x0A || last == 0x0D { l.removeLast() }
            return l
        }
        let anchorBytes = [UInt8](anchor.utf8)
        let hits = lines.indices.filter { stripped(lines[$0]) == anchorBytes }
        guard hits.count == 1 else {
            throw Failure(message: "\(mainScript): anchor found \(hits.count) times, expected once")
        }
        let i = hits[0]
        let endsLF = lines[i].last == 0x0A
        let endsCRLF = endsLF && lines[i].count >= 2 && lines[i][lines[i].count - 2] == 0x0D
        let hasCRLF = zip(bytes, bytes.dropFirst()).contains { $0 == 0x0D && $1 == 0x0A }
        let newline: [UInt8] = (endsCRLF || (!endsLF && hasCRLF)) ? [0x0D, 0x0A] : [0x0A]
        if !endsLF { lines[i] += newline }
        var added: [UInt8] = []
        for b in insert.utf8 { if b == 0x0A { added += newline } else { added.append(b) } }
        lines.insert(added, at: i + 1)
        return Data(lines.joined())
    }

    /// The co-op scripts by archive path (`game/scripts/coop/<name>.lua`).
    static func scripts(in folder: URL) throws -> [String: Data] {
        let names = try FileManager.default.contentsOfDirectory(atPath: folder.path)
            .filter { $0.hasSuffix(".lua") }.sorted()
        var files: [String: Data] = [:]
        for name in names {
            files[scriptPrefix + name.lowercased()] = try Data(contentsOf: folder.appendingPathComponent(name))
        }
        guard files[scriptPrefix + "coop_main.lua"] != nil else {
            throw Failure(message: "\(folder.path): coop_main.lua is missing")
        }
        return files
    }

    /// A game script as the game sees it: the update archive wins over the base archive.
    static func readGameScript(_ layout: Layout, _ name: String) throws -> Data {
        for archive in [layout.update.appendingPathComponent(patchArchive),
                        layout.game.appendingPathComponent(baseArchive)]
        where FileManager.default.fileExists(atPath: archive.path) {
            if let data = try PSARC(url: archive).read(name) { return data }
        }
        throw Failure(message: "\(name) was not found in the game or update archives")
    }

    struct Marker: Codable, Equatable {
        struct Source: Codable, Equatable { var path: String; var size: Int; var mtime: Int }
        var mod: String
        var version: String
        var source: Source
        var scripts: [String: String]
        var archive_size: Int
    }

    static func readMarker(_ layout: Layout) -> Marker? {
        guard let data = try? Data(contentsOf: layout.markerFile) else { return nil }
        return try? JSONDecoder().decode(Marker.self, from: data)
    }

    static func isInstalled(game: URL) -> Bool { readMarker(Layout(game: game)) != nil }

    /// Whether the installed copy no longer matches `scriptFolder` or the update archive.
    static func isCurrent(game: URL, scriptFolder: URL) -> Bool {
        let layout = Layout(game: game)
        guard let marker = readMarker(layout), marker.version == version,
              let size = try? FileManager.default.attributesOfItem(atPath: layout.target.path)[.size] as? Int,
              size == marker.archive_size,
              let scripts = try? scripts(in: scriptFolder) else { return false }
        let installed = marker.scripts.filter { $0.key.hasPrefix(scriptPrefix) }
        return installed == scripts.mapValues(sha256)
    }

    @discardableResult
    static func install(game: URL, scriptFolder: URL,
                        knownMain: Set<String>? = knownMainSHA256) throws -> URL {
        let fm = FileManager.default
        let layout = Layout(game: game)
        let source = layout.update.appendingPathComponent(patchArchive)
        guard fm.fileExists(atPath: source.path) else {
            throw Failure(message: "\(source.path) is missing: the co-op mod needs update 1.03")
        }
        if fm.fileExists(atPath: layout.target.path) && readMarker(layout) == nil {
            throw Failure(message: "\(layout.target.path) belongs to another mod; remove it first")
        }
        var replacements = try scripts(in: scriptFolder)
        replacements[mainScript] = try applyEdit(readGameScript(layout, mainScript), known: knownMain)
        try fm.createDirectory(at: layout.mods, withIntermediateDirectories: true)
        try PSARC(url: source).writePatched(replacements, to: layout.target)
        let attrs = try fm.attributesOfItem(atPath: source.path)
        let targetSize = try fm.attributesOfItem(atPath: layout.target.path)[.size] as? Int ?? 0
        let marker = Marker(
            mod: "tlg-coop", version: version,
            source: .init(path: source.path, size: attrs[.size] as? Int ?? 0,
                          mtime: Int((attrs[.modificationDate] as? Date ?? Date()).timeIntervalSince1970)),
            scripts: replacements.mapValues(sha256), archive_size: targetSize)
        let encoder = JSONEncoder()
        encoder.outputFormatting = [.prettyPrinted, .sortedKeys]
        try encoder.encode(marker).write(to: layout.markerFile)
        return layout.target
    }

    /// Removes our archive and marker (an archive another mod put there is left alone).
    @discardableResult
    static func uninstall(game: URL) throws -> Bool {
        let fm = FileManager.default
        let layout = Layout(game: game)
        guard readMarker(layout) != nil else {
            if fm.fileExists(atPath: layout.target.path) {
                throw Failure(message: "\(layout.target.path) was not installed by the co-op mod; left alone")
            }
            return false
        }
        try? fm.removeItem(at: layout.target)
        try fm.removeItem(at: layout.markerFile)
        if (try? fm.contentsOfDirectory(atPath: layout.mods.path))?.isEmpty == true {
            try? fm.removeItem(at: layout.mods)
        }
        return true
    }

    // MARK: Game patch

    static let patchName = "Co-op: open the second controller"
    /// eboot 1.03: the game gives its second pad object the user id -1 (`mov dword [rbx+0x1c],
    /// 0xffffffff`); 1001 (the emulator's second user) makes it open and read pad 2.
    static let patchBlock = """
        <Metadata Title="%@" Name="Co-op: open the second controller" Note="Second player (tlg-coop)" \
        Author="tlg-coop" PatchVer="1.0" AppVer="01.03" AppElf="eboot.bin" isEnabled="true">\
        <PatchList><Line Type="bytes" Address="0x010f808c" Value="e9030000" /></PatchList></Metadata>
        """

    /// The resolution patch XML with the co-op block added once, before `</Patch>`.
    static func combinePatch(_ resolutionXML: String) throws -> String {
        guard let close = resolutionXML.range(of: "</Patch>", options: .backwards) else {
            throw Failure(message: "not a shadPS4 patch file")
        }
        var text = resolutionXML
        // drop an earlier co-op block
        while let start = text.range(of: "<Metadata Title=\"[^\"]*\" Name=\"\(patchName)\"",
                                     options: .regularExpression),
              let end = text.range(of: "</Metadata>", range: start.upperBound..<text.endIndex) {
            text.removeSubrange(start.lowerBound..<end.upperBound)
        }
        let title = text.range(of: "Title=\"([^\"]+)\"", options: .regularExpression)
            .map { String(text[$0].dropFirst(7).dropLast()) } ?? "The Last Guardian"
        let closeNow = text.range(of: "</Patch>", options: .backwards) ?? close
        text.insert(contentsOf: String(format: patchBlock, title) + "\n", at: closeNow.lowerBound)
        return text
    }
}

/// Minimal PSARC (v1.4) reader and patch-archive writer, as `coop/tlg_coop/psarc.py`. Entries are
/// looked up by the MD5 of their upper-cased path.
struct PSARC {
    struct Entry { var md5: Data; var zindex: Int; var length: Int; var offset: Int }

    static let headerSize = 32
    static let entrySize = 30

    let url: URL
    var major: UInt16, minor: UInt16
    var compression: Data
    var tocLength: Int
    var blockSize: Int
    var flags: UInt32
    var entries: [Entry]
    var zsizes: [Int]
    var zsizeBytes: Int
    var names: [String] = []
    var separator: UInt8 = 0x0A

    static func pathMD5(_ name: String) -> Data { Data(Insecure.MD5.hash(data: Data(name.uppercased().utf8))) }

    private static func be(_ d: Data, _ o: Int, _ n: Int) -> Int {
        var v = 0
        for i in 0..<n { v = v << 8 | Int(d[d.startIndex + o + i]) }
        return v
    }

    init(url: URL) throws {
        self.url = url
        let handle = try FileHandle(forReadingFrom: url)
        defer { try? handle.close() }
        let head = try handle.read(upToCount: PSARC.headerSize) ?? Data()
        guard head.count == PSARC.headerSize, head.prefix(4) == Data("PSAR".utf8) else {
            throw CoopMod.Failure(message: "\(url.path): not a PSARC archive")
        }
        major = UInt16(PSARC.be(head, 4, 2)); minor = UInt16(PSARC.be(head, 6, 2))
        compression = head.subdata(in: 8..<12)
        tocLength = PSARC.be(head, 12, 4)
        let entSize = PSARC.be(head, 16, 4), count = PSARC.be(head, 20, 4)
        blockSize = PSARC.be(head, 24, 4)
        flags = UInt32(PSARC.be(head, 28, 4))
        guard entSize == PSARC.entrySize else {
            throw CoopMod.Failure(message: "\(url.path): unexpected TOC entry size \(entSize)")
        }
        let toc = try handle.read(upToCount: tocLength - PSARC.headerSize) ?? Data()
        entries = (0..<count).map { i in
            let o = i * PSARC.entrySize
            return Entry(md5: toc.subdata(in: o..<o + 16), zindex: PSARC.be(toc, o + 16, 4),
                         length: PSARC.be(toc, o + 20, 5), offset: PSARC.be(toc, o + 25, 5))
        }
        var zb = 1
        while (1 << (8 * zb)) < blockSize { zb += 1 }
        zsizeBytes = zb
        let tableStart = count * PSARC.entrySize
        let blocks = (toc.count - tableStart) / zb
        zsizes = (0..<blocks).map { PSARC.be(toc, tableStart + $0 * zb, zb) }
        let manifest = try readEntry(0)
        let nul = manifest.filter { $0 == 0 }.count, nl = manifest.filter { $0 == 0x0A }.count
        separator = nul > nl ? 0 : 0x0A
        names = manifest.split(separator: separator).map { String(decoding: $0, as: UTF8.self) }
    }

    func readEntry(_ index: Int) throws -> Data {
        let e = entries[index]
        let handle = try FileHandle(forReadingFrom: url)
        defer { try? handle.close() }
        try handle.seek(toOffset: UInt64(e.offset))
        var out = Data()
        var remaining = e.length, z = e.zindex
        while remaining > 0 {
            let want = min(remaining, blockSize)
            let size = zsizes[z]
            z += 1
            var chunk: Data
            if size == 0 {
                chunk = try handle.read(upToCount: blockSize) ?? Data()
            } else {
                chunk = try handle.read(upToCount: size) ?? Data()
                if size != want {
                    // zlib stream: 2-byte header, raw deflate, 4-byte checksum
                    chunk = try (chunk.dropFirst(2) as NSData).decompressed(using: .zlib) as Data
                }
            }
            out += chunk.prefix(want)
            remaining -= want
        }
        return out
    }

    func read(_ name: String) throws -> Data? {
        let md5 = PSARC.pathMD5(name)
        guard let i = entries.indices.dropFirst().first(where: { entries[$0].md5 == md5 }) else { return nil }
        return try readEntry(i)
    }

    var dataStart: Int { entries.map(\.offset).min() ?? tocLength }

    /// Uncompressed block sizes for `count` bytes (0 = a full block), as the patch archives store them.
    static func blockSizes(_ count: Int, _ blockSize: Int) -> [Int] {
        stride(from: 0, to: count, by: blockSize).map { min(blockSize, count - $0) == blockSize ? 0 : count - $0 }
    }

    /// A copy of this archive where each path in `replacements` has new contents (replaced or
    /// added); untouched entries are copied byte for byte and new data is appended uncompressed.
    func writePatched(_ replacements: [String: Data], to out: URL) throws {
        var entries = self.entries
        var zsizes = self.zsizes
        var names = self.names
        var known = Set(names.map { $0.uppercased() })
        var byMD5: [Data: Int] = [:]
        for (i, e) in entries.enumerated() where i != 0 { byMD5[e.md5] = i }
        let origEnd = try FileManager.default.attributesOfItem(atPath: url.path)[.size] as? Int ?? 0

        var pending: [(index: Int, data: Data)] = []
        for name in replacements.keys.sorted() {
            let data = replacements[name]!
            let md5 = PSARC.pathMD5(name)
            let entry = Entry(md5: md5, zindex: zsizes.count, length: data.count, offset: 0)
            zsizes += PSARC.blockSizes(data.count, blockSize)
            if let i = byMD5[md5] {
                entries[i] = entry
                pending.append((i, data))
            } else {
                entries.append(entry)
                pending.append((entries.count - 1, data))
            }
            if !known.contains(name.uppercased()) {
                names.append(name)
                known.insert(name.uppercased())
            }
        }
        if separator == 0 { names.sort() }
        let manifest = Data(names.map { Data($0.utf8) }.joined(separator: [separator]))
        entries[0] = Entry(md5: Data(count: 16), zindex: zsizes.count, length: manifest.count, offset: 0)
        zsizes += PSARC.blockSizes(manifest.count, blockSize)
        pending.append((0, manifest))
        guard (zsizes.max() ?? 0) < (1 << (8 * zsizeBytes)) else {
            throw CoopMod.Failure(message: "block size table overflow")
        }

        let tocLength = PSARC.headerSize + entries.count * PSARC.entrySize + zsizes.count * zsizeBytes
        let oldStart = dataStart
        let shift = tocLength <= oldStart ? 0 : (tocLength + 0xFFFF) / 0x10000 * 0x10000 - oldStart
        var appendAt = origEnd + shift
        let pendingIndices = Set(pending.map(\.index))
        for i in entries.indices where !pendingIndices.contains(i) { entries[i].offset += shift }
        var appended = Data()
        for p in pending {
            entries[p.index].offset = appendAt
            appendAt += p.data.count
            appended += p.data
        }
        // the manifest first, then the others in MD5 order
        let ordered = [entries[0]] + entries.dropFirst().sorted { $0.md5.lexicographicallyPrecedes($1.md5) }

        func bytes(_ v: Int, _ n: Int) -> Data { Data((0..<n).reversed().map { UInt8(truncatingIfNeeded: v >> (8 * $0)) }) }
        var head = Data("PSAR".utf8)
        head += bytes(Int(major), 2) + bytes(Int(minor), 2) + compression
        head += bytes(tocLength, 4) + bytes(PSARC.entrySize, 4) + bytes(ordered.count, 4)
        head += bytes(blockSize, 4) + bytes(Int(flags), 4)
        for e in ordered {
            head += e.md5 + bytes(e.zindex, 4) + bytes(e.length, 5) + bytes(e.offset, 5)
        }
        for s in zsizes { head += bytes(s, zsizeBytes) }
        head += Data(count: oldStart + shift - head.count)

        let partial = out.appendingPathExtension("partial")
        FileManager.default.createFile(atPath: partial.path, contents: nil)
        let writer = try FileHandle(forWritingTo: partial)
        let reader = try FileHandle(forReadingFrom: url)
        defer { try? reader.close() }
        do {
            try writer.write(contentsOf: head)
            try reader.seek(toOffset: UInt64(oldStart))
            while let buf = try reader.read(upToCount: 8 << 20), !buf.isEmpty {
                try writer.write(contentsOf: buf)
            }
            try writer.write(contentsOf: appended)
            try writer.close()
        } catch {
            try? writer.close()
            try? FileManager.default.removeItem(at: partial)
            throw error
        }
        _ = try? FileManager.default.removeItem(at: out)
        try FileManager.default.moveItem(at: partial, to: out)
    }
}
