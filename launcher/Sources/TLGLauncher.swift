// The Last Guardian launcher for macOS.
//
// Sets up a self-contained shadPS4 user folder, links the player's own extracted copy of the
// game (and its optional update), optionally imports PS4 system modules, and starts the bundled
// shadPS4 build with the settings that keep the game stable and fast on Apple Silicon.
//
// SPDX-License-Identifier: GPL-2.0-or-later

import AppKit
import SwiftUI

// MARK: - param.sfo

/// Reads the key/value table of a PS4 `param.sfo` (title ID, version, title).
enum SFO {
    static func read(_ url: URL) -> [String: String]? {
        guard let data = try? Data(contentsOf: url), data.count >= 20,
              data.prefix(4) == Data([0x00, 0x50, 0x53, 0x46]) else { return nil }
        func u32(_ o: Int) -> Int {
            Int(data[o]) | Int(data[o + 1]) << 8 | Int(data[o + 2]) << 16 | Int(data[o + 3]) << 24
        }
        func u16(_ o: Int) -> Int { Int(data[o]) | Int(data[o + 1]) << 8 }
        let keys = u32(8), values = u32(12), count = u32(16)
        guard 20 + count * 16 <= data.count else { return nil }
        var result: [String: String] = [:]
        for i in 0..<count {
            let e = 20 + i * 16
            let keyStart = keys + u16(e), kind = u16(e + 2), length = u32(e + 4)
            let valueStart = values + u32(e + 12)
            guard keyStart < data.count, valueStart + length <= data.count,
                  let keyEnd = data[keyStart...].firstIndex(of: 0) else { return nil }
            let key = String(decoding: data[keyStart..<keyEnd], as: UTF8.self)
            let raw = data[valueStart..<valueStart + length]
            if kind == 0x404, length >= 4 {
                result[key] = String(u32(valueStart))
            } else {
                result[key] = String(decoding: raw.prefix { $0 != 0 }, as: UTF8.self)
            }
        }
        return result
    }
}

// MARK: - Model

/// Title IDs of The Last Guardian that the community resolution patches cover.
let supportedTitleIDs: Set<String> = ["CUSA03745", "CUSA03627", "CUSA04936", "CUSA05152"]
/// The release this build was tested with (US, update 1.03).
let testedTitleID = "CUSA03745"

struct GameInfo {
    let titleID: String
    let version: String
    let title: String
}

func gameInfo(_ folder: URL?) -> GameInfo? {
    guard let folder, let sfo = SFO.read(folder.appendingPathComponent("sce_sys/param.sfo"))
    else { return nil }
    return GameInfo(titleID: sfo["TITLE_ID"] ?? "", version: sfo["APP_VER"] ?? sfo["VERSION"] ?? "",
                    title: sfo["TITLE"] ?? "")
}

struct LauncherSettings: Codable {
    var gamePath: String?
    var updatePath: String?
    var resolution = "720p"
    var fullscreen = false
    var showFPS = false
    var metalTweaks = true
}

@MainActor
final class Launcher: ObservableObject {
    @Published var settings: LauncherSettings {
        didSet {
            save()
            message = ""
        }
    }
    @Published var message = ""
    @Published var running = false
    @Published var sysModuleCount = 0
    @Published var extracting = false
    @Published var extractionProgress: ExtractionProgress?

    let fm = FileManager.default
    let dataDir: URL
    var userDir: URL { dataDir.appendingPathComponent("user") }
    var libraryDir: URL { dataDir.appendingPathComponent("library") }
    var engineDir: URL { Bundle.main.resourceURL!.appendingPathComponent("engine") }
    var pkgTool: URL { Bundle.main.resourceURL!.appendingPathComponent("tools/ps4-pkg-tool") }
    var defaultsDir: URL { Bundle.main.resourceURL!.appendingPathComponent("defaults") }
    private var process: Process?

