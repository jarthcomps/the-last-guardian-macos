-- tlg-coop: one shared camera with two framings, switched with Select (the touchpad).
--
-- Boy framing is the game's own camera, untouched. Trico framing pins one of the game camera's
-- own parameter sets the way the developers' auto-Trico test does (condordebugautotrico.lua):
-- GameCamera:FixParam("TricoAutoRunTest_middle"), released with GameCamera:FixParam(). The
-- emulator keeps the Select toggle (CoopInput.tricoCamera); with it off, even solo as the boy,
-- nothing here touches the camera.
--
-- The pin is only applied while the game camera is the active one, never over a parameter set the
-- game pinned itself (scripted scenes), and given back as soon as the toggle goes off or the game
-- takes the camera.

CoopCamera = CoopCamera or {}

CoopCamera.tricoParam = CoopCamera.tricoParam or "TricoAutoRunTest_middle"
local CHECK = 0.5          -- seconds between checks of the camera
local REPORT = 1.0

function CoopCamera.Reset()
	CoopCamera.mode = "boy"     -- what is on screen: "boy", "trico", or "game" (the game's own pin)
	CoopCamera.pinned = nil     -- the name of the parameter object we pinned
	CoopCamera.checkIn = 0
	CoopCamera.reportIn = 0
	CoopCamera.lastError = nil
	CoopCamera.missing = false
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

local function Unpin(cam)
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
		CoopCamera.pinned = nil
		CoopCamera.mode = "game"
		return
	end
	if want == "trico" and GameCameraActive() then
		if not ours then
			GameCamera:FixParam(CoopCamera.tricoParam)
			CoopCamera.pinned = NameOf(cam.fixedParam)
			CoopCamera.missing = CoopCamera.pinned == nil
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

-- Call once per frame with whether the Trico framing is asked for (CoopInput.tricoCamera).
function CoopCamera.Update(dt, tricoFraming)
	dt = dt or 0
	local want = tricoFraming and "trico" or "boy"
	local changed = want ~= CoopCamera.want
	CoopCamera.want = want
	CoopCamera.checkIn = CoopCamera.checkIn - dt
	if changed or CoopCamera.checkIn <= 0 then
		CoopCamera.checkIn = CHECK
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
