-- tlg-coop settings (the test runs install their own copy of this file).
CoopDebug.enabled = false
CoopTrico.strategy = "ctrl"
CoopPulse.method = "aimhold"
-- Cross toward a level jump the level hasn't opened yet: "near" opens it only with the boy within
-- 4 m of Trico or riding him; "always" for tests; "never" leaves the level's jumps to the level
CoopActions.forceEdges = "near"
