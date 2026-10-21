dofile(string.match(debug.getinfo(1, "S").source, "^@(.*/)") .. "harness.lua")

-- the game camera as far as coop_camera.lua touches it: FixParam(name) looks the set up and pins
-- it (a fresh wrapper each time, as a native call would return), FixParam() unpins
local cam = { paramName = "Stand", fixedParam = nil }
local sets = { TricoAutoRunTest_middle = true, TricoAutoRunTest_far = true, TricoEye = true }
local fixes = {}
local function param(name) return { GetName = function() return "gamecamera_param_" .. name end } end
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
for _ = 1, 30 do CoopCamera.Update(0.1, true) end
check(#fixes == 1, "not pinned again while it holds")

-- Select off: given back at once
CoopCamera.Update(0.016, false)
check(#fixes == 2 and fixes[2] == "-" and cam.fixedParam == nil and CoopCamera.mode == "boy", "boy framing")
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
