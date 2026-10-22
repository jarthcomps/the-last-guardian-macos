-- tlg-coop settings (the test runs install their own copy of this file).
CoopDebug.enabled = false
CoopTrico.strategy = "ctrl"
CoopPulse.method = "aimhold"
-- Cross toward a level jump the level hasn't opened yet: "always" opens it (Trico may leave the
-- boy behind: reload the checkpoint if he's stranded); "near" only with the boy within 4 m of
-- Trico or riding him (a jump on from a column only riding); "never" leaves them to the level
CoopActions.forceEdges = "always"
-- The Trico player's own lightning (hold L2 to aim, R2 to fire): off until an engine fix keeps
-- Trico's view while he aims. The boy's mirror-shield lightning is unaffected.
CoopPulse.enabled = false
