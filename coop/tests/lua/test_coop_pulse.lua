dofile(string.match(debug.getinfo(1, "S").source, "^@(.*/)") .. "harness.lua")

Load("game/scripts/coop/coop_debug")
Load("game/scripts/coop/coop_pulse")
check(CoopPulse.method == "aimhold", "aimhold is the default")
CoopPulse.method = "emblem"

local function near(a, b) return math.abs(a - b) < 1e-3 end
local function pad(rx, ry, r2)
	return { x = 0, y = 0, rx = rx or 0, ry = ry or 0, hold = { R2 = r2 }, down = { R2 = r2 } }
end

-- cursor steps are camera-relative (forward +Z, right -X) and need the stick out of the dead zone
check(CoopPulse.Step(0.1, 0.1, 0, 1, 0.5) == nil, "dead zone")
local dx, dz = CoopPulse.Step(0, 1, 0, 1, 0.5)
check(near(dx, 0) and near(dz, 4), "up for half a second: 4 m forward")
dx, dz = CoopPulse.Step(1, 0, 0, 1, 0.25)
check(near(dx, -2) and near(dz, 0), "right is -X")

-- the cursor stays 4..25 m from Trico
local x, z = CoopPulse.Clamp(Vector(0, 0, 0), 0, 1)
check(near(z, 4), "too close: pushed out to 4 m")
x, z = CoopPulse.Clamp(Vector(0, 0, 0), 30, 40)
check(near(x, 15) and near(z, 20), "too far: pulled in to 25 m")

-- the shield comes from BoyHaveTrigger when the stage scripts are loaded, else GameFlag
check(CoopPulse.HasShield() == false, "no shield")
GameFlag.HaveTrigger = 1
check(CoopPulse.HasShield() == true, "shield from GameFlag")
BoyHaveTrigger = function() return false end
check(CoopPulse.HasShield() == false, "BoyHaveTrigger wins")
BoyHaveTrigger = nil
MS = { BoyHaveTrigger = function() return false end }
check(CoopPulse.HasShield() == false, "MS.BoyHaveTrigger (as the mission scripts call it)")
MS = nil