    init() {
        // TLG_LAUNCHER_DATA_DIR moves all launcher data elsewhere (used by the self-test).
        if let custom = ProcessInfo.processInfo.environment["TLG_LAUNCHER_DATA_DIR"] {
            dataDir = URL(fileURLWithPath: custom)
        } else {
            dataDir = fm.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
                .appendingPathComponent("The Last Guardian Launcher")
        }
        let file = dataDir.appendingPathComponent("launcher.json")
        settings = (try? JSONDecoder().decode(LauncherSettings.self, from: Data(contentsOf: file)))
            ?? LauncherSettings()
        try? fm.createDirectory(at: userDir, withIntermediateDirectories: true)
        refreshSysModules()
    }

    private func save() {
        try? fm.createDirectory(at: dataDir, withIntermediateDirectories: true)
        if let data = try? JSONEncoder().encode(settings) {
            try? data.write(to: dataDir.appendingPathComponent("launcher.json"))
        }
    }

    var game: GameInfo? { gameInfo(settings.gamePath.map { URL(fileURLWithPath: $0) }) }
    var update: GameInfo? { gameInfo(settings.updatePath.map { URL(fileURLWithPath: $0) }) }

    // MARK: Checks

    var rosettaInstalled: Bool {
        fm.fileExists(atPath: "/Library/Apple/usr/libexec/oah/libRosettaRuntime")
    }

    var macOSSupported: Bool {
        ProcessInfo.processInfo.isOperatingSystemAtLeast(
            OperatingSystemVersion(majorVersion: 26, minorVersion: 0, patchVersion: 0))
    }

    /// What stops the game from starting, or nil when it can start.
    var blocker: String? {
        if !macOSSupported { return "This build needs macOS 26 (Tahoe) or newer." }
        if !rosettaInstalled { return "Rosetta 2 is not installed (see below)." }
        guard settings.gamePath != nil else { return "Choose your extracted game folder." }
        guard let game else { return "The game folder has no sce_sys/param.sfo." }
        if !supportedTitleIDs.contains(game.titleID) {
            return "This folder is \(game.titleID), not The Last Guardian."
        }
        if !fm.fileExists(atPath: settings.gamePath! + "/eboot.bin") {
            return "The game folder has no eboot.bin."
        }
        if settings.updatePath != nil {
            guard let update else { return "The update folder has no sce_sys/param.sfo." }
            if update.titleID != game.titleID {
                return "The update is for \(update.titleID), the game is \(game.titleID)."
            }
        }
        if settings.resolution != "native" && update?.version != "01.03" {
            return "The \(settings.resolution) patch needs update 1.03. Add it or pick Native."
        }
        return nil
    }

    // MARK: Actions

    func choose(_ prompt: String) -> URL? {
        let panel = NSOpenPanel()
        panel.canChooseDirectories = true
        panel.canChooseFiles = false
        panel.allowsMultipleSelection = false
        panel.message = prompt
        return panel.runModal() == .OK ? panel.url : nil
    }

    func chooseGame() {
        guard let url = choose("Choose the extracted game folder (it contains eboot.bin).")
        else { return }
        settings.gamePath = url.path
        // An update next to the game with the usual name is picked up automatically.
        let sibling = url.deletingLastPathComponent()
            .appendingPathComponent(url.lastPathComponent + "-UPDATE")
        if settings.updatePath == nil, fm.fileExists(atPath: sibling.path + "/sce_sys/param.sfo") {
            settings.updatePath = sibling.path
        }
    }

    func chooseUpdate() {
        if let url = choose("Choose the extracted update folder (version 1.03).") {
            settings.updatePath = url.path
        }
    }

    // MARK: PKG install

