import XCTest

final class CoopUITests: XCTestCase {
    var temp: URL!
    var app: XCUIApplication!

    override func setUpWithError() throws {
        continueAfterFailure = false
        temp = URL(fileURLWithPath: "/private/tmp/guardian-ui-tests").appendingPathComponent(UUID().uuidString)
        try FileManager.default.createDirectory(at: temp, withIntermediateDirectories: true)
        app = XCUIApplication()
        app.launchEnvironment = ["TLG_LAUNCHER_DATA_DIR": temp.appendingPathComponent("data").path]
        app.launch()
    }

    override func tearDownWithError() throws {
        app?.terminate()
        try? FileManager.default.removeItem(at: temp)
    }

    func savedCoop() -> Bool? {
        let file = temp.appendingPathComponent("data/launcher.json")
        guard let data = try? Data(contentsOf: file),
              let json = try? JSONSerialization.jsonObject(with: data) as? [String: Any] else { return nil }
        return json["coop"] as? Bool
    }

    func waitUntil(_ condition: @escaping () -> Bool, timeout: TimeInterval = 5) -> Bool {
        let end = Date().addingTimeInterval(timeout)
        while Date() < end {
            if condition() { return true }
            RunLoop.current.run(until: Date().addingTimeInterval(0.1))
        }
        return condition()
    }

    func testCoopOptionIsOffByDefaultAndIsSaved() throws {
        let toggle = app.checkBoxes["coop-toggle"]
        XCTAssertTrue(toggle.waitForExistence(timeout: 10), app.debugDescription)
        XCTAssertEqual(toggle.label, "Two-player co-op (experimental)")
        XCTAssertEqual(toggle.value as? Int, 0)
        XCTAssertFalse(app.staticTexts["coop-note"].exists)

        toggle.click()
        XCTAssertEqual(toggle.value as? Int, 1)
        XCTAssertTrue(app.staticTexts["coop-note"].waitForExistence(timeout: 5))
        XCTAssertTrue(waitUntil { self.savedCoop() == true })

        toggle.click()
        XCTAssertEqual(toggle.value as? Int, 0)
        XCTAssertTrue(waitUntil { self.savedCoop() == false })
        XCTAssertFalse(app.staticTexts["coop-note"].exists)
        let screenshot = XCTAttachment(screenshot: app.screenshot())
        screenshot.lifetime = .keepAlways
        add(screenshot)
    }
}
