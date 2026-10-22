-- tlg-coop: one shared camera with two framings, switched with Select (the touchpad).
--
-- Boy framing is the game's own camera, untouched. Trico framing pins one of the game camera's
-- own parameter sets the way the developers' auto-Trico test does (condordebugautotrico.lua):
-- GameCamera:FixParam("TricoAutoRunTest_middle"), released with GameCamera:FixParam(). That set
-- targets Trico's neck but looks straight down from 40 m with a fixed world yaw (a test view),
-- so CoopCamera.tune retunes the pinned set while it is ours: a 14 m rod, 25 degrees down,
-- following yaw and a wider lens, which keeps Trico centred and the boy near him in view; the
-- set's own values are put back on release (the game never uses this set in play). The
-- emulator keeps the Select toggle (CoopInput.tricoCamera); with it off, even solo as the boy,
-- nothing here touches the camera.
--
-- The pin is only applied while the game camera is the active one, never over a parameter set the
-- game pinned itself (scripted scenes), and given back as soon as the toggle goes off or the game
-- takes the camera.
--
-- While the Trico framing is up, CoopCamera.framing keeps both characters on screen. The test set
-- has the game's terrain avoidance and object fading switched off, so the tune switches them on
-- (the flags the game's own Run set uses). Then, at the CHECK pace, it asks the camera manager
-- whether Trico's neck and the boy's head are on screen (CameraManager:GetVisibleInfo2, as the
-- mission scripts' camera monitors do):
--   either out of the shot  -> lengthen the rod and look down more steeply;
--   both seen for a while   -> ease back to the tuned rod.
-- Walls are the game's job: with its collision on, the rod shortens where a wall is behind the
-- camera, so in tight spots the steeper look is what keeps both in view.
--
-- CoopCamera.orbit: in the Trico framing the Trico player's right stick (while L2 is not held;
-- with L2 it aims the lightning) turns the camera around Trico and tilts it. The rod is then
-- held at a world yaw ("Global" basis) that is Trico's back plus the player's offset, and the
-- offset eases back to straight behind Trico after `idle` seconds without input; once it is
-- back the game's free camera takes over again. The boy's player keeps the game's own
-- right-stick camera: whoever moved a right stick last has the camera.

CoopCamera = CoopCamera or {}

CoopCamera.tricoParam = CoopCamera.tricoParam or "TricoAutoRunTest_middle"
CoopCamera.tune = CoopCamera.tune or {
	Rod_LengthBase = 14.0,
	Rod_PitchBaseDeg = -25.0,
	Rod_YawBasisType = "Free",
	Rod_YawBaseDeg = 180.0,   -- the set's own; listed so the orbit's changes are put back too
	Base_FovDeg = 75.0,
	AutoAvoid_Flags = "Valid|ChkTerrain|ChkCondor",   -- turn the rod away from walls and Trico (as Run does)
	Rod_Flags = "ApproachHead|ShortenByPitch",        -- the set's own, less IgnoreCondorHit: the
	                                                  -- rod no longer passes into Trico's feathers
	ObjSensor_Flags = "Valid",               -- fade objects between the camera and the boy
	PreAvoid_Flags = "Valid",                -- and look ahead for walls (as Run does)
	-- the test set only moves the camera once Trico is 20-30 m away (a lazy overhead view);
	-- these are the Default set's follow-spring ranges, so the camera stays behind Trico
	Rod_SpringPosXZ_Range = "0 3",
	Base_SpringBasePos_Range = "0.1 1",
	Base_SpringBasePosXZ_Range = "0.1 1",
	Base_SpringBasePosY_Range = "0.3 1",
}
CoopCamera.framing = CoopCamera.framing or {
	enabled = true,
	tricoBone = "jt_Neck",
	edge = 0.85,          -- off the screen centre (0) toward its edge (1) beyond which one is out
	boyUp = 1.0,          -- metres above the boy's position that must be in the shot (his chest)
	aspect = 16 / 9,
	range = 60,           -- metres from the camera
	grow = 1.5,           -- rod metres added per check while someone is out of the shot
	maxGrow = 12,
	lift = 3,             -- degrees more downward per check while someone is out of the shot
	maxLift = 25,
	relax = 0.5,          -- metres (and degrees x2) given back per check once both are seen
	settle = 2.0,         -- seconds both must be seen before easing back
}
CoopCamera.orbit = CoopCamera.orbit or {
	enabled = true,
	yawSpeed = 120,       -- degrees per second at full stick
	pitchSpeed = 60,
	pitchMin = -20,       -- degrees added to the rod's pitch (+ lower, looking up; - higher)
	pitchMax = 20,
	deadzone = 0.25,
	idle = 3.0,           -- seconds without input before easing back behind Trico
	back = 90,            -- degrees per second while easing back
	near = 4.0,           -- metres: a turn that brings the camera this close to Trico (a wall
	                      -- pushing it into him) goes back to the last angle that had room
	-- the "Global" yaw for a rod pointing from Trico toward the world direction (x, z):
	-- yawZero + yawSign * atan2(x, z) in degrees (from the yaw probe run)
	yawZero = 0,
	yawSign = 1,
}
-- CoopCamera.cycle (a list of set names), when set, uses the next one each time the Trico
-- framing is switched on (for trying sets in game).
local CHECK = 0.5          -- seconds between checks of the camera
local REPORT = 1.0

function CoopCamera.Reset()
	CoopCamera.mode = "boy"     -- what is on screen: "boy", "trico", or "game" (the game's own pin)
	CoopCamera.pinned = nil     -- the name of the parameter object we pinned
	CoopCamera.checkIn = 0
	CoopCamera.reportIn = 0
	CoopCamera.lastError = nil
	CoopCamera.missing = false
	CoopCamera.cycleIndex = 0
	CoopCamera.orbitState = nil   -- { yaw, pitch, idle } while the Trico player turns the camera
end

local function Camera()
	if not GameCamera or not GameCamera.GetCamera then return nil end
	local ok, cam = pcall(GameCamera.GetCamera, GameCamera)
	if ok then return cam end
	return nil
end

local function GameCameraActive()
	if not CameraManager or not CameraManager.IsActiveCamera then return false end
	local ok, active = pcall(CameraManager.IsActiveCamera, CameraManager, "game")
	return ok and active and active ~= 0
end

-- The game hands the parameter object back through a native call each time, so compare names.
local function NameOf(param)
	if not param then return nil end
	local ok, name = pcall(function() return param:GetName() end)
	return ok and name or "?"
end

-- Retune the pinned set (its own values kept to put back).
local function Tune(param)
	if type(CoopCamera.tune) ~= "table" or not param then return end
	CoopCamera.untune = {}
	for key, value in pairs(CoopCamera.tune) do
		local ok, old = pcall(function() return param[key] end)
		if ok and old ~= nil then
			-- a vector property comes back as an object that may change with the property, so
			-- keep its text form ("20 30"), which the engine takes back like any other value
			if type(old) == "userdata" then old = tostring(old) end
			CoopCamera.untune[key] = old
			pcall(function() param[key] = value end)
		end
	end
	CoopCamera.tunedParam = param
	CoopCamera.tuneLogged = false
end

local function Untune()
	local param = CoopCamera.tunedParam
	if param and CoopCamera.untune then
		for key, value in pairs(CoopCamera.untune) do pcall(function() param[key] = value end) end
	end
	CoopCamera.tunedParam = nil
	CoopCamera.untune = nil
	CoopCamera.frame = nil
	CoopCamera.orbitState = nil
	CoopCamera.tricoSeen, CoopCamera.boySeen = nil, nil   -- measured again for the next pin
end

-- Whether a bone of an entity is in the shot: true, false, or nil when the camera manager
-- can't tell. Its info has ratio_xy, how far the bone is from the screen centre (0) toward the
-- edge (1, capped at the ratio asked for, so 1 is asked), and distance_z; both are 0 before it
-- has measured. (Its timer, the time in the shot, didn't count up when asked from here, and the
-- second return value only says the call worked.) The id keeps this check apart from the game's.
local function Seen(entity, bone, id)
	local f = CoopCamera.framing
	if not entity or not CameraManager or not CameraManager.GetVisibleInfo2 then return nil end
	local ok, info = pcall(CameraManager.GetVisibleInfo2, CameraManager, entity, bone, 1.0, f.range, id)
	if not ok or type(info) ~= "table" then return nil end
	CoopCamera.seenInfo = CoopCamera.seenInfo or {}
	CoopCamera.seenInfo[id] = info
	local r, z = tonumber(info.ratio_xy), tonumber(info.distance_z)
	if not r or not z or (r == 0 and z == 0) then return nil end
	if type(info.timer) == "number" and info.timer > 0 then return true end
	return r < f.edge and z > 0
end

-- Trico's entity name as the camera manager knows it: the one his AI reports (as the game's
-- in-frame checks use, condorcommonaxis.lua), else his character's, else "Condor".
local function TricoName()
	local ok, name = pcall(function() return Ai2.Utils.GetCondorAIProps().currentInfo.parentEntityName end)
	if ok and type(name) == "string" and name ~= "" then return name end
	ok, name = pcall(function() return CharacterMngr:GetTrico():GetName() end)
	if ok and type(name) == "string" and name ~= "" then return name end
	return "Condor"
end

-- Where the boy is in the shot, from the rendering camera (the object steering reads too): his
-- chest in screen units, x and y from -1 to 1 across the view, with the FOV taken as vertical.
-- The camera manager's own check gives no screen position for "Player", so this is done here.
-- Returns true / false, or nil when there is no camera or boy to go by.
local CAMERA_NAME = "_trico_camera"
local function BoyInShot()
	local f = CoopCamera.framing
	local cam = FindObject and FindObject(CAMERA_NAME)
	if not cam or not cam.worldPosition or not cam.worldRotation then return nil end
	local ok, boy = pcall(function() return Ai2.Utils.GetPlayerAI().pos end)
	if not ok or not boy then return nil end
	local c, q = Vector(cam.worldPosition), cam.worldRotation
	local fwd, right, up = Vector(0, 0, -1):Rotate(q), Vector(1, 0, 0):Rotate(q), Vector(0, 1, 0):Rotate(q)
	local vx, vy, vz = boy.X - c.X, boy.Y + f.boyUp - c.Y, boy.Z - c.Z
	local depth = vx * fwd.X + vy * fwd.Y + vz * fwd.Z
	if depth < 0.5 then
		CoopCamera.boyScreen = { 9, 9, depth }
		return false
	end
	local fov = tonumber(CoopCamera.tunedParam and CoopCamera.tunedParam.Base_FovDeg) or 70
	local tanV = math.tan(math.rad(fov / 2))
	local sx = (vx * right.X + vy * right.Y + vz * right.Z) / depth / (tanV * f.aspect)
	local sy = (vx * up.X + vy * up.Y + vz * up.Z) / depth / tanV
	CoopCamera.boyScreen = { sx, sy, depth }
	return math.max(math.abs(sx), math.abs(sy)) < f.edge
end

-- Every frame while the Trico framing is ours: ask who is on screen.
local function Watch()
	local f = CoopCamera.framing
	if type(f) ~= "table" or not f.enabled or not CoopCamera.tunedParam then
		CoopCamera.tricoSeen, CoopCamera.boySeen = nil, nil
		return
	end
	CoopCamera.tricoSeen = Seen(TricoName(), f.tricoBone, "CoopCamTrico")
	local okBoy, boy = pcall(BoyInShot)
	if okBoy then CoopCamera.boySeen = boy else CoopCamera.boySeen = nil end   -- (keeps false)
end

-- Keep both characters in the Trico framing by changing the tuned rod (see the top).
local function Frame(dt)
	local f = CoopCamera.framing
	local param = CoopCamera.tunedParam
	local tune = CoopCamera.tune
	if type(f) ~= "table" or not f.enabled or not param or type(tune) ~= "table" then return end
	local trico, boy = CoopCamera.tricoSeen, CoopCamera.boySeen
	if trico == nil and boy == nil then return end
	local fr = CoopCamera.frame or { grow = 0, lift = 0, both = 0 }
	CoopCamera.frame = fr
	if trico == false or boy == false then
		-- someone is out of the shot: back out and look down more (the game's own collision
		-- shortens the rod where a wall is behind, so in tight spots this mostly raises it)
		fr.grow = math.min(fr.grow + f.grow, f.maxGrow)
		fr.lift = math.min(fr.lift + f.lift, f.maxLift)
		fr.both = 0
	else
		fr.both = fr.both + dt
		if fr.both >= f.settle then
			fr.grow = math.max(0, fr.grow - f.relax)
			fr.lift = math.max(0, fr.lift - 2 * f.relax)
		end
	end
	fr.boy, fr.trico = boy, trico
	fr.length = (tune.Rod_LengthBase or 14) + fr.grow
	fr.pitch = (tune.Rod_PitchBaseDeg or -25) - fr.lift
	param.Rod_LengthBase = fr.length
	param.Rod_PitchBaseDeg = fr.pitch + (CoopCamera.orbitState and CoopCamera.orbitState.pitch or 0)
end

local function Unpin(cam)
	Untune()
	if CoopCamera.pinned and cam and NameOf(cam.fixedParam) == CoopCamera.pinned then
		GameCamera:FixParam()
	end
	CoopCamera.pinned = nil
end

local function Apply(want)
	local cam = Camera()
	if not cam then CoopCamera.mode = "none" return end
	local fixed = NameOf(cam.fixedParam)
	local ours = CoopCamera.pinned ~= nil and fixed == CoopCamera.pinned
	if fixed and not ours then
		-- the game pinned a parameter set of its own: leave the camera alone
		Untune()
		CoopCamera.pinned = nil
		CoopCamera.mode = "game"
		return
	end
	if want == "trico" and GameCameraActive() then
		if not ours then
			GameCamera:FixParam(CoopCamera.tricoParam)
			CoopCamera.pinned = NameOf(cam.fixedParam)
			CoopCamera.missing = CoopCamera.pinned == nil
			if CoopCamera.pinned then Tune(cam.fixedParam) end
		end
		CoopCamera.mode = CoopCamera.pinned and "trico" or "boy"
		if CoopCamera.mode == "trico" then Frame(CHECK) end
	else
		Unpin(cam)
		CoopCamera.mode = "boy"
	end
end

local function Report()
	if not CoopDebug or not CoopDebug.enabled then return end
	local cam = Camera()
	CoopDebug.Print("CA mode=%s want=%s game=%s param=%s fixed=%s%s", CoopCamera.mode,
		CoopCamera.want or "-", tostring(GameCameraActive()), tostring(cam and cam.paramName),
		tostring(cam and NameOf(cam.fixedParam)),
		CoopCamera.missing and " missing=" .. CoopCamera.tricoParam or "")
	local fr = CoopCamera.frame
	if fr and CoopCamera.mode == "trico" then
		local function BoyAt()
			local b = CoopCamera.boyScreen
			return b and string.format("%.2f,%.2f@%.1f", b[1], b[2], b[3]) or "-"
		end
		local function Raw(id)
			local i = CoopCamera.seenInfo and CoopCamera.seenInfo[id]
			if not i then return "-" end
			return string.format("%.1f/%.2f/%.1f", i.timer or -1, tonumber(i.ratio_xy) or -1, tonumber(i.distance_z) or -1)
		end
		CoopDebug.Print("CA frame len=%.1f pitch=%.0f boy=%s(%s) trico=%s(%s) grow=%.1f lift=%.0f", fr.length or 0,
			fr.pitch or 0, tostring(fr.boy), BoyAt(), tostring(fr.trico), Raw("CoopCamTrico"), fr.grow, fr.lift)
	end
	if CoopCamera.tunedParam and not CoopCamera.tuneLogged then
		-- once per pin: what the game kept of the tune (flags it doesn't know are dropped)
		CoopCamera.tuneLogged = true
		local parts = {}
		for key, _ in pairs(CoopCamera.tune or {}) do
			local ok, v = pcall(function() return CoopCamera.tunedParam[key] end)
			local was = CoopCamera.untune and CoopCamera.untune[key]
			parts[#parts + 1] = key .. "=" .. tostring(ok and v) .. "(" .. tostring(was) .. ")"
		end
		table.sort(parts)
		CoopDebug.Print("CA tune %s", table.concat(parts, " "))
	end
end

-- Debug: print a parameter set's table (GameCamera.game_param_tbl[name], what OverwriteParam
-- copies onto a parameter object as Category_Member) and the live object's properties.
function CoopCamera.DumpSet(name)
	if not CoopDebug then return end
	local tbl = GameCamera and GameCamera.game_param_tbl and GameCamera.game_param_tbl[name]
	if type(tbl) ~= "table" then
		CoopDebug.Print("CA dump %s: no table", name)
	else
		local keys = {}
		for cat, members in pairs(tbl) do
			if type(members) == "table" and string.sub(tostring(cat), 1, 1) ~= "_" then
				for k, v in pairs(members) do
					if type(v) == "table" then
						local parts = {}
						for kk, vv in pairs(v) do parts[#parts + 1] = tostring(kk) .. ":" .. tostring(vv) end
						table.sort(parts)
						v = "{" .. table.concat(parts, " ") .. "}"
					end
					keys[#keys + 1] = tostring(cat) .. "." .. tostring(k) .. "=" .. tostring(v)
				end
			end
		end
		table.sort(keys)
		local line = "CA dump " .. name .. ":"
		for _, kv in ipairs(keys) do
			if string.len(line) + string.len(kv) > 150 then
				CoopDebug.Print("%s", line)
				line = "  "
			end
			line = line .. " " .. kv
		end
		CoopDebug.Print("%s (%d)", line, #keys)
	end
	local ok, obj = pcall(GameCam_Command, "GetParam", name)
	if ok and obj then CoopDebug.DumpObject("CA obj " .. name, obj, 400) end
end

-- The Global yaw that puts the rod `offset` degrees around from straight behind Trico.
function CoopCamera.OrbitYaw(dirX, dirZ, offset)
	local o = CoopCamera.orbit
	local back = math.deg(math.atan2(-dirX, -dirZ)) + offset   -- Trico to the camera
	local yaw = (o.yawZero + o.yawSign * back) % 360
	return yaw
end

local function TricoDir()
	local ok, dir = pcall(function() return Ai2.Utils.GetCondorAIProps().currentInfo.ai2Ext.dirXZ end)
	if ok and dir and (dir.X ~= 0 or dir.Z ~= 0) then return dir.X, dir.Z end
	return nil
end

local function BoyTurning()
	local pad = Input and Input.pad1
	local mag = pad and tonumber(pad.RMag)
	return mag ~= nil and mag > CoopCamera.orbit.deadzone
end

-- Every frame in the Trico framing: the Trico player's right stick turns the camera.
local function Orbit(dt, input)
	local o = CoopCamera.orbit
	local param = CoopCamera.tunedParam
	if type(o) ~= "table" or not o.enabled or not param then return end
	local st = CoopCamera.orbitState
	local rx, ry = 0, 0
	if input and not (input.hold and input.hold.L2) then rx, ry = input.rx or 0, input.ry or 0 end
	local turning = math.abs(rx) > o.deadzone or math.abs(ry) > o.deadzone
	if BoyTurning() and not turning then
		-- the boy's player took the camera: the game's own camera, as it is
		if st then
			CoopCamera.orbitState = nil
			param.Rod_YawBasisType = CoopCamera.tune.Rod_YawBasisType or "Free"
		end
		return
	end
	if turning then
		st = st or { yaw = 0, pitch = 0, idle = 0, good = 0 }
		CoopCamera.orbitState = st
		local t = CoopCamera.seenInfo and CoopCamera.seenInfo.CoopCamTrico
		local z = t and tonumber(t.distance_z)
		if z and z > 0 and z < o.near then
			st.yaw = st.good          -- no room this way: back to where there was
		else
			st.good = st.yaw
			st.yaw = (st.yaw + rx * o.yawSpeed * dt + 180) % 360 - 180
		end
		st.pitch = math.max(o.pitchMin, math.min(o.pitchMax, st.pitch + ry * o.pitchSpeed * dt))
		st.idle = 0
	elseif st then
		st.idle = st.idle + dt
		if st.idle >= o.idle then
			local step = o.back * dt
			local function Toward(v) if math.abs(v) <= step then return 0 end return v - step * (v > 0 and 1 or -1) end
			st.yaw, st.pitch = Toward(st.yaw), Toward(st.pitch)
			if st.yaw == 0 and st.pitch == 0 then
				CoopCamera.orbitState = nil
				param.Rod_YawBasisType = CoopCamera.tune.Rod_YawBasisType or "Free"
				param.Rod_PitchBaseDeg = CoopCamera.frame and CoopCamera.frame.pitch or CoopCamera.tune.Rod_PitchBaseDeg
				return
			end
		end
	end
	if not st then return end
	local dx, dz = TricoDir()
	if not dx then return end
	param.Rod_YawBasisType = "Global"
	param.Rod_YawBaseDeg = CoopCamera.OrbitYaw(dx, dz, st.yaw)
	local base = CoopCamera.frame and CoopCamera.frame.pitch or (CoopCamera.tune.Rod_PitchBaseDeg or -25)
	param.Rod_PitchBaseDeg = base + st.pitch
end

-- Debug (CoopCamera.yawProbe = { start = s, step = s, angles = { ... } }): once the Trico
-- framing has been up `start` seconds, pin the rod to each world yaw in turn ("Global" basis) and
-- report where the camera then looks, against Trico's facing, to map the basis to directions.
local function YawProbe(dt)
	local yp = CoopCamera.yawProbe
	local param = CoopCamera.tunedParam
	if type(yp) ~= "table" or yp.done or not param then return end
	yp.t = (yp.t or 0) + dt
	local i = math.floor((yp.t - yp.start) / yp.step) + 1
	if i < 1 then return end
	if i ~= yp.i then
		if yp.i then
			local cam = FindObject and FindObject(CAMERA_NAME)
			local ok, fwd = pcall(function() return Vector(0, 0, -1):Rotate(cam.worldRotation) end)
			local okD, dir = pcall(function() return Ai2.Utils.GetCondorAIProps().currentInfo.ai2Ext.dirXZ end)
			local okP, rel = pcall(function()
				local t = Ai2.Utils.GetCondorAIProps().currentInfo.aiPos
				return Vector(cam.worldPosition.X - t.X, 0, cam.worldPosition.Z - t.Z)
			end)
			CoopDebug.Print("CA yaw a=%s fwd=%s dir=%s cam-trico=%s", tostring(yp.angles[yp.i]),
				ok and string.format("%.2f,%.2f", fwd.X, fwd.Z) or "?",
				okD and string.format("%.2f,%.2f", dir.X, dir.Z) or "?",
				okP and string.format("%.1f,%.1f", rel.X, rel.Z) or "?")
		end
		yp.i = i
		if i > #yp.angles then
			yp.done = true
			pcall(function() param.Rod_YawBasisType = CoopCamera.tune.Rod_YawBasisType or "Free" end)
			return
		end
		pcall(function()
			param.Rod_YawBasisType = "Global"
			param.Rod_YawBaseDeg = yp.angles[i]
		end)
	end
end

-- Call once per frame with whether the Trico framing is asked for (CoopInput.tricoCamera) and
-- the Trico player's input (CoopInput.TricoPad(), nil when nobody plays Trico).
function CoopCamera.Update(dt, tricoFraming, tricoInput)
	dt = dt or 0
	local want = tricoFraming and "trico" or "boy"
	local changed = want ~= CoopCamera.want
	if changed and want == "trico" and type(CoopCamera.cycle) == "table" and #CoopCamera.cycle > 0 then
		CoopCamera.cycleIndex = CoopCamera.cycleIndex % #CoopCamera.cycle + 1
		CoopCamera.tricoParam = CoopCamera.cycle[CoopCamera.cycleIndex]
		CoopCamera.missing = false
	end
	CoopCamera.want = want
	if CoopCamera.mode == "trico" then
		pcall(Watch)
		local okO, errO = pcall(Orbit, dt, tricoInput)
		if not okO and errO ~= CoopCamera.lastError then
			CoopCamera.lastError = errO
			if CoopDebug then CoopDebug.Print("CA error %s", tostring(errO)) end
		end
		if CoopDebug and CoopDebug.enabled then pcall(YawProbe, dt) end
	end
	CoopCamera.checkIn = CoopCamera.checkIn - dt
	if changed or CoopCamera.checkIn <= 0 then
		CoopCamera.checkIn = CHECK
		-- the debug dump, once the game camera is up (checked at the same pace as the rest)
		if type(CoopCamera.dumpSets) == "table" and not CoopCamera.dumped and GameCameraActive() then
			CoopCamera.dumped = true
			for _, name in ipairs(CoopCamera.dumpSets) do pcall(CoopCamera.DumpSet, name) end
		end
		-- with the toggle off and nothing of ours pinned there is nothing to do
		if want == "trico" or CoopCamera.pinned then
			local ok, err = pcall(Apply, want)
			if not ok and err ~= CoopCamera.lastError then
				CoopCamera.lastError = err
				if CoopDebug then CoopDebug.Print("CA error %s", tostring(err)) end
			end
		else
			CoopCamera.mode = "boy"
		end
	end
	CoopCamera.reportIn = CoopCamera.reportIn - dt
	if CoopCamera.reportIn <= 0 then
		CoopCamera.reportIn = REPORT
		pcall(Report)
	end
end

-- Give the camera back (another game state, co-op off).
function CoopCamera.Release()
	pcall(Unpin, Camera())
	CoopCamera.mode = "boy"
end

CoopCamera.Reset()