    /// Runs the bundled extractor on one PKG into `output`; calls `progress` with its output lines.
    nonisolated static func extract(tool: URL, pkg: URL, output: URL,
                                    progress: @escaping @Sendable (String) -> Void) throws {
        let process = Process()
        process.executableURL = tool
        process.arguments = [pkg.path, output.path]
        let pipe = Pipe()
        process.standardOutput = pipe
        process.standardError = pipe
        try process.run()
        var diagnostic = ""
        var lines = ExtractionLines()
        var completed = false
        let handle = pipe.fileHandleForReading
        while true {
            let data = handle.availableData
            for line in lines.append(data, endOfFile: data.isEmpty) {
                if !line.hasPrefix("TLG_BYTES"), !line.hasPrefix("Progress:") {
                    diagnostic = String(line.prefix(600))
                }
                if line.hasPrefix("Extraction complete.") { completed = true }
                progress(line)
            }
            if data.isEmpty { break }
        }
        process.waitUntilExit()
        guard process.terminationStatus == 0, completed else {
            throw NSError(domain: "TLGLauncher", code: Int(process.terminationStatus),
                          userInfo: [NSLocalizedDescriptionKey:
                                        "Extracting \(pkg.lastPathComponent) failed.\n\(diagnostic)"])
        }
    }

    /// Unpacks the game PKG (and optionally the update PKG) into `destination` as
    /// `<TITLE ID>` and `<TITLE ID>-UPDATE`, the layout shadPS4 expects. Returns both folders.
    nonisolated static func installPKGs(tool: URL, game: URL, update: URL?, destination: URL,
                                        progress: @escaping @Sendable (String) -> Void) throws
        -> (game: URL, update: URL?) {
        let fm = FileManager.default
        try fm.createDirectory(at: destination, withIntermediateDirectories: true)
        let before = Set((try? fm.contentsOfDirectory(atPath: destination.path)) ?? [])
        progress("Unpacking the game…")
        try extract(tool: tool, pkg: game, output: destination, progress: progress)
        let created = ((try? fm.contentsOfDirectory(atPath: destination.path)) ?? [])
            .filter { !before.contains($0) && $0.hasPrefix("CUSA") }
        let titleID = created.first ?? supportedTitleIDs.first {
            fm.fileExists(atPath: destination.appendingPathComponent($0 + "/eboot.bin").path)
        } ?? ""
        let gameFolder = destination.appendingPathComponent(titleID)
        guard !titleID.isEmpty, fm.fileExists(atPath: gameFolder.path + "/eboot.bin") else {
            throw NSError(domain: "TLGLauncher", code: 1, userInfo: [NSLocalizedDescriptionKey:
                "The game PKG did not unpack to a game folder."])
        }
        guard let update else { return (gameFolder, nil) }
        // The update unpacks under its own title ID too: stage it, then rename to -UPDATE.
        let staging = destination.appendingPathComponent(".update-staging")
        try? fm.removeItem(at: staging)
        progress("Unpacking the update…")
        try extract(tool: tool, pkg: update, output: staging, progress: progress)
        let staged = staging.appendingPathComponent(titleID)
        guard fm.fileExists(atPath: staged.path + "/sce_sys/param.sfo") else {
            throw NSError(domain: "TLGLauncher", code: 2, userInfo: [NSLocalizedDescriptionKey:
                "The update PKG is not for \(titleID)."])
        }
        let updateFolder = destination.appendingPathComponent(titleID + "-UPDATE")
        try? fm.removeItem(at: updateFolder)
        try fm.moveItem(at: staged, to: updateFolder)
        try? fm.removeItem(at: staging)
        return (gameFolder, updateFolder)
    }

    func choosePKG(_ prompt: String) -> URL? {
        let panel = NSOpenPanel()
        panel.canChooseFiles = true
        panel.canChooseDirectories = false
        panel.allowedContentTypes = []
        panel.allowsOtherFileTypes = true
        panel.message = prompt
        return panel.runModal() == .OK ? panel.url : nil
    }

