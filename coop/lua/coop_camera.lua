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

CoopCamera = CoopCamera or {}

CoopCamera.tricoParam = CoopCamera.tricoParam or "TricoAutoRunTest_middle"
CoopCamera.tune = CoopCamera.tune or {
	Rod_LengthBase = 14.0,
	Rod_PitchBaseDeg = -25.0,
	Rod_YawBasisType = "Free",
	Base_FovDeg = 75.0,
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
			CoopCamera.untune[key] = old
			param[key] = value
		end
	end
	CoopCamera.tunedParam = param
end

local function Untune()
	local param = CoopCamera.tunedParam
	if param and CoopCamera.untune then
		for key, value in pairs(CoopCamera.untune) do pcall(function() param[key] = value end) end
	end
	CoopCamera.tunedParam = nil
	CoopCamera.untune = nil
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

-- Call once per frame with whether the Trico framing is asked for (CoopInput.tricoCamera).
function CoopCamera.Update(dt, tricoFraming)
	dt = dt or 0
	local want = tricoFraming and "trico" or "boy"
	local changed = want ~= CoopCamera.want
	if changed and want == "trico" and type(CoopCamera.cycle) == "table" and #CoopCamera.cycle > 0 then
		CoopCamera.cycleIndex = CoopCamera.cycleIndex % #CoopCamera.cycle + 1
		CoopCamera.tricoParam = CoopCamera.cycle[CoopCamera.cycleIndex]
		CoopCamera.missing = false
	end
	CoopCamera.want = want
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
