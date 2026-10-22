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
		Base_FovDeg = 70 }
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
check(tm.Rod_LengthBase == 14 and tm.Rod_PitchBaseDeg == -25 and tm.Rod_YawBasisType == "Free"
	and tm.Base_FovDeg == 75, "retuned: 14 m, 25 degrees down, following, wider")
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