    /// Asks for the game PKG, the optional update PKG and where to unpack them, then unpacks.
    func installFromPKG() {
        guard let gamePKG = choosePKG("Choose the game PKG (about 12 GB).") else { return }
        let updatePKG = choosePKG("Choose the update 1.03 PKG (optional, Cancel to skip).")
        guard let destination = choose(
            "Choose where to unpack the game. It needs about 27 GB of free space.")
        else { return }
        startInstall(gamePKG: gamePKG, updatePKG: updatePKG, destination: destination)
    }

    private enum InstallEvent: Sendable {
        case progress(String)
        case success(URL, URL?)
        case failure(String)
    }

    func startInstall(gamePKG: URL, updatePKG: URL?, destination: URL, tool: URL? = nil) {
        guard !extracting, !running else { return }
        extracting = true
        extractionProgress = ExtractionProgress(packageCount: updatePKG == nil ? 1 : 2)
        message = ""
        let tool = tool ?? pkgTool
        let (events, continuation) = AsyncStream<InstallEvent>.makeStream()
        Task.detached {
            do {
                let result = try Launcher.installPKGs(
                    tool: tool, game: gamePKG, update: updatePKG, destination: destination,
                    progress: { continuation.yield(.progress($0)) })
                continuation.yield(.success(result.game, result.update))
            } catch {
                continuation.yield(.failure(error.localizedDescription))
            }
            continuation.finish()
        }
        // A single consumer preserves order, including the final success/error event.
        Task {
            for await event in events {
                switch event {
                case .progress(let line): extractionProgress?.receive(line)
                case .success(let game, let update):
                    settings.gamePath = game.path
                    if let update { settings.updatePath = update.path }
                    extractionProgress?.complete()
                    extracting = false
                    message = "Unpacked. Ready to play."
                case .failure(let error):
                    extractionProgress?.fail()
                    extracting = false
                    message = error
                }
            }
        }
    }

    func refreshSysModules() {
        let dir = userDir.appendingPathComponent("sys_modules")
        sysModuleCount = ((try? fm.contentsOfDirectory(atPath: dir.path)) ?? [])
            .filter { $0.lowercased().hasSuffix(".sprx") || $0.lowercased().hasSuffix(".prx") }.count
    }

    /// Copies PS4 firmware modules (*.sprx) the player dumped from their own console.
    func importSysModules() {
        guard let source = choose("Choose a folder of PS4 system modules (.sprx files).")
        else { return }
        let target = userDir.appendingPathComponent("sys_modules")
        try? fm.createDirectory(at: target, withIntermediateDirectories: true)
        var copied = 0
        if let items = fm.enumerator(at: source, includingPropertiesForKeys: nil) {
            for case let file as URL in items
            where ["sprx", "prx"].contains(file.pathExtension.lowercased()) {
                let destination = target.appendingPathComponent(file.lastPathComponent)
                try? fm.removeItem(at: destination)
                if (try? fm.copyItem(at: file, to: destination)) != nil { copied += 1 }
            }
        }
        refreshSysModules()
        message = copied == 0 ? "No .sprx files were found in that folder."
                              : "Imported \(copied) system modules."
    }

    func removeSysModules() {
        try? fm.removeItem(at: userDir.appendingPathComponent("sys_modules"))
        refreshSysModules()
        message = "System modules removed."
    }