-- no shield: the cursor never shows and R2 does nothing
GameFlag.HaveTrigger = 0
Harness.tricoProps = Harness.NewTricoProps(0, 0, 0)
Harness.tricoProps.currentInfo.ai2Ext.tailPulse = { state = "None" }
Harness.objects["game/effects/trigger_mark_boy"] = { _object_name = "game/effects/trigger_mark_boy" }
Harness.objects["game/objects/props/chara/tritrigger_01/tritrigger_01"] = { name = "trigger" }
local inst = {}
Harness.objects.PlayerAI = { instruction = inst }
CoopPulse.Update(0.1, pad(0, 1, true))
check(not CoopPulse.active and CoopPulse.refused == "no-shield", "refused without the shield")
check(#Harness.CallsNamed("free") == 0 and inst.triggerObj == nil, "nothing touched")

-- with the shield: the right stick brings the cursor out 10 m ahead (Trico's facing, no camera)
GameFlag.HaveTrigger = 1
CoopPulse.Update(0.1, pad(0, 1))
check(CoopPulse.active and near(CoopPulse.cursor.Z, 10.8) and near(CoopPulse.cursor.Y, 0.5),
	"cursor 10 m ahead, moved 0.8 m, on the ground")
local cursor = Harness.objects[CoopPulse.cursorName]
check(cursor and cursor.source == "game/effects/trigger_mark_boy" and cursor.entityFlags == "~Hide",
	"cursor effect cloned and shown")
check(Harness.moved[#Harness.moved][1] == cursor, "cursor effect moved")

-- R2 fires through the boy's trigger emblem fields
CoopPulse.Update(0.1, pad(0, 0, true))
check(#Harness.CallsNamed("free") == 1 and #Harness.CallsNamed("tail") >= 1, "free mode, tail aimed")
check(inst.triggerObj and inst.triggerObj.name == "trigger" and inst.triggerIsDirectAim
	and inst.triggerIsDemo and near(inst.triggerAimPos.Z, 10.8), "emblem aimed at the cursor")
check(CoopPulse.shots == 1 and CoopPulse.firing > 9.5, "firing")
-- the aim follows the cursor while firing
CoopPulse.Update(0.25, pad(0, 1))
check(near(inst.triggerAimPos.Z, 12.8), "aim follows the cursor: " .. inst.triggerAimPos.Z .. " " .. CoopPulse.cursor.Z)
-- R2 again while firing does not fire twice
CoopPulse.Update(0.1, pad(0, 0, true))
check(CoopPulse.shots == 1, "one shot at a time")
-- the aim holds while the pulse aims and shoots, and ends once it has shot
local tp = Harness.tricoProps.currentInfo.ai2Ext.tailPulse
tp.state = "Aim"
for _ = 1, 20 do CoopPulse.Update(0.1, pad()) end
check(CoopPulse.firing > 0 and inst.triggerObj ~= nil, "still aiming after 2 s of Aim")
tp.state = "Shot"
CoopPulse.Update(0.1, pad())
check(CoopPulse.shot and inst.triggerObj ~= nil, "shot seen, aim still held")
tp.state = "Finalize"
CoopPulse.Update(0.1, pad())
check(CoopPulse.firing <= 0 and inst.triggerObj == nil and not inst.triggerIsDemo, "emblem cleared after the shot")
check(#Harness.CallsNamed("tailReset") == 1 and #Harness.CallsNamed("unfree") == 1, "pulse settings reset")
tp.state = "Idle"

-- the cursor goes away after 8 s without aiming (counted once the shot is over) and comes back
-- where it was
check(CoopPulse.active, "cursor still up right after the shot")
local spot = CoopPulse.cursor
for _ = 1, 81 do CoopPulse.Update(0.1, pad()) end
check(not CoopPulse.active and cursor.entityFlags == "|Hide", "cursor hidden when idle")
CoopPulse.Update(0.1, pad(0, 0, true))
check(CoopPulse.active and near(CoopPulse.cursor.Z, spot.Z), "back at the same spot")
-- with nothing from the pulse the aim gives up after 10 s
for _ = 1, 101 do CoopPulse.Update(0.1, pad()) end
check(CoopPulse.firing <= 0 and inst.triggerObj == nil, "gave up after 10 s")
for _ = 1, 81 do CoopPulse.Update(0.1, pad()) end
check(not CoopPulse.active, "hidden again")

-- the boy's own aim is never overridden
local boyTrigger = { name = "boy's" }
inst.triggerObj = boyTrigger
CoopPulse.Update(0.1, pad(0, 1, true))
check(CoopPulse.refused == "boy-aiming" and inst.triggerObj == boyTrigger, "refused while the boy aims")
check(#Harness.CallsNamed("free") == 2, "no free mode for that")
inst.triggerObj = nil

-- the command method: ready on R2, shot a second later; losing the shield cancels
CoopPulse.method = "command"
CoopPulse.Update(0.1, pad(0, 0, true))
local cmds = Harness.CallsNamed("cmd")
check(#cmds == 1 and cmds[1][3] == "ControlCommand_C_A_tail_pulse_ready", "ready")
check(inst.triggerObj == nil, "no emblem in the command method")
for _ = 1, 11 do CoopPulse.Update(0.1, pad()) end
cmds = Harness.CallsNamed("cmd")
check(#cmds == 2 and cmds[2][3] == "ControlCommand_C_A_tail_pulse_shot", "shot after a second")
GameFlag.HaveTrigger = 0
CoopPulse.Update(0.1, pad())
cmds = Harness.CallsNamed("cmd")
check(cmds[#cmds][3] == "ControlCommand_C_A_tail_pulse_cancel" and not CoopPulse.active, "cancelled")
check(#Harness.Find("PU shield false") == 0, "(debug off: no messages)")

-- both: the emblem and the ready command; the shot goes as soon as the pulse aims
CoopPulse.method = "both"
GameFlag.HaveTrigger = 1
local tpb = Harness.tricoProps.currentInfo.ai2Ext.tailPulse
tpb.state = "Search"
local before = #Harness.CallsNamed("cmd")
CoopPulse.Update(0.1, pad(0, 1, true))
cmds = Harness.CallsNamed("cmd")
check(#cmds == before + 1 and cmds[#cmds][3] == "ControlCommand_C_A_tail_pulse_ready", "both: ready")
check(inst.triggerObj ~= nil, "both: emblem")
CoopPulse.Update(0.1, pad())
check(#Harness.CallsNamed("cmd") == before + 1, "no shot while searching")
tpb.state = "Aim"
CoopPulse.Update(0.1, pad())
cmds = Harness.CallsNamed("cmd")
check(#cmds == before + 2 and cmds[#cmds][3] == "ControlCommand_C_A_tail_pulse_shot", "both: shot on Aim")
tpb.state = "Shot"
CoopPulse.Update(0.1, pad())
tpb.state = "Finalize"
CoopPulse.Update(0.1, pad())
check(inst.triggerObj == nil and CoopPulse.firing <= 0, "both: done after the shot")
tpb.state = "Idle"
-- and the shot also goes after 2 s if the pulse never aims
CoopPulse.Update(0.1, pad(0, 0, true))
for _ = 1, 21 do CoopPulse.Update(0.1, pad()) end
cmds = Harness.CallsNamed("cmd")
check(cmds[#cmds][3] == "ControlCommand_C_A_tail_pulse_shot", "both: shot after 2 s")
CoopPulse.Release()
GameFlag.HaveTrigger = 0
CoopPulse.Update(0.1, pad())

-- aim: the emblem is the cursor while the cursor is up; R2 then readies and shoots
CoopPulse.method = "aim"
GameFlag.HaveTrigger = 1
local cmdsBefore = #Harness.CallsNamed("cmd")
CoopPulse.Update(0.1, pad(0, 1))
check(CoopPulse.active and inst.triggerObj ~= nil and CoopPulse.ownEmblem, "aim: emblem shown with the cursor")
local z0 = inst.triggerAimPos.Z
CoopPulse.Update(0.25, pad(0, 1))
check(inst.triggerAimPos.Z > z0, "aim: emblem follows the cursor")
check(#Harness.CallsNamed("cmd") == cmdsBefore, "aim: no commands before R2")
CoopPulse.Update(0.1, pad(0, 0, true))
cmds = Harness.CallsNamed("cmd")
check(cmds[#cmds][3] == "ControlCommand_C_A_tail_pulse_ready", "aim: ready on R2")
for _ = 1, 21 do CoopPulse.Update(0.1, pad()) end
cmds = Harness.CallsNamed("cmd")
check(cmds[#cmds][3] == "ControlCommand_C_A_tail_pulse_shot", "aim: shot")
for _ = 1, 80 do CoopPulse.Update(0.1, pad()) end
for _ = 1, 81 do CoopPulse.Update(0.1, pad()) end
check(not CoopPulse.active and inst.triggerObj == nil, "aim: emblem gone with the cursor")
-- the boy's own aim still wins
inst.triggerObj = boyTrigger
CoopPulse.Update(0.1, pad(0, 1))
check(inst.triggerObj == boyTrigger and not CoopPulse.ownEmblem, "aim: boy's aim left alone")
inst.triggerObj = nil
CoopPulse.Release()
GameFlag.HaveTrigger = 0
CoopPulse.Update(0.1, pad())

-- hold: aim and fire while R2 is held, stop when it is let go
CoopPulse.method = "hold"
GameFlag.HaveTrigger = 1
local nb = #Harness.CallsNamed("cmd")
local function held(rx, ry) return { x = 0, y = 0, rx = rx or 0, ry = ry or 0, hold = { R2 = true }, down = {} } end
CoopPulse.Update(0.1, pad(0, 1))
check(CoopPulse.active and inst.triggerObj == nil, "hold: cursor without R2 shows no emblem")
CoopPulse.Update(0.1, pad(0, 0, true))
check(inst.triggerObj ~= nil and CoopPulse.firing > 0, "hold: emblem on R2")
cmds = Harness.CallsNamed("cmd")
check(#cmds == nb + 1 and cmds[#cmds][3] == "ControlCommand_C_A_tail_pulse_ready", "hold: ready")
local tph = Harness.tricoProps.currentInfo.ai2Ext.tailPulse
tph.state = "Aim"
CoopPulse.Update(0.1, held())
cmds = Harness.CallsNamed("cmd")
check(cmds[#cmds][3] == "ControlCommand_C_A_tail_pulse_shot", "hold: shot on Aim")
tph.state = "Shot"
for _ = 1, 150 do CoopPulse.Update(0.1, held(0, 1)) end
check(inst.triggerObj ~= nil and CoopPulse.firing > 0, "hold: still firing after 15 s held")
tph.state = "Finalize"
CoopPulse.Update(0.1, held())
check(inst.triggerObj ~= nil, "hold: the shot ending does not stop a held R2")
CoopPulse.Update(0.1, pad())
check(inst.triggerObj == nil and CoopPulse.firing <= 0, "hold: let go, put back")
cmds = Harness.CallsNamed("cmd")
check(cmds[#cmds][3] == "ControlCommand_C_A_tail_pulse_shot", "hold: no cancel after a shot")
-- let go before it shot: cancelled
tph.state = "Idle"
CoopPulse.Update(0.1, pad(0, 0, true))
CoopPulse.Update(0.1, pad())
cmds = Harness.CallsNamed("cmd")
check(cmds[#cmds][3] == "ControlCommand_C_A_tail_pulse_cancel" and inst.triggerObj == nil, "hold: early let-go cancels")
CoopPulse.Release()
GameFlag.HaveTrigger = 0
CoopPulse.Update(0.1, pad())

-- aimhold: L2 held aims with the emblem as the cursor (7 m ahead), the beam is hidden, R2 shoots
CoopPulse.method = "aimhold"
GameFlag.HaveTrigger = 1
local ray = { entityFlags = "TransformInWorld" }
inst.rayObj = ray
local function l2(rx, ry, r2)
	return { x = 0, y = 0, rx = rx or 0, ry = ry or 0, hold = { L2 = true, R2 = r2 }, down = { R2 = r2 } }
end
local na = #Harness.CallsNamed("cmd")
local nf = #Harness.CallsNamed("free")
CoopPulse.Update(0.1, pad(0, 1))
check(not CoopPulse.active and inst.triggerObj == nil, "aimhold: the right stick alone does nothing")
CoopPulse.Update(0.1, pad(0, 0, true))
check(not CoopPulse.active and #Harness.CallsNamed("cmd") == na, "aimhold: R2 without L2 does nothing")
Harness.tricoProps.currentInfo.ai2Ext.tailPulse.state = "Idle"
CoopPulse.Update(0.1, l2())
check(CoopPulse.active and inst.triggerObj ~= nil and near(CoopPulse.cursor.Z, 7), "aimhold: emblem 7 m ahead")
check(#Harness.CallsNamed("free") == nf + 1 and #Harness.CallsNamed("cmd") == na, "aimhold: free mode, no commands")
check(ray.entityFlags == "|Hide", "aimhold: beam hidden")
CoopPulse.Update(0.25, l2(0, 1))
check(near(inst.triggerAimPos.Z, 9), "aimhold: the right stick moves the emblem")
for _ = 1, 150 do CoopPulse.Update(0.1, l2()) end
check(inst.triggerObj ~= nil and CoopPulse.active, "aimhold: held 15 s, still aiming")
CoopPulse.Update(0.1, l2(0, 0, true))
cmds = Harness.CallsNamed("cmd")
check(#cmds == na + 2 and cmds[na + 1][3] == "ControlCommand_C_A_tail_pulse_ready"
	and cmds[na + 2][3] == "ControlCommand_C_A_tail_pulse_shot", "aimhold: R2 readies and shoots at once")
Harness.tricoProps.currentInfo.ai2Ext.tailPulse.state = "Aim"
CoopPulse.Update(0.1, l2())
cmds = Harness.CallsNamed("cmd")
check(#cmds == na + 3 and cmds[#cmds][3] == "ControlCommand_C_A_tail_pulse_shot", "aimhold: shot again on Aim")
CoopPulse.Update(0.1, l2())
check(#Harness.CallsNamed("cmd") == na + 3, "aimhold: only once")
Harness.tricoProps.currentInfo.ai2Ext.tailPulse.state = "Shot"
CoopPulse.Update(0.1, l2())
CoopPulse.Update(0.1, pad())
check(inst.triggerObj == nil and not CoopPulse.active and ray.entityFlags == "TransformInWorld",
	"aimhold: let go, emblem cleared and the beam's own flags back")
check(#Harness.CallsNamed("cmd") == na + 3, "aimhold: no cancel after a shot")
-- let go before any shot: cancelled
Harness.tricoProps.currentInfo.ai2Ext.tailPulse.state = "Idle"
CoopPulse.Update(0.1, l2())
CoopPulse.Update(0.1, pad())
cmds = Harness.CallsNamed("cmd")
check(cmds[#cmds][3] == "ControlCommand_C_A_tail_pulse_cancel", "aimhold: early let-go cancels")
-- the boy's own aim wins, his beam untouched
inst.triggerObj = boyTrigger
ray.entityFlags = "TransformInWorld"
CoopPulse.Update(0.1, l2(0, 0, true))
check(inst.triggerObj == boyTrigger and CoopPulse.refused == "boy-aiming" and ray.entityFlags == "TransformInWorld",
	"aimhold: refused while the boy aims")
inst.triggerObj = nil
-- no shield: nothing
GameFlag.HaveTrigger = 0
CoopPulse.Update(0.1, l2())
check(not CoopPulse.active and inst.triggerObj == nil, "aimhold: no shield, no aim")
CoopPulse.Update(0.1, pad())
inst.rayObj = nil
CoopPulse.method = "hold"

-- reports: the shield on change, the pulse state twice a second
CoopDebug.enabled = true
GameFlag.HaveTrigger = 1
Harness.tricoProps.currentInfo.ai2Ext.tailPulse = { state = "Aim", mode = "Electricity", targetEntName = "t1" }
CoopPulse.reportIn = 0
CoopPulse.Update(0.1, pad())
CoopDebug.Flush()
check(#Harness.Find("^PU shield true") == 1, "shield change reported")
check(#Harness.Find("^PU hold sh=true aim=%? st=Aim md=Electricity tg=t1 zap=false") == 1,
	"state report: " .. tostring((Harness.Find("^PU hold")[1] or {})[2]))

-- leaving the Trico role puts things back
CoopPulse.method = "emblem"
CoopPulse.Update(0.1, pad(0, 1, true))
check(inst.triggerObj ~= nil, "firing again")
CoopPulse.Release()
check(inst.triggerObj == nil and not CoopPulse.active, "released")

-- no Trico yet: nothing happens
Harness.tricoProps = nil
CoopPulse.Update(0.1, pad(0, 1, true))
print("ok")
