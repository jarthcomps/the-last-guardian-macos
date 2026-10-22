dofile(string.match(debug.getinfo(1, "S").source, "^@(.*/)") .. "harness.lua")

Load("game/scripts/coop/coop_debug")
CoopTrico = { keepLastPos = false }   -- the tests put Trico at the origin on purpose
Load("game/scripts/coop/coop_trico")
Load("game/scripts/coop/coop_actions")

local function near(a, b) return math.abs(a - b) < 1e-3 end
local function btn(name, x, y, held)
	local i = { x = x or 0, y = y or 0, hold = {}, down = {} }
	if name then i.down[name] = not held; i.hold[name] = true end
	return i
end
local function lastCmd()
	local c = Harness.CallsNamed("cmd")
	return c[#c] and c[#c][3]
end

Harness.tricoProps = Harness.NewTricoProps(0, 0, 0)  -- facing +Z

-- the way: Trico's facing with the stick centred, else the stick camera-relative
local wx, wz = CoopActions.Way(Harness.tricoProps, btn())
check(near(wx, 0) and near(wz, 1), "no stick: Trico's facing")
Harness.objects._trico_camera = { worldPosition = Vector(0, 3, -8) }  -- forward +Z
wx, wz = CoopActions.Way(Harness.tricoProps, btn(nil, 1, 0))
check(near(wx, -1) and near(wz, 0), "stick right: -X")
CoopTrico.relative, CoopTrico.worldForward = "world", { 1, 0 }
wx, wz = CoopActions.Way(Harness.tricoProps, btn(nil, 0, 1))
check(near(wx, 1) and near(wz, 0), "debug world frame: stick up is the world way")
CoopTrico.relative, CoopTrico.worldForward, CoopTrico.latch = nil, nil, nil

-- Square: the attack gesture: an Instruction_Unique interest ahead, on the stick's side
local work = {}
Harness.tricoProps.currentInfo.work = work
local anim = "c_wait"
Ai2.Utils.GetCurAnimState = function() return anim end
local n = #Harness.CallsNamed("cmd")
CoopActions.Update(0.016, btn("Square", 0.8, 0))
local g = Harness.objects.CoopActions_Gesture
check(g and g.children[1].SubType == "Instruction_Unique", "gesture interest")
local moved = Harness.moved[#Harness.moved]
check(moved[1] == g and moved[2] == "-2,0,4.5", "4.5 m ahead, 2 m to the right")
local lv = Harness.CallsNamed("level")
check(lv[#lv][3] == g and lv[#lv][5] == "addMax", "top level")
check(#Harness.CallsNamed("cmd") == n and CoopActions.gesture.type == "Gesture_Attack", "no command, gesture waits")
check(work.EyeContact_GesturePermitAction == nil, "not granted before Trico looks at it")
-- Trico's interest becomes the target: granted as the handshake would
local target = { GetName = function() return "CoopActions_Gesture" end }
Harness.tricoProps.currentInfo.interestEntity = target
CoopActions.Update(0.1, btn())
check(work.EyeContact_GesturePermitAction == true and work.EyeContact_GesturePermitActionTarget == target
	and work.EyeContact_RecentGesture == "Gesture_Attack" and work.GestureType_ByInstruction == "Gesture_Attack",
	"granted")
-- the swipe starts: everything put back
anim = "ext_c_attack_yoko_right_TH"
CoopActions.Update(0.1, btn())
check(CoopActions.gesture == nil and work.EyeContact_GesturePermitAction == false
	and work.EyeContact_GestureMode == nil, "dropped once the swipe started")
check(#Harness.CallsNamed("levelReset") >= 1, "level taken away")
anim = "c_stand"
Harness.tricoProps.currentInfo.interestEntity = nil
CoopActions.Update(0.016, btn("Square", -0.8, 0))
check(Harness.moved[#Harness.moved][2] == "2,0,4.5", "left: 2 m to the left")
CoopTrico.Update(0.016, btn(nil, -0.8, 0))
check(not CoopTrico.steering, "steering waits while the gesture does")
CoopActions.Update(0.016, btn("Square"))
check(Harness.moved[#Harness.moved][2] == "-2,0,4.5", "alternates")
for _ = 1, 51 do CoopActions.Update(0.1, btn()) end
check(CoopActions.gesture == nil, "a gesture Trico never takes up times out")
n = #Harness.CallsNamed("level")
CoopActions.Update(0.016, btn("Square", 0, 0, true))
check(#Harness.CallsNamed("level") == n, "held, not pressed again: nothing")

-- D-pad up: rear up, re-sent every 0.5 s while held, stops with the button
CoopActions.Update(0.016, btn("Up"))
check(lastCmd() == "ControlCommand_C_rise", "rise")
n = #Harness.CallsNamed("cmd")
for _ = 1, 6 do CoopActions.Update(0.1, btn("Up", 0, 0, true)) end
check(#Harness.CallsNamed("cmd") == n + 1, "rise held: sent again after 0.5 s")
CoopActions.Update(0.1, btn())
for _ = 1, 10 do CoopActions.Update(0.1, btn()) end
check(#Harness.CallsNamed("cmd") == n + 1, "let go: no more")

-- Cross with nothing to jump to: the jump gesture (a pounce)
Harness.reachable = function() return true end
CoopActions.Update(0.016, btn("Cross"))
check(CoopActions.gesture and CoopActions.gesture.type == "Gesture_Jump" and CoopActions.jump == nil,
	"jump gesture")
check(Harness.moved[#Harness.moved][2] == "0,0,4.5", "straight ahead")
CoopActions.Release()
check(CoopActions.gesture == nil, "released")

-- Cross with a special edge ahead: an interest on its far side, held until Trico is there
-- (the island across starts 9 m ahead and 5 m up; walking there is impossible, the edge leads there)
Ai2.Utils.Navmesh.CheckSameIsland = function(props, from, to, excludeSpEdge)
	local across = to.Z > 9 and to.Y > 3
	if not across then return true end
	return not excludeSpEdge
end
local nCmd = #Harness.CallsNamed("cmd")
CoopActions.Update(0.016, btn("Cross"))
check(CoopActions.jump and near(CoopActions.jump.target.Z, 10) and near(CoopActions.jump.target.Y, 5),
	"jump target across the edge")
check(#Harness.CallsNamed("cmd") == nCmd and CoopActions.gesture == nil, "no gesture when there is an edge")
local root = Harness.objects.CoopActions_Jump
check(root and root.children[1].SubType == "intr_AutoTorico", "jump interest")
local lv = Harness.CallsNamed("level")
check(lv[#lv][3] == root and lv[#lv][5] == "addMax", "top level")
CoopActions.Update(0.5, btn())
check(CoopActions.jump ~= nil, "held while Trico is on its way")
Harness.tricoProps.currentInfo.aiPos = Vector(0, 5, 9)
CoopActions.Update(0.016, btn())
check(CoopActions.jump == nil and #Harness.CallsNamed("levelReset") >= 1, "dropped once Trico is there")
-- and given up after 10 s
Harness.tricoProps.currentInfo.aiPos = Vector(0, 0, 0)
CoopActions.Update(0.016, btn("Cross"))
for _ = 1, 101 do CoopActions.Update(0.1, btn()) end
check(CoopActions.jump == nil, "timed out")

-- the stick's way picks where to look
Ai2.Utils.Navmesh.CheckSameIsland = function(props, from, to, excludeSpEdge)
	local across = to.X < -9 and to.Y > 3      -- only to the right (-X)
	if not across then return true end
	return not excludeSpEdge
end
CoopActions.Update(0.016, btn("Cross", 1, 0))
check(CoopActions.jump and CoopActions.jump.target.X < -9, "jump to the right with the stick")
CoopTrico.Update(0.016, btn(nil, 1, 0))
check(not CoopTrico.steering, "steering waits while the jump target is held")
CoopActions.Release()
check(CoopActions.jump == nil, "released")

-- the level's hint first: remembered while Trico has it in mind, taken when it lies the way
local hintEnt = { worldPosition = Vector(0, 3, 20), GetName = function() return "HintIntr_1" end }
Harness.tricoProps.currentInfo.interestEntity = hintEnt
Harness.tricoProps.currentInfo.interestType = "CommonHint"
CoopActions.Update(0.016, btn())
Harness.tricoProps.currentInfo.interestEntity, Harness.tricoProps.currentInfo.interestType = nil, nil
CoopActions.Update(0.016, btn("Cross"))
check(CoopActions.jump and CoopActions.jump.entity == hintEnt, "the level's hint ahead taken")
lv = Harness.CallsNamed("level")
check(lv[#lv][3] == hintEnt and lv[#lv][5] == "addMax", "the hint to the top")
CoopActions.Release()
CoopActions.Update(0.016, btn("Cross", 0, -1))
check(not (CoopActions.jump and CoopActions.jump.entity == hintEnt), "a hint behind the stick's way: not taken")
CoopActions.Release()
CoopActions.hint = nil

-- the level's own jump spots first: Instruction interests in auto-Trico's search volume (attached
-- once the Trico player is there), across a special edge, the one nearest the stick's way
local function spot(name, x, y, z) return { worldPosition = Vector(x, y, z), GetName = function() return name end } end
local spots = { spot("Inst_back", 0, 6, -20), spot("Inst_walk", 2, 0, 20), spot("Inst_left", 20, 6, 4),
	spot("Inst_pillar", -3, 6, 25), spot("Inst_far", 0, 6, 60) }
local vol = { name = "autoTrico_findSpEdgeVol" }
Ai2.Utils.GetVolumeByAssetName = function(props, name) return name == vol.name and vol or nil end
Ai2.Utils.AttachVolume = Harness.Record("attach")
Ai2.Utils.DetachVolume = Harness.Record("detach")
Ai2.Utils.IsInVolumeSubType = function(v, types)
	check(v == vol and types[1] == "Instruction" and types[2] == "Instruction_30s", "searched the volume for Instructions")
	return true, spots
end
Ai2.Utils.Navmesh.CheckSameIsland = function(props, from, to, excludeSpEdge)
	if to.Y < 3 then return true end   -- the ground Trico walks on
	return not excludeSpEdge           -- the raised spots: only through a special edge
end
CoopActions.Update(0.016, btn())
check(#Harness.CallsNamed("attach") == 1, "volume attached ahead of the press")
CoopActions.Update(0.016, btn())
check(#Harness.CallsNamed("attach") == 1, "attached once")
local nMoved = #Harness.moved
CoopActions.Update(0.016, btn("Cross"))
check(CoopActions.jump and CoopActions.jump.entity == spots[4] and near(CoopActions.jump.target.Z, 25),
	"the pillar ahead (not the walkable spot, the one behind or the one out of range)")
lv = Harness.CallsNamed("level")
check(lv[#lv][3] == spots[4] and lv[#lv][4] == "CoopActions_Jump" and lv[#lv][5] == "addMax", "the spot's own interest to the top")
check(#Harness.moved == nMoved, "no made-up interest moved there")
check(CoopActions.gesture and CoopActions.gesture.type == "Gesture_Jump" and CoopActions.gesture.target == "Inst_pillar",
	"the jump gesture aimed at the spot")
-- granted once the spot is what he has in mind
Harness.tricoProps.currentInfo.interestEntity = spots[4]
CoopActions.Update(0.016, btn())
check(work.EyeContact_GestureMode == "Gesture_Jump" and work.InterestEntity_ByInstruction == spots[4], "granted at the spot")
Harness.tricoProps.currentInfo.interestEntity = nil
-- the stick to the left picks the spot that way
CoopActions.Update(0.016, btn("Cross", -1, 0))
check(CoopActions.jump.entity == spots[3], "the spot to the left")
local resets = Harness.CallsNamed("levelReset")
local letGo = false
for _, r in ipairs(resets) do if r[3] == spots[4] then letGo = true end end
check(letGo, "the earlier spot let go")
-- held up to 20 s, until he is within 5.5 m of it
for _ = 1, 150 do CoopActions.Update(0.1, btn()) end
check(CoopActions.jump ~= nil, "still held after 15 s")
Harness.tricoProps.currentInfo.aiPos = Vector(16, 0, 4)
CoopActions.Update(0.016, btn())
check(CoopActions.jump == nil, "there: dropped")
Harness.tricoProps.currentInfo.aiPos = Vector(0, 0, 0)
CoopActions.Release()
check(#Harness.CallsNamed("detach") == 1 and CoopActions.edgeVolume == nil, "volume detached on release")
-- a spot the navmesh checks can't link at all (reached through its own special approach): still
-- taken; and the stick let go and pushed again lets it go
Ai2.Utils.Navmesh.CheckSameIsland = function(props, from, to, excludeSpEdge) return to.Y < 3 end
CoopActions.Update(0.016, btn())
CoopActions.Update(0.016, btn("Cross"))
check(CoopActions.jump and CoopActions.jump.entity == spots[4], "an unlinked spot ahead is still taken")
CoopActions.Update(0.1, btn(nil, 0, 1))
check(CoopActions.jump ~= nil, "the stick still held from the press: kept")
CoopActions.Update(0.1, btn())
CoopActions.Update(0.1, btn(nil, 0, 1))
check(CoopActions.jump == nil, "the stick pushed again: let go")
check(CoopActions.gesture == nil, "its gesture let go too")
CoopActions.Release()
Ai2.Utils.GetVolumeByAssetName, Ai2.Utils.IsInVolumeSubType = nil, nil

-- Cross toward a special edge the level has switched off (the canyon pillar before the boy's
-- part): switched on by the "near" policy only with the boy within 4 m or riding, Trico sent 3 m
-- past its end, and switched off again unless he made the jump
local edgeLog = {}
Harness.objects.Edge_0 = { start = { worldPosition = Vector(0, 0, 5) }, ["end"] = { worldPosition = Vector(0, 4, 12) }, attr = "" }
Harness.objects.Edge_far = { start = { worldPosition = Vector(0, 0, 50) }, ["end"] = { worldPosition = Vector(0, 0, 60) }, attr = "" }
Class = function(c) if c == "AiSpPathEdge" then return { "Edge_0", "Edge_far" } end return {} end
local riding = false
MS = {
	SetEnableSpPathEdge = function(n) edgeLog[#edgeLog + 1] = "on " .. n Harness.objects[n].attr = "Valid" end,
	SetDisableSpPathEdge = function(n) edgeLog[#edgeLog + 1] = "off " .. n Harness.objects[n].attr = "" end,
	BoyIsRidingOnCondor = function() return riding end,
}
local boyPos = Vector(0, 0, -10)
local savedPlayer = Ai2.Utils.GetPlayerAI
Ai2.Utils.GetPlayerAI = function() return { pos = boyPos } end
Ai2.Utils.Navmesh.CheckSameIsland = function(props, from, to, ex) return (from.Z > 11) == (to.Z > 11) end
CoopActions.spEdgeNames = nil
local edges = CoopActions.SpEdges()
check(#edges == 2 and edges[1].name == "Edge_0" and not edges[1].valid and near(edges[1].to.Z, 12), "the level's edges listed")
check(CoopActions.FindSpEdge(Harness.tricoProps, 0, 1).name == "Edge_0", "the edge ahead found (the far one out of reach)")
check(CoopActions.FindSpEdge(Harness.tricoProps, 0, -1) == nil, "none behind")
CoopActions.Update(0.016, btn("Cross"))
check(#edgeLog == 0 and not (CoopActions.jump and CoopActions.jump.forced), "near policy, the boy 10 m away: left off")
CoopActions.Release()
boyPos = Vector(1, 0, -2)
CoopActions.Update(0.016, btn("Cross"))
local j = CoopActions.jump
check(edgeLog[1] == "on Edge_0" and j and j.forced == "Edge_0" and near(j.target.Z, 15) and near(j.target.Y, 4),
	"the boy beside him: switched on, Trico sent 3 m past its end")
check(Harness.moved[#Harness.moved][2] == "0,4,15" and CoopActions.forced.Edge_0, "the goal interest placed, the edge recorded")
CoopTrico.Update(0.016, btn(nil, 0, 1))
check(not CoopTrico.steering, "steering waits: his own planning takes the edge")
CoopActions.Update(0.1, btn())
CoopActions.Update(0.016, btn("Cross"))
check(CoopActions.jump == j and #edgeLog == 1, "Cross again: the same jump, the edge left on")
for _ = 1, 201 do CoopActions.Update(0.1, btn()) end
check(CoopActions.jump == nil and edgeLog[2] == "off Edge_0" and Harness.objects.Edge_0.attr == "", "timed out: off again")
-- made it: the edge stays as it is
CoopActions.Update(0.016, btn("Cross"))
Harness.tricoProps.currentInfo.aiPos = Vector(0, 4, 14)
CoopActions.Update(0.1, btn())
check(CoopActions.jump == nil and edgeLog[#edgeLog] == "on Edge_0" and Harness.objects.Edge_0.attr == "Valid", "on the far side: left on")
Harness.tricoProps.currentInfo.aiPos = Vector(0, 0, 0)
Harness.objects.Edge_0.attr = ""
-- riding counts as near; "always" and "never"; never in a scripted scene
boyPos, riding = Vector(30, 0, 30), true
CoopActions.Update(0.016, btn("Cross"))
check(CoopActions.jump and CoopActions.jump.forced == "Edge_0", "riding: switched on")
CoopActions.Release()
check(edgeLog[#edgeLog] == "off Edge_0", "released: off again")
riding = false
CoopActions.forceEdges = "always"
CoopActions.Update(0.016, btn("Cross"))
check(CoopActions.jump and CoopActions.jump.forced == "Edge_0", "always: switched on with the boy away")
CoopActions.Release()
Harness.tricoProps.currentInfo.controlMode = "EVENT"
local before = #edgeLog
CoopActions.Update(0.016, btn("Cross"))
check(#edgeLog == before, "never in a scripted scene")
Harness.tricoProps.currentInfo.controlMode = nil
CoopActions.Release()
CoopActions.forceEdges = "never"
CoopActions.Update(0.016, btn("Cross"))
check(#edgeLog == before, "never: left off")
CoopActions.Release()
CoopActions.forceEdges = "near"
Class, MS, CoopActions.spEdgeNames = nil, nil, nil
Harness.objects.Edge_0, Harness.objects.Edge_far = nil, nil
Ai2.Utils.GetPlayerAI = savedPlayer

-- nothing while the lightning is aimed (L2 held)
local aiming = btn("Square")
aiming.hold.L2 = true
CoopActions.Update(0.016, aiming)
check(CoopActions.gesture == nil, "no actions while aiming")

-- single moves: sent from an idle Trico, refused (and reported) otherwise; rest() lets the gap
-- after a sent move pass
local function rest() CoopActions.Update(1.1, btn()) end
anim = "c_stand"
rest() CoopActions.Update(0.016, btn("R1"))
check(lastCmd() == "ControlCommand_C_roar", "R1 roar")
rest() CoopActions.Update(0.016, btn("Triangle"))
check(lastCmd() == "ControlCommand_C_attack_below", "Triangle stomp")
rest() CoopActions.Update(0.016, btn("Right"))
check(lastCmd() == "ControlCommand_C_attack_hind_left", "Right kick")
rest() CoopActions.Update(0.016, btn("Right"))
check(lastCmd() == "ControlCommand_C_attack_hind_right", "kicks alternate")
rest() CoopActions.Update(0.016, btn("Left"))
check(lastCmd() == "ControlCommand_C_stretch_front", "Left stretch")
rest() CoopActions.Update(0.016, btn("Left"))
check(lastCmd() == "ControlCommand_C_stretch_up", "stretches in turn")
-- Down: sit, then lie down, then stand up
anim = "c_wait"
rest() CoopActions.Update(0.016, btn("Down"))
check(lastCmd() == "ControlCommand_C_sit", "Down sits")
anim = "c_sit_loop"
rest() CoopActions.Update(0.016, btn("Down"))
check(lastCmd() == "ControlCommand_C_lie_down", "Down again lies down")
anim = "c_lie_down_loop"
Harness.tricoProps.standCommand = "ControlCommand_C_stand_test"
rest() CoopActions.Update(0.016, btn("Down"))
check(lastCmd() == "ControlCommand_C_stand_test", "and stands up")
-- L1: the threat posture while held, ended on release
anim = "c_stand"
rest() CoopActions.Update(0.016, btn("L1"))
check(lastCmd() == "ControlCommand_C_threat_posture", "L1 threat")
CoopActions.Update(0.1, btn("L1", 0, 0, true))
check(lastCmd() == "ControlCommand_C_threat_posture", "held")
CoopActions.Update(0.1, btn())
check(lastCmd() == "ControlCommand_C_threat_posture_end", "ended on release")
-- a press is sent at once whatever Trico is doing (most of his own actions give way)
anim = "c_walkC"
rest()
CoopActions.sent = nil
n = #Harness.CallsNamed("cmd")
CoopActions.Update(0.016, btn("R1"))
check(#Harness.CallsNamed("cmd") == n + 1 and lastCmd() == "ControlCommand_C_roar" and CoopActions.pending == nil,
	"walking: roar sent at once")
anim = "c_roar"
CoopActions.Update(0.1, btn())
check(CoopActions.sent == nil, "and seen to start")
-- refused (three sends, no change): it waits for Trico to stand after all
anim = "c_walkC"
rest()
CoopActions.sent = nil
n = #Harness.CallsNamed("cmd")
CoopActions.Update(0.016, btn("Triangle"))
for _ = 1, 16 do CoopActions.Update(0.1, btn()) end
check(#Harness.CallsNamed("cmd") == n + 3 and CoopActions.pending and CoopActions.pending.name == "stomp",
	"refused three times: waits")
CoopTrico.Update(0.016, btn(nil, 0, 1))
anim = "c_stand"
CoopActions.Update(0.1, btn())
check(lastCmd() == "ControlCommand_C_attack_below" and CoopActions.pending == nil, "sent once he stands")
anim = "ext_c_attack_below_start"
CoopActions.Update(0.1, btn())
check(CoopActions.sent == nil, "started")
-- a queued move still lapses 8 s after its press
anim = "c_walkC"
rest()
CoopActions.sent = nil
CoopActions.Update(0.016, btn("Triangle"))
for _ = 1, 70 do CoopActions.Update(0.1, btn()) end
check(CoopActions.pending and CoopActions.pending.name == "stomp", "still waiting before 8 s")
for _ = 1, 12 do CoopActions.Update(0.1, btn()) end
check(CoopActions.pending == nil, "dropped 8 s after the press")
-- lying, a standing move asks with the stand command
anim = "ext_c_lie_down_loop"
rest() CoopActions.Update(0.016, btn("R1"))
check(lastCmd() == "ControlCommand_C_stand_test", "up from lying: the stand command")
CoopActions.Release()
-- presses made while one waits queue up behind it (two at most) and go in turn
anim = "c_walkC"
rest()
CoopActions.sent = nil
CoopActions.Update(0.016, btn("Triangle"))
for _ = 1, 16 do CoopActions.Update(0.1, btn()) end   -- refused: waits
n = #Harness.CallsNamed("cmd")
CoopActions.Update(0.016, btn("R1"))
CoopActions.Update(0.3, btn())
CoopActions.Update(0.016, btn("Left"))
check(#CoopActions.queue == 2 and CoopActions.queue[1].name == "stomp" and CoopActions.queue[2].name == "roar"
	and #Harness.CallsNamed("cmd") == n, "two queued, the third dropped")
anim = "c_stand"
CoopActions.Update(0.1, btn())
check(lastCmd() == "ControlCommand_C_attack_below" and CoopActions.pending.name == "roar", "the first goes once standing")
anim = "ext_c_attack_below"
CoopActions.Update(0.6, btn())
check(lastCmd() == "ControlCommand_C_attack_below", "the next waits for Trico to stand again")
anim = "c_stand"
CoopActions.Update(0.1, btn())
check(lastCmd() == "ControlCommand_C_roar" and CoopActions.pending == nil and #CoopActions.queue == 0, "then the next")
anim = "c_roar"
CoopActions.Update(0.1, btn())
-- L1 let go while the threat posture still waits: it leaves the queue, nothing to end
anim = "c_walkC"
rest()
CoopActions.sent = nil
CoopActions.Update(0.016, btn("Triangle"))
for _ = 1, 16 do CoopActions.Update(0.1, btn()) end   -- a refused stomp waits first
n = #Harness.CallsNamed("cmd")
CoopActions.Update(0.016, btn("L1"))
check(CoopActions.queue[2] and CoopActions.queue[2].name == "threat", "threat waits behind it")
CoopActions.Update(0.1, btn())
check(#CoopActions.queue == 1 and #Harness.CallsNamed("cmd") == n, "let go: unqueued, no end sent")
CoopActions.Release()

-- L3 tapped alone (a "turn" event): face the stick's way on the spot, or right round
anim = "c_stand"
CoopInput = { events = {} }
local function tap(input)
	CoopInput.events = { { kind = "turn" } }
	CoopActions.Update(0.016, input)
	CoopInput.events = {}
end
rest()
Harness.tricoProps.currentInfo.ai2Ext.dirXZ = Vector(0, 0, 1)
local function lastSlide()
	local c = Harness.CallsNamed("slide")
	return c[#c] and c[#c][3]
end
tap(btn())
check(lastSlide() and math.abs(lastSlide().destinationDir.Z + 1) < 1e-3, "centred stick: his own slide right round")
rest()
-- stick held the way Trico already faces: nothing to do
local before = #Harness.CallsNamed("slide")
CoopTrico.latch = nil
Harness.objects._trico_camera = { worldPosition = Vector(1, 3, -10) }   -- camera forward is about +Z
tap(btn(nil, 0, 1))
check(#Harness.CallsNamed("slide") == before, "already facing that way")
rest()
CoopTrico.latch = nil
tap(btn(nil, -1, 0))   -- stick left: the left of +Z is +X
check(lastSlide() and lastSlide().destinationDir.X > 0.9, "stick left: his own slide to face +X")
-- a turn event just after a swap is the swap's own L3: ignored
rest()
CoopActions.sent = nil   -- (the turn above is never seen to start here: no resends)
local nturn = #Harness.CallsNamed("slide")
CoopInput.events = { { kind = "swap", value = "boy" } }
CoopActions.Update(0.016, btn())
CoopInput.events = { { kind = "turn" } }
CoopActions.Update(0.5, btn())
CoopInput.events = {}
check(#Harness.CallsNamed("slide") == nturn, "no turn right after a swap")
CoopActions.Update(1.1, btn())
CoopActions.sent = nil
tap(btn())
check(#Harness.CallsNamed("slide") == nturn + 1, "but a tap later turns")
CoopActions.sent = nil
CoopInput = nil
CoopTrico.latch = nil
Harness.objects._trico_camera = nil
rest()

-- a posture move queued behind the gap leaves from sitting too
anim = "c_sit_loop"
rest()
CoopActions.Update(0.016, btn("Down"))
check(lastCmd() == "ControlCommand_C_lie_down", "lie down at once")
n = #Harness.CallsNamed("cmd")
CoopActions.Update(0.016, btn("Down"))
check(#Harness.CallsNamed("cmd") == n and CoopActions.pending.name == "lieDown", "a quick second press waits")
anim = "c_sit_to_lie_down"   -- Trico started the first one
CoopActions.Update(1.1, btn())
check(#Harness.CallsNamed("cmd") == n + 1 and lastCmd() == "ControlCommand_C_lie_down", "and goes from sitting")
CoopActions.Release()

-- a sent move is watched: sent again while Trico's animation doesn't change, three times at most
anim = "c_stand"
rest()
CoopDebug.enabled = true
n = #Harness.CallsNamed("cmd")
CoopActions.Update(0.016, btn("R1"))
check(#Harness.CallsNamed("cmd") == n + 1, "roar sent")
CoopActions.Update(0.3, btn())
check(#Harness.CallsNamed("cmd") == n + 1, "not resent within 0.5 s")
CoopActions.Update(0.3, btn())
check(#Harness.CallsNamed("cmd") == n + 2 and lastCmd() == "ControlCommand_C_roar", "resent")
CoopActions.Update(0.6, btn())
check(#Harness.CallsNamed("cmd") == n + 3, "and once more")
CoopActions.Update(0.6, btn())
check(#Harness.CallsNamed("cmd") == n + 3 and CoopActions.sent == nil, "then given up")
CoopDebug.Flush()
check(#Harness.Find("^AC roar not started") == 1, "the miss is logged")
-- started: no resend
rest()
n = #Harness.CallsNamed("cmd")
CoopActions.Update(0.016, btn("R1"))
anim = "ext_c_roar_start"
CoopActions.Update(0.1, btn())
CoopActions.Update(1.0, btn())
check(#Harness.CallsNamed("cmd") == n + 1 and CoopActions.sent == nil, "started: sent once")
CoopDebug.Flush()
check(#Harness.Find("^AC roar started anim=ext_c_roar_start") == 1, "the start is logged")
CoopDebug.enabled = false
Harness.lightbar = {}
anim = "c_stand"
rest()
-- while Trico sits, a standing move first asks it to stand
anim = "ext_c_sit_loop"
rest() CoopActions.Update(0.016, btn("Right"))
check(lastCmd() == "ControlCommand_C_sit_end" and (CoopActions.pending.name == "kickRight"
	or CoopActions.pending.name == "kickLeft"), "stands up first, with the end of sitting")
local asked = #Harness.CallsNamed("cmd")
CoopActions.Update(1.0, btn())
check(#Harness.CallsNamed("cmd") == asked, "not asked again at once")
CoopActions.Update(1.1, btn())
check(#Harness.CallsNamed("cmd") == asked + 1 and lastCmd() == "ControlCommand_C_sit_end", "asked again after 2 s")
anim = "c_stand"
CoopActions.Update(0.1, btn())
check(string.find(lastCmd(), "attack_hind") ~= nil, "then kicks")

-- Circle with the stick left or right: a paw pull, once Trico stands still (steering waits)
anim = "c_walkC"
n = #Harness.CallsNamed("cmd")
rest() CoopActions.Update(0.016, btn("Circle", -0.9, 0))
check(CoopActions.pending and CoopActions.pending.name == "pullLeft" and #Harness.CallsNamed("cmd") == n,
	"pull waits while Trico walks")
CoopTrico.Update(0.016, btn(nil, -0.9, 0))
check(not CoopTrico.steering, "steering waits for the pull")
anim = "c_stand"
CoopActions.Update(0.1, btn(nil, -0.9, 0))
check(lastCmd() == "ControlCommand_C_touch_left_pull" and CoopActions.pending == nil, "left paw pull once standing")
rest() CoopActions.Update(0.016, btn("Circle", 0.9, 0))
CoopActions.Update(0.1, btn())
check(lastCmd() == "ControlCommand_C_touch_right_pull", "right paw pull")
anim = "c_walkC"
n = #Harness.CallsNamed("cmd")
rest() CoopActions.Update(0.016, btn("Circle", 0.9, 0))
for _ = 1, 81 do CoopActions.Update(0.1, btn()) end
check(CoopActions.pending == nil and #Harness.CallsNamed("cmd") == n, "never still: refused after 8 s")
anim = "c_stand"

-- Circle near an edible barrel: the nearest one in reach (or in view) gets the top level for 15 s
local function barrelAt(name, x, z, seen)
	return { sensorEntity = { GetName = function() return name end, worldPosition = Vector(x, 0, z) },
		seen = seen }
end
local barrels = {}
Ai2.Utils.FindInterestByType = function(ai, typeName, checkExcl)
	check(typeName == "Barrel" and checkExcl == true, "asks for non-excluded barrels")
	return barrels
end
function Ai2IsInterestViewable(intr) return intr.seen and 1 or 0 end
barrels = { barrelAt("far", 0, 40), barrelAt("seen", 20, 0, true), barrelAt("near", 0, 9), barrelAt("nearer", 0, 6) }
Harness.tricoProps.currentInfo.aiPos = Vector(0, 0, 0)
local nl = #Harness.CallsNamed("level")
CoopActions.Update(0.016, btn("Circle"))
local lvl = Harness.CallsNamed("level")
check(#lvl == nl + 1 and lvl[#lvl][3]:GetName() == "nearer" and lvl[#lvl][4] == "CoopActions_Eat"
	and lvl[#lvl][5] == "addMax" and lvl[#lvl][6] == 15, "eats the nearest barrel in reach")
check(CoopActions.gesture == nil, "no Action gesture when eating")
barrels = { barrelAt("far", 0, 40), barrelAt("seen", 20, 0, true) }
CoopActions.Update(0.016, btn("Circle"))
lvl = Harness.CallsNamed("level")
check(lvl[#lvl][3]:GetName() == "seen", "a barrel in view counts further out")
barrels = { barrelAt("far", 0, 40), barrelAt("unseen", 20, 0, false) }
CoopActions.Update(0.016, btn("Circle"))
check(CoopActions.gesture and CoopActions.gesture.type == "Gesture_Action", "none in reach: Action")
CoopActions.Release()
barrels = {}

-- Circle: the Action gesture on Trico's interest, else on the dummy ahead
work = {}
Harness.tricoProps.currentInfo.work = work
local barrel = { GetName = function() return "Barrel_01" end }
Harness.tricoProps.currentInfo.interestEntity = barrel
CoopActions.Update(0.016, btn("Circle"))
check(work.EyeContact_GesturePermitAction and work.EyeContact_GesturePermitActionTarget == barrel
	and work.GestureType_ByInstruction == "Gesture_Action", "action on the barrel")
for _ = 1, 51 do CoopActions.Update(0.1, btn()) end
check(CoopActions.gesture == nil and work.EyeContact_GesturePermitAction == false, "action dropped after 5 s")
Harness.tricoProps.currentInfo.interestEntity = { GetName = function() return "Player" end }
CoopActions.Update(0.016, btn("Circle"))
check(CoopActions.gesture and CoopActions.gesture.type == "Gesture_Action" and CoopActions.gesture.target == nil,
	"nothing to act on: the dummy ahead")
CoopActions.Release()
Harness.tricoProps.currentInfo.interestEntity = nil

-- reports what was done and the animation after it
CoopDebug.enabled = true
Harness.lightbar = {}
anim = "ext_c_rise_start"
CoopActions.Update(0.016, btn("Up"))
CoopActions.Update(0.1, btn("Up", 0, 0, true))
CoopDebug.Flush()
check(#Harness.Find("^AC rise start anim=ext_c_rise_start") == 1, "action report")

-- errors are contained; no Trico: nothing
Ai2.Utils.ReqCtrlCmd = function() error("boom") end
CoopActions.Update(0.016, btn("Up"))
CoopDebug.Flush()
check(#Harness.Find("^AC error") == 1, "error reported")
Harness.tricoProps = nil
CoopActions.Update(0.016, btn("Square"))
print("ok")
