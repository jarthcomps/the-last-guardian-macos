import XCTest

final class ProgressUITests: XCTestCase {
    var temp: URL!
    var app: XCUIApplication!

    override func setUpWithError() throws {
        continueAfterFailure = false
        temp = URL(fileURLWithPath: "/private/tmp/guardian-ui-tests").appendingPathComponent(UUID().uuidString)
        try FileManager.default.createDirectory(at: temp, withIntermediateDirectories: true)
    }

    override func tearDownWithError() throws {
        app?.terminate()
        try? FileManager.default.removeItem(at: temp)
    }

    func launch(update: Bool = false, fail: Bool = false) throws {
        app = XCUIApplication()
        app.launchEnvironment = [
            "TLG_LAUNCHER_DATA_DIR": temp.appendingPathComponent("data").path,
            "TLG_TEST_GAME_PKG": temp.appendingPathComponent("game.pkg").path,
            "TLG_TEST_DESTINATION": temp.appendingPathComponent("output").path,
            "TLG_TEST_EXTRACT_FAIL": fail ? "1" : "0"
        ]
        if update { app.launchEnvironment["TLG_TEST_UPDATE_PKG"] = temp.appendingPathComponent("update.pkg").path }
        app.launch()
    }

    func waitFor(_ element: XCUIElement, label: String, timeout: TimeInterval = 15) {
        let expectation = XCTNSPredicateExpectation(predicate: NSPredicate(format: "label == %@ OR value == %@", label, label), object: element)
        let result = XCTWaiter.wait(for: [expectation], timeout: timeout)
        if result != .completed { XCTFail("Expected \(label): \(app.debugDescription)") }
    }

    func text(_ element: XCUIElement) -> String { element.value as? String ?? element.label }

    func testVisibleBarTracksLiveOutputAndReaches100AfterSuccess() throws {
        try launch()
        let percent = app.staticTexts["extraction-percent"]
        waitFor(app.staticTexts["extraction-stage"], label: "Preparing game (1 of 1)…")
        waitFor(percent, label: "25%")
        XCTAssertTrue(app.progressIndicators["extraction-progress"].exists)
        XCTAssertFalse(app.buttons["install-pkgs"].isEnabled)
        XCTAssertTrue(text(app.staticTexts["extraction-amount"]).contains("unpacked"))
        XCTAssertTrue(text(app.staticTexts["extraction-timing"]).contains("elapsed"))
        waitFor(percent, label: "75%")
        waitFor(percent, label: "100%")
        XCTAssertEqual(text(app.staticTexts["extraction-stage"]), "Unpacking complete")
        XCTAssertTrue(app.buttons["install-pkgs"].isEnabled)
        let screenshot = XCTAttachment(screenshot: app.screenshot())
        screenshot.lifetime = .keepAlways
        add(screenshot)
    }

    func testUpdateStartsNewLabelledPackageAndCompletes() throws {
        try launch(update: true)
        waitFor(app.staticTexts["extraction-stage"], label: "Preparing update (2 of 2)…")
        waitFor(app.staticTexts["extraction-percent"], label: "25%")
        XCTAssertEqual(text(app.staticTexts["extraction-stage"]), "Unpacking update (2 of 2)…")
        waitFor(app.staticTexts["extraction-percent"], label: "100%")
        XCTAssertTrue(FileManager.default.fileExists(atPath: temp.path + "/output/CUSA03745-UPDATE/sce_sys/param.sfo"))
    }

    func testFailureKeepsActualFractionAndEnablesRetry() throws {
        try launch(fail: true)
        waitFor(app.staticTexts["extraction-stage"], label: "Unpacking failed")
        XCTAssertEqual(text(app.staticTexts["extraction-percent"]), "25%")
        XCTAssertTrue(app.buttons["install-pkgs"].isEnabled)
        XCTAssertTrue(app.staticTexts.containing(NSPredicate(format: "label CONTAINS %@ OR value CONTAINS %@", "fixture disk error", "fixture disk error")).firstMatch.exists)
    }
}
