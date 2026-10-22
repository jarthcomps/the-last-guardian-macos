dofile(string.match(debug.getinfo(1, "S").source, "^@(.*/)") .. "harness.lua")

-- the game camera as far as coop_camera.lua touches it: FixParam(name) looks the set up and pins
-- it (a fresh wrapper each time, as a native call would return), FixParam() unpins
local cam = { paramName = "Stand", fixedParam = nil }
local sets = { TricoAutoRunTest_middle = true, TricoAutoRunTest_far = true, TricoEye = true }
local fixes = {}
-- one object per set, as the game keeps them (FixParam hands it back in a fresh wrapper)
local objects = {}
local function param(name)
	objects[name] = objects[name] or { Rod_LengthBase = 40, Rod_PitchBaseDeg = -85, Rod_YawBasisType = "Global",
		Rod_YawBaseDeg = 180, Base_FovDeg = 70 }
	local obj = objects[name]
	return setmetatable({ GetName = function() return "gamecamera_param_" .. name end },
		{ __index = obj, __newindex = obj })
end
GameCamera = {
	GetCamera = function(self) return cam end,
	FixParam = function(self, name)
		fixes[#fixes + 1] = name or "-"
		cam.fixedParam = name and sets[name] and param(name) or nil
	end,
}
local active = "game"
CameraManager = { IsActiveCamera = function(self, key) return key == active end }

Load("game/scripts/coop/coop_debug")
Load("game/scripts/coop/coop_camera")

-- toggle off (solo as the boy): the camera is never touched
for _ = 1, 30 do CoopCamera.Update(0.1, false) end
check(#fixes == 0 and CoopCamera.mode == "boy", "untouched without Select")

-- Select on: the Trico framing is pinned at once, then left alone
CoopCamera.Update(0.016, true)
check(#fixes == 1 and fixes[1] == "TricoAutoRunTest_middle" and CoopCamera.mode == "trico", "Trico framing")
check(CoopCamera.pinned == "gamecamera_param_TricoAutoRunTest_middle", "pinned by name")
local tm = objects.TricoAutoRunTest_middle
check(tm.Rod_LengthBase == 14 and tm.Rod_PitchBaseDeg == -25 and tm.Rod_YawBasisType == "Global"
	and tm.Base_FovDeg == 75, "retuned: 14 m, 25 degrees down, a world yaw, wider")
for _ = 1, 30 do CoopCamera.Update(0.1, true) end
check(#fixes == 1, "not pinned again while it holds")

-- Select off: given back at once
CoopCamera.Update(0.016, false)
check(#fixes == 2 and fixes[2] == "-" and cam.fixedParam == nil and CoopCamera.mode == "boy", "boy framing")
check(tm.Rod_LengthBase == 40 and tm.Rod_PitchBaseDeg == -85 and tm.Rod_YawBasisType == "Global"
	and tm.Base_FovDeg == 70, "the set's own values back")
for _ = 1, 30 do CoopCamera.Update(0.1, false) end
check(#fixes == 2, "nothing more with the toggle off")

-- a scripted scene pins its own set: leave it, and never unpin it
CoopCamera.Update(0.016, true)
check(#fixes == 3, "Trico framing again")
cam.fixedParam = param("TricoEye")
for _ = 1, 10 do CoopCamera.Update(0.1, true) end
check(CoopCamera.mode == "game" and #fixes == 3, "the game's own pin is left alone")
CoopCamera.Update(0.016, false)
check(#fixes == 3 and cam.fixedParam ~= nil, "and not unpinned when Select goes off")
cam.fixedParam = nil

-- another camera active (a scripted camera): no pin until the game camera is back
active = "animation"
CoopCamera.Update(0.016, true)
check(#fixes == 3 and CoopCamera.mode == "boy", "no pin on another camera")
active = "game"
CoopCamera.Update(0.6, true)
check(#fixes == 4 and CoopCamera.mode == "trico", "pinned once the game camera is back")
-- the game camera goes away while pinned: unpin
active = "animation"
CoopCamera.Update(0.6, true)
check(#fixes == 5 and fixes[5] == "-", "unpinned when another camera takes over")
active = "game"

-- a set the game does not have: reported, nothing pinned
CoopCamera.Update(0.016, false)
CoopCamera.tricoParam = "NoSuchSet"
CoopCamera.Update(0.016, true)
check(CoopCamera.mode == "boy" and CoopCamera.missing, "missing set")
CoopCamera.tricoParam = "TricoAutoRunTest_far"
CoopCamera.Update(0.6, true)
check(CoopCamera.mode == "trico" and fixes[#fixes] == "TricoAutoRunTest_far", "another set")

-- a cycle of sets: the next one each time the Trico framing goes on
CoopCamera.Update(0.016, false)
CoopCamera.cycle = { "TricoEye", "TricoAutoRunTest_middle" }
CoopCamera.Update(0.016, true)
check(fixes[#fixes] == "TricoEye", "first set of the cycle")
CoopCamera.Update(0.016, false)
CoopCamera.Update(0.016, true)
check(fixes[#fixes] == "TricoAutoRunTest_middle", "second set")
CoopCamera.Update(0.016, false)
CoopCamera.Update(0.016, true)
check(fixes[#fixes] == "TricoEye", "round again")
CoopCamera.cycle = nil
CoopCamera.tricoParam = "TricoAutoRunTest_far"
CoopCamera.Update(0.016, false)

-- keeping both in the shot: the camera manager says how far from the screen centre each is
local seen = { Player = true, Trico = true }
-- (ratio_xy: off the screen centre toward the edge; the second value only says the call worked)
CameraManager.GetVisibleInfo2 = function(self, entity, bone, ratio, range, id)
	return { timer = 0, ratio_xy = seen[entity] and 0.3 or math.min(ratio, 1.0), distance_z = 10 }, true
end
CharacterMngr = { GetTrico = function() return { GetName = function() return "Trico" end } end }
-- the boy, and the rendering camera 10 m behind him looking along -Z
local boyPos = Vector(0, 4, 0)
local savedAi2 = Ai2
Ai2 = Ai2 or {}
Ai2.Utils = Ai2.Utils or {}
local savedGetPlayerAI = Ai2.Utils.GetPlayerAI
Ai2.Utils.GetPlayerAI = function() return { pos = boyPos } end
Harness.objects._trico_camera = { worldPosition = Vector(0, 5, 10), worldRotation = { X = 0, Y = 0, Z = 0, W = 1 } }
local function boyOut(out) seen.Player = not out; boyPos = out and Vector(30, 4, 0) or Vector(0, 4, 0) end
-- (the follow's steady rod first: the framing never grows or lifts it)
CoopCamera.tricoParam = "TricoAutoRunTest_middle"
CoopCamera.Update(0.016, true)
boyOut(true)
for _ = 1, 6 do CoopCamera.Update(0.5, true) end
check(objects.TricoAutoRunTest_middle.Rod_LengthBase == 14 and objects.TricoAutoRunTest_middle.Rod_PitchBaseDeg == -25,
	"follow.fixed: the boy out of the shot, the rod as tuned")
boyOut(false)
CoopCamera.Update(0.016, false)
CoopCamera.follow.fixed = false   -- the framing's own tests
CoopCamera.tricoParam = "TricoAutoRunTest_middle"
CoopCamera.Update(0.016, true)
CoopCamera.Update(0.6, true)
local tm2 = objects.TricoAutoRunTest_middle
check(tm2.Rod_LengthBase == 14 and tm2.Rod_PitchBaseDeg == -25, "both seen: the tuned rod")
boyOut(true)
for _ = 1, 4 do CoopCamera.Update(0.5, true) end
check(tm2.Rod_LengthBase == 18.5 and tm2.Rod_PitchBaseDeg == -34, "the boy out of the shot for 1 s: longer and steeper")
boyOut(false)
seen.Trico = false
for _ = 1, 20 do CoopCamera.Update(0.5, true) end
check(tm2.Rod_LengthBase == 26 and tm2.Rod_PitchBaseDeg == -50, "Trico out too: as far as allowed")
seen.Trico = true
CoopCamera.Update(0.5, true)
check(tm2.Rod_LengthBase == 26, "not eased back at once")
for _ = 1, 80 do CoopCamera.Update(0.5, true) end
check(tm2.Rod_LengthBase == 14 and tm2.Rod_PitchBaseDeg == -25, "eased back once both are seen")
-- no hunting: a boy who drops out of the shot whenever the rod is under 17 m (easing back loses
-- him) must not make the camera swing in and out for ever: it settles within a few swings
Harness.tricoProps = Harness.NewTricoProps(0, 0, 0)
local boyFrom = function() return tm2.Rod_LengthBase >= 17 end
local lengths, turns, last, dir = {}, 0, nil, 0
for i = 1, 160 do   -- 80 s of checks
	boyOut(not boyFrom())
	CoopCamera.Update(0.5, true)
	local l = tm2.Rod_LengthBase
	if last and l ~= last then
		local d = l > last and 1 or -1
		if dir ~= 0 and d ~= dir and i > 80 then turns = turns + 1 end
		dir = d
	end
	last = l
	lengths[#lengths + 1] = l
end
check(turns <= 1, "the second 40 s: the rod changes direction at most once (" .. turns .. ")")
check(tm2.Rod_LengthBase >= 17, "and keeps the boy in shot")
-- once Trico has walked on, the camera may ease back again
Harness.tricoProps.currentInfo.aiPos = Vector(0, 0, 10)
boyOut(false)
for _ = 1, 60 do CoopCamera.Update(0.5, true) end
check(tm2.Rod_LengthBase < 17, "Trico moved on: eased back again")
Harness.tricoProps = nil
boyOut(false)
for _ = 1, 200 do CoopCamera.Update(0.5, true) end

-- the game's timer, when it counts, says seen whatever the ratio
CameraManager.GetVisibleInfo2 = function() return { timer = 2.0, ratio_xy = 0.99, distance_z = 10 }, true end
for _ = 1, 4 do CoopCamera.Update(0.5, true) end
check(tm2.Rod_LengthBase == 14, "a running timer counts as seen")
-- not measured yet (all zero): no change
CameraManager.GetVisibleInfo2 = function() return { timer = 0, ratio_xy = 0, distance_z = 0 }, true end
for _ = 1, 4 do CoopCamera.Update(0.5, true) end
check(tm2.Rod_LengthBase == 14, "nothing measured, nothing changed")
CoopCamera.Update(0.016, false)
check(tm2.Rod_LengthBase == 40 and tm2.Rod_PitchBaseDeg == -85 and CoopCamera.frame == nil, "the set's own values back")
CoopCamera.framing.enabled = false
boyOut(true)
CameraManager.GetVisibleInfo2 = function(self, entity, bone, ratio, range, id)
	return { timer = 0, ratio_xy = seen[entity] and 0.3 or 1.0, distance_z = 10 }, true
end
CoopCamera.Update(0.016, true)
for _ = 1, 4 do CoopCamera.Update(0.5, true) end
check(tm2.Rod_LengthBase == 14, "framing off: the tuned rod stays")
CoopCamera.framing.enabled = true
CoopCamera.Update(0.016, false)
-- the boy behind the camera is out of the shot too
CoopCamera.framing.enabled = true
boyPos = Vector(0, 4, 20)
CoopCamera.Update(0.016, true)
for _ = 1, 4 do CoopCamera.Update(0.5, true) end
check(tm2.Rod_LengthBase > 14, "the boy behind the camera")
CoopCamera.Update(0.016, false)
CoopCamera.follow.fixed = true
CameraManager.GetVisibleInfo2 = nil
CharacterMngr = nil
Harness.objects._trico_camera = nil
Ai2.Utils.GetPlayerAI = savedGetPlayerAI
Ai2 = savedAi2
CoopCamera.tricoParam = "TricoAutoRunTest_far"

-- the follow: the rod held at a Global yaw behind Trico's heading, through a filter; the Trico
-- player's right stick (not while aiming with L2) adds an offset that eases back
local savedAi2b = Ai2
Ai2 = Ai2 or {}
Ai2.Utils = Ai2.Utils or {}
local savedProps = Ai2.Utils.GetCondorAIProps
local heading = Vector(0, 0, 1)
Ai2.Utils.GetCondorAIProps = function() return { currentInfo = { ai2Ext = { dirXZ = heading } } } end
Input = { pad1 = { RMag = 0 } }
local function yawOf(h) return (CoopCamera.orbit.yawZero + h) % 360 end
local function near(a, b, tol) return math.abs((a - b + 180) % 360 - 180) <= (tol or 0.01) end
CoopCamera.Update(0.016, true)
local tf = objects.TricoAutoRunTest_far
local stick = { x = 0, y = 0, rx = 0, ry = 0, hold = {}, down = {} }
CoopCamera.Update(0.1, true, stick)
check(tf.Rod_YawBasisType == "Global" and near(tf.Rod_YawBaseDeg, yawOf(180)), "pinned: straight behind him, " .. tostring(tf.Rod_YawBaseDeg))
check(CoopCamera.OrbitYaw(0, 1, 0) == yawOf(180) and CoopCamera.OrbitYaw(1, 0, 0) == yawOf(270), "behind Trico, as mapped")
-- a quarter turn: the camera comes round smoothly, within 15 degrees in 2 s, never past it
heading = Vector(1, 0, 0)
local errs = {}
for i = 1, 20 do
	CoopCamera.Update(0.1, true, stick)
	errs[i] = CoopCamera.followState.e
end
check(math.abs(errs[20]) < 15, "a quarter turn: behind him within 2 s (" .. errs[20] .. ")")
local flips = 0
for i = 2, 20 do if errs[i] * errs[i - 1] < 0 then flips = flips + 1 end end
check(flips == 0, "no overshoot")
check(math.abs(errs[2] - errs[1]) <= 12.01, "at most 120 degrees a second")
for _ = 1, 30 do CoopCamera.Update(0.1, true, stick) end
local settled = tf.Rod_YawBaseDeg
check(math.abs(CoopCamera.followState.e) <= 1.01, "settled within a degree")
-- a wobble inside the dead band doesn't move it
heading = Vector(math.cos(math.rad(4)), 0, math.sin(math.rad(4)))
for _ = 1, 10 do CoopCamera.Update(0.1, true, stick) end
check(tf.Rod_YawBaseDeg == settled, "a 4 degree wobble: still")
-- a heading jump (a slide's false reading) for 0.2 s is ignored; one lasting is taken
heading = Vector(-1, 0, 0)
CoopCamera.Update(0.1, true, stick)
CoopCamera.Update(0.1, true, stick)
heading = Vector(1, 0, 0)
CoopCamera.Update(0.1, true, stick)
check(tf.Rod_YawBaseDeg == settled, "a short heading jump: ignored")
heading = Vector(-1, 0, 0)
for _ = 1, 4 do CoopCamera.Update(0.1, true, stick) end
check(tf.Rod_YawBaseDeg ~= settled, "a lasting one: followed")
heading = Vector(0, 0, 1)
for _ = 1, 60 do CoopCamera.Update(0.1, true, stick) end
check(near(tf.Rod_YawBaseDeg, yawOf(180), 1.01), "behind him again")
-- the orbit: 60 degrees round on top of the follow, tilted; held while idle, eased back after
-- 1.5 s, and never the free camera
stick.rx = 1
CoopCamera.Update(0.5, true, stick)
check(tf.Rod_YawBasisType == "Global" and near(tf.Rod_YawBaseDeg, yawOf(180 + 60), 1.01), "turned 60 degrees")
stick.rx, stick.ry = 0, 1
CoopCamera.Update(0.5, true, stick)
check(tf.Rod_PitchBaseDeg == -25 + 20, "tilted, as far as allowed")
stick.hold.L2 = true
stick.rx = 1
CoopCamera.Update(0.5, true, stick)
check(near(tf.Rod_YawBaseDeg, yawOf(240), 1.01), "the stick aims while L2 is held")
stick.hold.L2 = nil
stick.rx, stick.ry = 0, 0
CoopCamera.Update(0.4, true, stick)
check(near(tf.Rod_YawBaseDeg, yawOf(240), 1.01), "held for a while after letting go (L2 counts as idle)")
-- and while the offset holds, Trico turning carries the camera round with him
heading = Vector(1, 0, 0)
for _ = 1, 3 do CoopCamera.Update(0.1, true, stick) end
check(not near(tf.Rod_YawBaseDeg, yawOf(240), 1.01) and CoopCamera.orbitState.yaw == 60, "the offset rides on the follow")
heading = Vector(0, 0, 1)
for _ = 1, 30 do CoopCamera.Update(0.1, true, stick) end
check(tf.Rod_YawBasisType == "Global" and CoopCamera.orbitState == nil and tf.Rod_PitchBaseDeg == -25
	and near(tf.Rod_YawBaseDeg, yawOf(180), 1.01), "eased back behind Trico, still following")
-- a turn into a wall (the camera pushed within 4 m of Trico) goes back to the last good angle
CoopCamera.seenInfo = { CoopCamTrico = { timer = 1, ratio_xy = 0.8, distance_z = 2.0 } }
stick.rx = 1
CoopCamera.Update(0.5, true, stick)
check(CoopCamera.orbitState and CoopCamera.orbitState.yaw == 0, "no room: back to the last good angle")
CoopCamera.seenInfo = nil
stick.rx = 0
for _ = 1, 10 do CoopCamera.Update(0.5, true, stick) end
-- the boy's player turns the game's camera: free at once; on letting go the follow picks up from
-- where the camera is (here looking along -Z: behind angle 0)
Input.pad1.RMag = 0.9
CoopCamera.Update(0.016, true, stick)
check(tf.Rod_YawBasisType == "Free" and CoopCamera.orbitState == nil, "the boy's stick takes it")
Harness.objects._trico_camera = { worldPosition = Vector(0, 5, 10), worldRotation = { X = 0, Y = 0, Z = 0, W = 1 } }
Input.pad1.RMag = 0
CoopCamera.Update(0.016, true, stick)
check(tf.Rod_YawBasisType == "Global" and near(tf.Rod_YawBaseDeg, yawOf(0) + 0.0, 2.5), "let go: following from where he left it, " .. tf.Rod_YawBaseDeg)
for _ = 1, 50 do CoopCamera.Update(0.1, true, stick) end
check(near(tf.Rod_YawBaseDeg, yawOf(180), 1.01), "and round behind Trico")
Harness.objects._trico_camera = nil
-- the follow off: the old orbit (its own Global yaw, free once eased back)
CoopCamera.follow.enabled = false
stick.rx = 1
CoopCamera.Update(0.5, true, stick)
check(tf.Rod_YawBaseDeg == CoopCamera.OrbitYaw(0, 1, 60), "follow off: the orbit's own yaw")
stick.rx = 0
for _ = 1, 8 do CoopCamera.Update(0.5, true, stick) end
check(tf.Rod_YawBasisType == "Free" and CoopCamera.orbitState == nil, "follow off: free once eased back")
CoopCamera.follow.enabled = true
-- the trace's follow line
CoopDebug.enabled, CoopCamera.trace, CoopCamera.traceIn = true, 0.25, 0
for _ = 1, 30 do CoopCamera.Update(0.1, true, stick) end
CoopDebug.Flush()
check(#Harness.Find("^CF hd=%-?180.0 h=%-?1[78]%d%.%d e=%-?%d%.%d live=%- off=0 len=14 pitch=%-25 basis=Global$") >= 1, "follow trace line")
CoopDebug.enabled, CoopCamera.trace = false, nil
CoopDebug.queue, Harness.lightbar = {}, {}
CoopCamera.Update(0.016, false)
check(tf.Rod_YawBasisType == "Global" and tf.Rod_YawBaseDeg == 180 and CoopCamera.orbitState == nil
	and CoopCamera.followState == nil, "the set's own yaw back on release")
Input = nil
Ai2.Utils.GetCondorAIProps = savedProps
Ai2 = savedAi2b

-- Trico's position spiking to near the origin (his own turns and slides): the camera holds still
-- (follow springs at no strength) for 5 s, then aims at the boy, and a jump lasting 10 s is
-- accepted; never aimed at the spike, and the springs put back
local spikeAt = Vector(-70, 7, -80)
local savedAi2c = Ai2
Ai2 = Ai2 or {}
Ai2.Utils = Ai2.Utils or {}
local savedPropsC = Ai2.Utils.GetCondorAIProps
Ai2.Utils.GetCondorAIProps = function() return { currentInfo = { work = { GetMySelfPos = spikeAt }, aiPos = spikeAt } } end
CoopCamera.Update(0.016, true)
local ts = objects[CoopCamera.tricoParam]
ts.Base_SpringBasePos_Str, ts.Gaze_SpringLookYaw_Str = 10, 2
for _ = 1, 5 do CoopCamera.Update(0.1, true) end
check(ts.Target_TargetType ~= "Default" and not CoopCamera.spiking, "steady: aimed at Trico")
spikeAt = Vector(-0.4, 7, -0.6)
local targets = {}
for _ = 1, 20 do CoopCamera.Update(0.1, true) targets[#targets + 1] = ts.Target_TargetType end
check(CoopCamera.spiking == "hold" and ts.Base_SpringBasePos_Str == 0 and ts.Gaze_SpringLookYaw_Str == 0,
	"a spike: the camera holds still")
for _, t in ipairs(targets) do check(t == "Trico", "held, not swung to the boy, for the first seconds") end
spikeAt = Vector(-70.5, 7, -80.2)
CoopCamera.Update(0.1, true)
check(ts.Target_TargetType == "Trico" and not CoopCamera.spiking and ts.Base_SpringBasePos_Str == 10
	and ts.Gaze_SpringLookYaw_Str == 2, "real again: springs back, on Trico")
-- one lasting over 5 s: the boy; over 10 s (a scene placing him elsewhere): accepted
spikeAt = Vector(40, 7, 40)
for _ = 1, 60 do CoopCamera.Update(0.1, true) end
check(CoopCamera.spiking == "boy" and ts.Target_TargetType == "Default" and ts.Base_SpringBasePos_Str == 10,
	"lasting: aimed at the boy, springs back")
for _ = 1, 45 do CoopCamera.Update(0.1, true) end
check(ts.Target_TargetType == "Trico" and not CoopCamera.spiking, "a lasting move: back on Trico at the new place")
-- released mid-hold: the springs are put back
spikeAt = Vector(-0.4, 7, -0.6)
for _ = 1, 3 do CoopCamera.Update(0.1, true) end
check(CoopCamera.spiking == "hold" and ts.Base_SpringBasePos_Str == 0, "held again")
CoopCamera.Update(0.016, false)
check(ts.Base_SpringBasePos_Str == 10 and not CoopCamera.held, "released: springs put back")
-- the debug fake spike holds the camera at the time asked
spikeAt = Vector(-70, 7, -80)
CoopCamera.testSpike, CoopCamera.testClock = { at = 1, length = 2 }, 0
CoopCamera.Update(0.016, true)
for _ = 1, 15 do CoopCamera.Update(0.1, true) end
check(CoopCamera.spiking == "hold", "test spike: held")
for _ = 1, 15 do CoopCamera.Update(0.1, true) end
check(not CoopCamera.spiking, "test spike over")
CoopCamera.testSpike = nil
CoopCamera.Update(0.016, false)
Ai2.Utils.GetCondorAIProps = savedPropsC
Ai2 = savedAi2c

-- the debug dump: the set's table, once the game camera is up
GameCamera.game_param_tbl = { Stand = { Base = { Distance = 4.5, Offset = { X = 0, Y = 1.5 } }, _cond = {} } }
GameCam_Command = function(cmd, name) return { GetName = function() return name end } end
CoopDebug.enabled = true
CoopCamera.dumpSets = { "Stand", "NoSet" }
CoopCamera.Update(0.016, false)
check(#Harness.Find("^CA dump") == 0, "not before the next check")
CoopCamera.Update(0.6, false)
CoopDebug.Flush()
check(#Harness.Find("^CA dump Stand: Base.Distance=4.5 Base.Offset={X:0 Y:1.5} %(2%)") == 1, "dump line")
check(#Harness.Find("^CA dump NoSet: no table") == 1, "missing set")
CoopCamera.Update(0.6, false)
CoopDebug.Flush()
check(#Harness.Find("^CA dump Stand") == 1, "dumped once")
CoopDebug.enabled = false
Harness.lightbar = {}

-- report line and release
CoopDebug.enabled = true
CoopCamera.reportIn = 0
CoopCamera.Update(0.016, true)
CoopDebug.Flush()
check(#Harness.Find("^CA mode=trico want=trico game=true param=Stand fixed=gamecamera_param_TricoAutoRunTest_far$") == 1,
	"report line")
CoopCamera.Release()
check(cam.fixedParam == nil and CoopCamera.mode == "boy", "released")

-- no game camera at all (title screen): nothing happens
GameCamera = nil
CoopCamera.Update(0.6, true)
print("ok")
