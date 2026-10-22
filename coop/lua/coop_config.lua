-- tlg-coop settings (the test runs install their own copy of this file).
CoopDebug.enabled = false
CoopTrico.strategy = "ctrl"
CoopPulse.method = "aimhold"
-- Cross toward a level jump the level hasn't opened yet: "always" opens it (Trico may leave the
-- boy behind: reload the checkpoint if he's stranded); "near" only with the boy within 4 m of
-- Trico or riding him (a jump on from a column only riding); "never" leaves them to the level
CoopActions.forceEdges = "always"
