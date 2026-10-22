dofile(string.match(debug.getinfo(1, "S").source, "^@(.*/)") .. "harness.lua")

Load("game/scripts/coop/coop_debug")
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

-- nothing while the lightning is aimed (L2 held)
local aiming = btn("Square")
aiming.hold.L2 = true
CoopActions.Update(0.016, aiming)
check(CoopActions.gesture == nil, "no actions while aiming")

-- single moves: sent from an idle Trico, refused (and reported) otherwise
anim = "c_stand"
CoopActions.Update(0.016, btn("R1"))
check(lastCmd() == "ControlCommand_C_roar", "R1 roar")
CoopActions.Update(0.016, btn("Triangle"))
check(lastCmd() == "ControlCommand_C_attack_below", "Triangle stomp")
CoopActions.Update(0.016, btn("Right"))
check(lastCmd() == "ControlCommand_C_attack_hind_left", "Right kick")
CoopActions.Update(0.016, btn("Right"))
check(lastCmd() == "ControlCommand_C_attack_hind_right", "kicks alternate")
CoopActions.Update(0.016, btn("Left"))
check(lastCmd() == "ControlCommand_C_stretch_front", "Left stretch")
CoopActions.Update(0.016, btn("Left"))
check(lastCmd() == "ControlCommand_C_stretch_up", "stretches in turn")
-- Down: sit, then lie down, then stand up
anim = "c_wait"
CoopActions.Update(0.016, btn("Down"))
check(lastCmd() == "ControlCommand_C_sit", "Down sits")
anim = "c_sit_loop"
CoopActions.Update(0.016, btn("Down"))
check(lastCmd() == "ControlCommand_C_lie_down", "Down again lies down")
anim = "c_lie_down_loop"
Harness.tricoProps.standCommand = "ControlCommand_C_stand_test"
CoopActions.Update(0.016, btn("Down"))
check(lastCmd() == "ControlCommand_C_stand_test", "and stands up")
-- L1: the threat posture while held, ended on release
anim = "c_stand"
CoopActions.Update(0.016, btn("L1"))
check(lastCmd() == "ControlCommand_C_threat_posture", "L1 threat")
CoopActions.Update(0.1, btn("L1", 0, 0, true))
check(lastCmd() == "ControlCommand_C_threat_posture", "held")
CoopActions.Update(0.1, btn())
check(lastCmd() == "ControlCommand_C_threat_posture_end", "ended on release")
-- while Trico walks a move waits (steering pauses) and goes once it stands; refused after 3 s
anim = "c_walkC"
n = #Harness.CallsNamed("cmd")
CoopActions.Update(0.016, btn("R1"))
check(#Harness.CallsNamed("cmd") == n and CoopActions.pending and CoopActions.pending.name == "roar", "roar waits")
CoopTrico.Update(0.016, btn(nil, 0, 1))
check(not CoopTrico.steering, "steering waits for the move")
anim = "c_stand"
CoopActions.Update(0.1, btn())
check(lastCmd() == "ControlCommand_C_roar" and CoopActions.pending == nil, "roar once standing")
anim = "c_walkC"
n = #Harness.CallsNamed("cmd")
CoopActions.Update(0.016, btn("Triangle"))
for _ = 1, 61 do CoopActions.Update(0.1, btn()) end
check(#Harness.CallsNamed("cmd") == n and CoopActions.pending == nil, "never standing: refused after 6 s")
-- the start of a walk is not standing
anim = "c_stand_walk_fastC"
CoopActions.Update(0.016, btn("R1"))
check(#Harness.CallsNamed("cmd") == n and CoopActions.pending, "c_stand_walk_fastC is walking")
CoopActions.Release()
-- while Trico sits, a standing move first asks it to stand
anim = "ext_c_sit_loop"
CoopActions.Update(0.016, btn("Right"))
check(lastCmd() == "ControlCommand_C_stand_test" and CoopActions.pending.name == "kickRight"
	or lastCmd() == "ControlCommand_C_stand_test" and CoopActions.pending.name == "kickLeft", "stands up first")
anim = "c_stand"
CoopActions.Update(0.1, btn())
check(string.find(lastCmd(), "attack_hind") ~= nil, "then kicks")

-- Circle with the stick left or right: a paw pull, once Trico stands still (steering waits)
anim = "c_walkC"
n = #Harness.CallsNamed("cmd")
CoopActions.Update(0.016, btn("Circle", -0.9, 0))
check(CoopActions.pending and CoopActions.pending.name == "pullLeft" and #Harness.CallsNamed("cmd") == n,
	"pull waits while Trico walks")
CoopTrico.Update(0.016, btn(nil, -0.9, 0))
check(not CoopTrico.steering, "steering waits for the pull")
anim = "c_stand"
CoopActions.Update(0.1, btn(nil, -0.9, 0))
check(lastCmd() == "ControlCommand_C_touch_left_pull" and CoopActions.pending == nil, "left paw pull once standing")
CoopActions.Update(0.016, btn("Circle", 0.9, 0))
CoopActions.Update(0.1, btn())
check(lastCmd() == "ControlCommand_C_touch_right_pull", "right paw pull")
anim = "c_walkC"
n = #Harness.CallsNamed("cmd")
CoopActions.Update(0.016, btn("Circle", 0.9, 0))
for _ = 1, 61 do CoopActions.Update(0.1, btn()) end
check(CoopActions.pending == nil and #Harness.CallsNamed("cmd") == n, "never still: refused after 6 s")
anim = "c_stand"

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