    /// Creates the shadPS4 user folder from the bundled defaults (never overwrites changes).
    func prepareUserFolder(titleID: String) throws {
        try fm.createDirectory(at: userDir.appendingPathComponent("custom_configs"),
                               withIntermediateDirectories: true)
        try fm.createDirectory(at: userDir.appendingPathComponent("patches"),
                               withIntermediateDirectories: true)
        try fm.createDirectory(at: libraryDir, withIntermediateDirectories: true)
        // shadPS4 asks (in a dialog) whether to migrate old saves whenever it creates the first
        // user's folder, even on a fresh install. Creating the user folders first avoids it.
        for user in 1000...1003 {
            for sub in ["savedata", "trophy", "inputs"] {
                try fm.createDirectory(at: userDir.appendingPathComponent("home/\(user)/\(sub)"),
                                       withIntermediateDirectories: true)
            }
        }
        let config = userDir.appendingPathComponent("config.json")
        if !fm.fileExists(atPath: config.path) {
            var json = try JSONSerialization.jsonObject(
                with: Data(contentsOf: defaultsDir.appendingPathComponent("config.json")))
                as! [String: Any]
            var general = json["General"] as? [String: Any] ?? [:]
            general["install_dirs"] = [["enabled": true, "path": libraryDir.path]]
            general["addon_install_dir"] = userDir.appendingPathComponent("addcont").path
            json["General"] = general
            try JSONSerialization.data(withJSONObject: json, options: [.prettyPrinted, .sortedKeys])
                .write(to: config)
        }
        for (name, suffix) in [("game.json", ".json"), ("game.readback.json", ".readback.json")] {
            let target = userDir.appendingPathComponent("custom_configs/\(titleID)\(suffix)")
            if !fm.fileExists(atPath: target.path) {
                try fm.copyItem(at: defaultsDir.appendingPathComponent("custom_configs/\(name)"),
                                to: target)
            }
        }
        for patch in ["Guardian-720p.xml", "Guardian-540p.xml"] {
            let target = userDir.appendingPathComponent("patches/\(patch)")
            try? fm.removeItem(at: target)
            try fm.copyItem(at: defaultsDir.appendingPathComponent("patches/\(patch)"), to: target)
        }
    }

    /// The engine finds an update as "<game folder>-UPDATE" next to the game, so both are
    /// linked side by side in the launcher's library folder.
    func linkGame(titleID: String) throws -> URL {
        let gameLink = libraryDir.appendingPathComponent(titleID)
        let updateLink = libraryDir.appendingPathComponent(titleID + "-UPDATE")
        for link in [gameLink, updateLink] {
            if (try? fm.destinationOfSymbolicLink(atPath: link.path)) != nil {
                try fm.removeItem(at: link)
            }
        }
        try fm.createSymbolicLink(atPath: gameLink.path, withDestinationPath: settings.gamePath!)
        if let updatePath = settings.updatePath {
            try fm.createSymbolicLink(atPath: updateLink.path, withDestinationPath: updatePath)
        }
        return gameLink
    }

    func environment() -> [String: String] {
        var env = ProcessInfo.processInfo.environment
        // shadPS4 also points the Vulkan loader at the manifest next to its executable.
        env["VK_DRIVER_FILES"] = engineDir.appendingPathComponent("kosmickrisp_mesa_icd.json").path
        env["DYLD_LIBRARY_PATH"] = engineDir.path
        return PerformanceEnvironment.apply(to: env, metalTweaks: settings.metalTweaks)
    }

    func start(_ arguments: [String], what: String) {
        let process = Process()
        process.executableURL = engineDir.appendingPathComponent("shadps4")
        process.arguments = arguments
        process.environment = environment()
        // The engine uses ./user as its user folder when it exists.
        process.currentDirectoryURL = dataDir
        process.terminationHandler = { [weak self] finished in
            Task { @MainActor in
                self?.running = false
                self?.process = nil
                if finished.terminationStatus != 0 {
                    self?.message = "shadPS4 stopped with code \(finished.terminationStatus). "
                        + "The log is in the data folder (user/log)."
                }
            }
        }
        do {
            try process.run()
            self.process = process
            running = true
            message = "\(what) is running."
        } catch {
            message = "Could not start shadPS4: \(error.localizedDescription)"
        }
    }

