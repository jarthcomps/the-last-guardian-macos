// SPDX-License-Identifier: GPL-2.0-or-later
import Foundation

/// Buffers bytes, not strings: pipes can split both a progress line and a UTF-8 character.
struct ExtractionLines {
    private var pending = Data()

    mutating func append(_ data: Data, endOfFile: Bool = false) -> [String] {
        pending.append(data)
        var lines: [String] = []
        while let end = pending.firstIndex(where: { $0 == 10 || $0 == 13 }) {
            if end > pending.startIndex {
                lines.append(String(decoding: pending[..<end], as: UTF8.self))
            }
            pending.removeSubrange(...end)
        }
        if endOfFile, !pending.isEmpty {
            lines.append(String(decoding: pending, as: UTF8.self))
            pending.removeAll()
        }
        return lines
    }
}

struct ExtractionProgress {
    enum State { case preparing, unpacking, finishing, complete, failed }
    private(set) var state = State.preparing
    private(set) var package = "Game"
    private(set) var packageNumber = 1
    let packageCount: Int
    private(set) var completedBytes: UInt64 = 0
    private(set) var totalBytes: UInt64 = 0
    let startedAt: Date
    private var bytesStartedAt: Date?
    private var finishedAt: Date?

    init(packageCount: Int = 1, now: Date = Date()) {
        self.packageCount = packageCount
        startedAt = now
    }

    mutating func receive(_ line: String, now: Date = Date()) {
        guard state != .complete, state != .failed else { return }
        if line.hasPrefix("Unpacking the ") {
            package = line.contains("update") ? "Update" : "Game"
            packageNumber = package == "Update" ? 2 : 1
            completedBytes = 0
            totalBytes = 0
            bytesStartedAt = nil
            state = .preparing
        } else if line.hasPrefix("TLG_BYTES ") {
            let fields = line.split(separator: " ")
            guard fields.count == 3, let done = UInt64(fields[1]),
                  let total = UInt64(fields[2]), total > 0, done <= total,
                  done >= completedBytes, totalBytes == 0 || total == totalBytes else { return }
            if bytesStartedAt == nil { bytesStartedAt = now }
            completedBytes = done
            totalBytes = total
            state = done == total ? .finishing : .unpacking
        }
    }

    mutating func complete(now: Date = Date()) { state = .complete; finishedAt = now }
    mutating func fail(now: Date = Date()) { state = .failed; finishedAt = now }

    var fraction: Double? {
        if state == .complete { return 1 }
        if state == .failed, totalBytes == 0 { return 0 }
        guard totalBytes > 0 else { return nil }
        // The installer still has to check the output and move the update into place.
        return min(0.99, Double(completedBytes) / Double(totalBytes))
    }

    var title: String {
        let stage = "\(package) (\(packageNumber) of \(packageCount))"
        switch state {
        case .preparing: return "Preparing \(stage.lowercased())…"
        case .unpacking: return "Unpacking \(stage.lowercased())…"
        case .finishing: return "Finishing \(stage.lowercased())…"
        case .complete: return "Unpacking complete"
        case .failed: return "Unpacking failed"
        }
    }

    var amount: String {
        guard totalBytes > 0 else {
            return state == .failed ? "No extraction progress was reported." : "Reading package contents…"
        }
        func size(_ value: UInt64) -> String {
            ByteCountFormatter.string(fromByteCount: Int64(clamping: value), countStyle: .file)
        }
        return "\(size(completedBytes)) of \(size(totalBytes)) unpacked"
    }

    var percent: String { fraction.map { "\(Int($0 * 100))%" } ?? "" }

    func timing(now: Date) -> String {
        let elapsed = max(0, (finishedAt ?? now).timeIntervalSince(startedAt))
        var text = "\(Self.duration(elapsed)) elapsed"
        if state == .preparing { return text + " · Scanning package…" }
        guard state == .unpacking, let bytesStartedAt else { return text }
        let measuring = max(0, now.timeIntervalSince(bytesStartedAt))
        guard measuring >= 5, completedBytes > 0, completedBytes < totalBytes else {
            return text + " · Estimating time remaining…"
        }
        let remaining = measuring * Double(totalBytes - completedBytes) / Double(completedBytes)
        text += " · About \(Self.duration(remaining)) remaining for this package"
        return text
    }

    static func duration(_ seconds: TimeInterval) -> String {
        let seconds = Int(min(Double(Int.max / 2), max(0, seconds.rounded(.up))))
        if seconds < 60 { return "\(seconds)s" }
        if seconds < 3600 { return "\(seconds / 60)m \(seconds % 60)s" }
        return "\(seconds / 3600)h \((seconds % 3600) / 60)m"
    }
}
