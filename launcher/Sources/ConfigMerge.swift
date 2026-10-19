import Foundation

/// Brings a user's existing per-game config up to date with the launcher's defaults: keys the
/// defaults gained in a newer launcher are added, values the user already has are kept.
enum ConfigMerge {
    static func addingMissingKeys(defaults: [String: Any], to user: [String: Any]) -> [String: Any] {
        var merged = user
        for (key, value) in defaults {
            if let existing = merged[key] {
                if let nestedDefaults = value as? [String: Any],
                   let nestedUser = existing as? [String: Any] {
                    merged[key] = addingMissingKeys(defaults: nestedDefaults, to: nestedUser)
                }
            } else {
                merged[key] = value
            }
        }
        return merged
    }

    /// Updates `target` in place when the defaults have keys it lacks; returns whether it changed.
    @discardableResult
    static func update(target: URL, defaults: URL) throws -> Bool {
        guard let user = try JSONSerialization.jsonObject(with: Data(contentsOf: target))
                as? [String: Any],
              let base = try JSONSerialization.jsonObject(with: Data(contentsOf: defaults))
                as? [String: Any]
        else { return false }
        let merged = addingMissingKeys(defaults: base, to: user)
        guard !NSDictionary(dictionary: merged).isEqual(to: user) else { return false }
        try JSONSerialization.data(withJSONObject: merged, options: [.prettyPrinted, .sortedKeys])
            .write(to: target)
        return true
    }
}