    /// Prepares the user folder and game links and returns the engine arguments.
    func playArguments() throws -> [String] {
        guard let game else { throw CocoaError(.fileNoSuchFile) }
        try prepareUserFolder(titleID: game.titleID)
        let gameLink = try linkGame(titleID: game.titleID)
        var args = ["--fullscreen", settings.fullscreen ? "true" : "false"]
        if settings.showFPS { args.append("--show-fps") }
        if settings.resolution != "native" {
            args += ["--patch",
                     userDir.appendingPathComponent("patches/Guardian-\(settings.resolution).xml").path]
        }
        if settings.updatePath == nil { args.append("--ignore-game-patch") }
        args.append(gameLink.appendingPathComponent("eboot.bin").path)
        return args
    }

    func play() {
        if let blocker { message = blocker; return }
        do {
            start(try playArguments(), what: "The Last Guardian")
        } catch {
            message = "Could not set up the game: \(error.localizedDescription)"
        }
    }

    /// shadPS4's own interface (big picture mode: game list and all emulator settings).
    func openShadPS4() {
        if !rosettaInstalled { message = "Install Rosetta 2 first (see below)."; return }
        do {
            try prepareUserFolder(titleID: game?.titleID ?? testedTitleID)
            if let game { _ = try linkGame(titleID: game.titleID) }
            start(["--big-picture"], what: "shadPS4")
        } catch {
            message = "Could not set up shadPS4: \(error.localizedDescription)"
        }
    }

    func stop() { process?.terminate() }

    func openDataFolder() {
        try? fm.createDirectory(at: userDir, withIntermediateDirectories: true)
        NSWorkspace.shared.open(dataDir)
    }

    func installRosetta() {
        let script = "tell application \"Terminal\" to do script "
            + "\"softwareupdate --install-rosetta --agree-to-license\""
        NSAppleScript(source: script)?.executeAndReturnError(nil)
        NSWorkspace.shared.open(URL(fileURLWithPath: "/System/Applications/Utilities/Terminal.app"))
    }
}

// MARK: - Interface

struct ExtractionProgressView: View {
    let progress: ExtractionProgress

    var body: some View {
        TimelineView(.periodic(from: .now, by: 1)) { context in
            VStack(alignment: .leading, spacing: 6) {
                HStack {
                    Text(progress.title).font(.callout).bold()
                        .accessibilityLabel(progress.title)
                        .accessibilityIdentifier("extraction-stage")
                    Spacer()
                    Text(progress.percent).monospacedDigit()
                        .accessibilityLabel(progress.percent)
                        .accessibilityIdentifier("extraction-percent")
                }
                .accessibilityElement(children: .contain)
                if let fraction = progress.fraction {
                    ProgressView(value: fraction, total: 1)
                        .accessibilityLabel("Package extraction")
                        .accessibilityValue(progress.percent)
                        .accessibilityIdentifier("extraction-progress")
                } else {
                    ProgressView().controlSize(.small)
                        .accessibilityLabel("Reading package contents")
                }
                Text(progress.amount).font(.caption)
                    .accessibilityLabel(progress.amount)
                    .accessibilityIdentifier("extraction-amount")
                Text(progress.timing(now: context.date)).font(.caption).foregroundStyle(.secondary)
                    .accessibilityLabel(progress.timing(now: context.date))
                    .accessibilityIdentifier("extraction-timing")
            }
            .accessibilityElement(children: .contain)
        }
    }
}

struct PathRow: View {
    let label: String
    let path: String?
    let detail: String
    let choose: () -> Void
    let clear: (() -> Void)?

    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            HStack {
                Text(label).font(.headline)
                Spacer()
                if let clear, path != nil { Button("Clear", action: clear) }
                Button("Choose…", action: choose)
            }
            Text(path ?? "Not set").font(.caption).foregroundStyle(.secondary)
                .lineLimit(1).truncationMode(.middle)
            if !detail.isEmpty { Text(detail).font(.caption) }
        }
    }
}

struct ContentView: View {
    @StateObject var launcher = Launcher()

    var gameDetail: String {
        guard launcher.settings.gamePath != nil else { return "" }
        guard let game = launcher.game else { return "⚠︎ Not a PS4 game folder (no sce_sys/param.sfo)." }
        let tested = game.titleID == testedTitleID ? "" : " (untested region)"
        return "\(game.title) · \(game.titleID) · version \(game.version)\(tested)"
    }

