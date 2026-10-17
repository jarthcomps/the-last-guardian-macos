import XCTest
@testable import TLGLauncher

final class ProgressTests: XCTestCase {
    let start = Date(timeIntervalSince1970: 100)

    func testStartsIndeterminateUntilExtractorKnowsTotal() {
        let p = ExtractionProgress(now: start)
        XCTAssertNil(p.fraction)
        XCTAssertEqual(p.state, .preparing)
        XCTAssertTrue(p.timing(now: start.addingTimeInterval(3)).contains("3s elapsed"))
    }

    func testByteFractionAndEstimateUseActualMeasurements() {
        var p = ExtractionProgress(now: start)
        p.receive("TLG_BYTES 0 1000", now: start.addingTimeInterval(10))
        p.receive("TLG_BYTES 250 1000", now: start.addingTimeInterval(20))
        XCTAssertEqual(p.fraction, 0.25)
        XCTAssertEqual(p.percent, "25%")
        XCTAssertTrue(p.timing(now: start.addingTimeInterval(20)).contains("30s remaining"))
        XCTAssertTrue(p.timing(now: start.addingTimeInterval(20)).contains("20s elapsed"))
    }

    func testMalformedStaleAndRegressiveReportsAreIgnored() {
        var p = ExtractionProgress(now: start)
        p.receive("TLG_BYTES 250 1000")
        for line in ["TLG_BYTES x 1000", "TLG_BYTES -1 1000", "TLG_BYTES 50 0",
                     "TLG_BYTES 2000 1000", "TLG_BYTES 100 1000", "TLG_BYTES 500 2000",
                     "TLG_BYTES 500 1000 extra", "Progress: 50 / 100 files"] {
            p.receive(line)
        }
        XCTAssertEqual(p.fraction, 0.25)
    }

    func testLargePackagesDoNotOverflow() {
        var p = ExtractionProgress(now: start)
        p.receive("TLG_BYTES 12884901888 25769803776")
        XCTAssertEqual(p.percent, "50%")
        XCTAssertTrue(p.amount.contains("GB"))
    }

    func testUpdateHasItsOwnClearlyLabelledFractionAndEstimate() {
        var p = ExtractionProgress(packageCount: 2, now: start)
        p.receive("TLG_BYTES 1000 1000")
        p.receive("Unpacking the update…", now: start.addingTimeInterval(100))
        XCTAssertNil(p.fraction)
        XCTAssertEqual(p.title, "Preparing update (2 of 2)…")
        p.receive("TLG_BYTES 0 2000", now: start.addingTimeInterval(110))
        p.receive("TLG_BYTES 1000 2000", now: start.addingTimeInterval(120))
        XCTAssertEqual(p.percent, "50%")
        XCTAssertTrue(p.timing(now: start.addingTimeInterval(120)).contains("10s remaining"))
    }

    func testCompleteRequiresInstallerSuccessAndIgnoresLateLines() {
        var p = ExtractionProgress(now: start)
        p.receive("TLG_BYTES 1000 1000")
        XCTAssertEqual(p.state, .finishing)
        XCTAssertEqual(p.percent, "99%")
        p.complete()
        p.receive("Unpacking the game…")
        XCTAssertEqual(p.percent, "100%")
        XCTAssertEqual(p.state, .complete)
    }

    func testFailureDoesNotClaimCompletion() {
        var p = ExtractionProgress(now: start)
        p.receive("TLG_BYTES 250 1000")
        p.fail()
        p.receive("TLG_BYTES 1000 1000")
        XCTAssertEqual(p.percent, "25%")
        XCTAssertEqual(p.title, "Unpacking failed")
    }

    func testTerminalStatesStopElapsedTimerAndPreparationFailureStopsSpinner() {
        var p = ExtractionProgress(now: start)
        p.fail(now: start.addingTimeInterval(7))
        XCTAssertEqual(p.fraction, 0)
        XCTAssertEqual(p.timing(now: start.addingTimeInterval(100)), "7s elapsed")
        var success = ExtractionProgress(now: start)
        success.complete(now: start.addingTimeInterval(12))
        XCTAssertEqual(success.timing(now: start.addingTimeInterval(100)), "12s elapsed")
    }

    func testPipeFragmentsUTF8CRLFAndFinalUnterminatedLine() {
        var lines = ExtractionLines()
        let input = Data("Unpacking…\r\nTLG_BYTES 250 1000\nlast".utf8)
        var actual: [String] = []
        for byte in input { actual += lines.append(Data([byte])) }
        actual += lines.append(Data(), endOfFile: true)
        XCTAssertEqual(actual, ["Unpacking…", "TLG_BYTES 250 1000", "last"])
    }

    func testNoEstimateUntilEnoughDataAndNoNegativeElapsedTime() {
        var p = ExtractionProgress(now: start)
        p.receive("TLG_BYTES 0 1000", now: start)
        XCTAssertFalse(p.timing(now: start.addingTimeInterval(20)).contains("About"))
        p.receive("TLG_BYTES 250 1000", now: start)
        XCTAssertFalse(p.timing(now: start.addingTimeInterval(2)).contains("About"))
        XCTAssertTrue(p.timing(now: start.addingTimeInterval(-2)).contains("0s elapsed"))
    }

    func testLivePipeReportsBeforeExtractorExits() throws {
        let temp = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
        try FileManager.default.createDirectory(at: temp, withIntermediateDirectories: true)
        defer { try? FileManager.default.removeItem(at: temp) }
        let script = temp.appendingPathComponent("extract")
        try Data("""
        #!/bin/sh
        printf 'TLG_BY'
        /bin/sleep 0.1
        printf 'TES 250 1000\\n'
        /bin/sleep 0.3
        /usr/bin/touch "$2/finished"
        printf 'Extraction complete.\\n'
        """.utf8).write(to: script)
        try FileManager.default.setAttributes([.posixPermissions: 0o755], ofItemAtPath: script.path)
        let recorder = Recorder()
        try Launcher.extract(tool: script, pkg: temp.appendingPathComponent("game.pkg"), output: temp) {
            if $0.hasPrefix("TLG_BYTES") {
                recorder.live = !FileManager.default.fileExists(atPath: temp.path + "/finished")
                recorder.line = $0
            }
        }
        XCTAssertTrue(recorder.live)
        XCTAssertEqual(recorder.line, "TLG_BYTES 250 1000")
    }

    func testExitZeroWithoutCompletionMarkerFails() throws {
        let temp = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
        try FileManager.default.createDirectory(at: temp, withIntermediateDirectories: true)
        defer { try? FileManager.default.removeItem(at: temp) }
        let script = temp.appendingPathComponent("extract")
        try Data("#!/bin/sh\nprintf 'TLG_BYTES 1000 1000\\n'\n".utf8).write(to: script)
        try FileManager.default.setAttributes([.posixPermissions: 0o755], ofItemAtPath: script.path)
        XCTAssertThrowsError(try Launcher.extract(tool: script, pkg: temp, output: temp, progress: { _ in }))
    }
}

private final class Recorder: @unchecked Sendable {
    var live = false
    var line = ""
}