    var updateDetail: String {
        guard launcher.settings.updatePath != nil else {
            return "Optional, but the 720p/540p patches need update 1.03."
        }
        guard let update = launcher.update else { return "⚠︎ Not a PS4 update folder." }
        return "Update \(update.version) · \(update.titleID)"
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 14) {
            HStack(spacing: 12) {
                Image(systemName: "gamecontroller.fill").font(.system(size: 34))
                VStack(alignment: .leading) {
                    Text("The Last Guardian").font(.title2).bold()
                    Text("for macOS · powered by shadPS4").foregroundStyle(.secondary)
                }
            }
            GroupBox {
                VStack(alignment: .leading, spacing: 12) {
                    HStack {
                        VStack(alignment: .leading, spacing: 4) {
                            Text("Install from PKG files").font(.headline)
                            Text("Unpacks your game PKG and update PKG once, then fills in the "
                                 + "folders below.").font(.caption)
                        }
                        Spacer()
                        Button(launcher.extracting ? "Unpacking…" : "Choose PKGs…",
                               action: launcher.installFromPKG)
                            .accessibilityIdentifier("install-pkgs")
                            .disabled(launcher.extracting || launcher.running)
                    }
                    if let progress = launcher.extractionProgress {
                        ExtractionProgressView(progress: progress)
                    }
                    Divider()
                    PathRow(label: "Game folder", path: launcher.settings.gamePath,
                            detail: gameDetail, choose: launcher.chooseGame,
                            clear: { launcher.settings.gamePath = nil })
                        .disabled(launcher.extracting)
                    Divider()
                    PathRow(label: "Update folder", path: launcher.settings.updatePath,
                            detail: updateDetail, choose: launcher.chooseUpdate,
                            clear: { launcher.settings.updatePath = nil })
                        .disabled(launcher.extracting)
                    Divider()
                    HStack {
                        VStack(alignment: .leading, spacing: 4) {
                            Text("System modules").font(.headline)
                            Text(launcher.sysModuleCount == 0
                                 ? "Optional. None imported."
                                 : "\(launcher.sysModuleCount) imported.")
                                .font(.caption)
                        }
                        Spacer()
                        if launcher.sysModuleCount > 0 {
                            Button("Remove", action: launcher.removeSysModules)
                        }
                        Button("Import…", action: launcher.importSysModules)
                    }
                }.padding(6)
            }
            GroupBox {
                VStack(alignment: .leading, spacing: 8) {
                    Picker("Resolution", selection: $launcher.settings.resolution) {
                        Text("720p (recommended)").tag("720p")
                        Text("540p (fastest)").tag("540p")
                        Text("Native 1080p (slow)").tag("native")
                    }
                    Toggle("Full screen", isOn: $launcher.settings.fullscreen)
                    Toggle("Show frame rate", isOn: $launcher.settings.showFPS)
                    Toggle("Mac performance and feather fixes (recommended)",
                           isOn: $launcher.settings.metalTweaks)
                }.padding(6)
            }
            if !launcher.rosettaInstalled {
                HStack {
                    Text("Rosetta 2 is needed to run shadPS4.").font(.callout)
                    Spacer()
                    Button("Install Rosetta…", action: launcher.installRosetta)
                }
            }
            HStack {
                Button("Open data folder", action: launcher.openDataFolder)
                Button("shadPS4 settings…", action: launcher.openShadPS4)
                    .disabled(launcher.running || launcher.extracting)
                    .help("Opens shadPS4's own interface (big picture mode) with all its settings.")
                Spacer()
                if launcher.running {
                    Button("Stop", action: launcher.stop)
                } else {
                    Button("Play") { launcher.play() }
                        .keyboardShortcut(.defaultAction)
                        .disabled(launcher.blocker != nil || launcher.extracting)
                }
            }
            Text(launcher.extracting ? "Your game folders will be filled in when unpacking finishes."
                 : launcher.message.isEmpty ? (launcher.blocker ?? "Ready to play.") : launcher.message)
                .font(.callout).foregroundStyle(.secondary)
                .fixedSize(horizontal: false, vertical: true)
        }
        .padding(20)
        .frame(width: 560)
        #if DEBUG
        // XCTest uses the real installer and a small deterministic extractor fixture.
        .task {
            let env = ProcessInfo.processInfo.environment
            if let pkg = env["TLG_TEST_GAME_PKG"], let dest = env["TLG_TEST_DESTINATION"],
               let tool = Bundle.main.url(forResource: "extract-fixture", withExtension: "sh") {
                launcher.startInstall(gamePKG: URL(fileURLWithPath: pkg),
                    updatePKG: env["TLG_TEST_UPDATE_PKG"].map { URL(fileURLWithPath: $0) },
                    destination: URL(fileURLWithPath: dest), tool: tool)
            }
        }
        #endif
    }
}

/// `TLGLauncher --self-test` checks the saved setup without a window: prints what Play would
/// run, or why it cannot, and exits (0 = ready).
@MainActor
func selfTest() -> Int32 {
    let launcher = Launcher()
    if let blocker = launcher.blocker {
        print("blocked: \(blocker)")
        return 1
    }
    do {
        let args = try launcher.playArguments()
        let env = launcher.environment()
        print("engine: \(launcher.engineDir.appendingPathComponent("shadps4").path)")
        print("arguments: \(args.joined(separator: " "))")
        for key in ["VK_DRIVER_FILES", "DYLD_LIBRARY_PATH", "MESA_KK_DISABLE_WORKAROUNDS",
                    "SHADPS4_METAL_ENCODER_ORDERING", "SHADPS4_PREFETCH_STRICT_UNMARK",
                    "SHADPS4_TEXTURE_LOOKUP_REUSE", "SHADPS4_DOWNLOAD_RESCAN_REUSE",
                    "SHADPS4_IMAGE_BACKING_READS", "SHADPS4_READBACK_WINDOW_KB",
                    "SHADPS4_DYNAMIC_STATE_REUSE", "SHADPS4_RANGE_PROTECTION",
                    "KK_UPLOAD_CACHE_EXPANDED"] {
            print("\(key)=\(env[key] ?? "")")
        }
        print("working directory: \(launcher.dataDir.path)")
        print("system modules: \(launcher.sysModuleCount)")
        return 0
    } catch {
        print("error: \(error.localizedDescription)")
        return 1
    }
}

@main
struct TLGLauncherApp: App {
    init() {
        let args = CommandLine.arguments
        if args.contains("--self-test") {
            exit(MainActor.assumeIsolated { selfTest() })
        }
        // TLGLauncher --install-pkg GAME.pkg [UPDATE.pkg] DESTINATION: the PKG install, no window.
        if let i = args.firstIndex(of: "--install-pkg"), args.count - i >= 3 {
            let rest = Array(args[(i + 1)...])
            let tool = Bundle.main.resourceURL!.appendingPathComponent("tools/ps4-pkg-tool")
            do {
                let result = try Launcher.installPKGs(
                    tool: tool, game: URL(fileURLWithPath: rest[0]),
                    update: rest.count >= 3 ? URL(fileURLWithPath: rest[1]) : nil,
                    destination: URL(fileURLWithPath: rest[rest.count - 1]),
                    progress: { print($0); fflush(stdout) })
                print("game: \(result.game.path)")
                print("update: \(result.update?.path ?? "none")")
                exit(0)
            } catch {
                print("error: \(error.localizedDescription)")
                exit(1)
            }
        }
    }

    var body: some Scene {
        WindowGroup("The Last Guardian") { ContentView() }
            .windowResizability(.contentSize)
    }
}
