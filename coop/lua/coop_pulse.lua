-- tlg-coop: the Trico player aims and fires Trico's tail lightning.
--
-- Only while the boy has the shield (GameFlag.HaveTrigger, as BoyHaveTrigger reads it). The right
-- stick moves a cursor on the ground ahead of Trico (camera-relative) and R2 fires at it (the
-- "aimhold" method, the default: hold L2 to aim, see below). Trico's
-- stock lightning keeps following the boy's own shield aim: nothing here is touched unless the
-- Trico player fires, the boy's aim is never overridden, and everything is put back after a shot.
--
-- Two ways of firing are kept while they are compared on the real game; CoopPulse.method picks:
--   "emblem":  the boy's trigger emblem fields (the ones BoySetTriggerEmblem in asc_boy.lua sets
--              for scripted shots) aim at the cursor, as a direct aim;
--   "command": Trico's own pulse control commands (ready, then shot);
--   "both":    the emblem, plus the ready command on R2 and the shot command as soon as the pulse
--              aims (in game the emblem alone gets the pulse to Search and Aim, but not to Shot);
--   "aim":     as "both", but the emblem is shown as the cursor the whole time the cursor is up,
--              so the player sees where the shield's ray lands and the pulse can search early
--              (in game the emblem alone makes Trico aim and fire within a few seconds, as when
--              the boy holds the shield up, so this one fires without R2);
--   "hold":    hold R2 to aim and fire, like the boy holding the shield up: while R2 is held the
--              emblem is on the cursor and the ready and shot commands go out as in "both";
--              letting go puts everything back;
--   "aimhold": hold L2 to aim: the emblem is the cursor and Trico charges and fires on its own
--              as with the boy's shield; R2 while aiming sends the ready and shot commands at once.
--              The aim is a direction from Trico's head (starting the way Trico faces, level):
--              the right stick turns it left and right and tilts it up and down, and the emblem
--              sits AIM_RANGE along it, so targets across gaps and up walls can be aimed at. The
--              game's camera follows the emblem (its reticle stays at the centre of the screen),
--              so this plays like aiming the boy's shield. The shield's green guide beam is hidden while the
--              Trico player aims (the player gets the shield's cursor, not its beam). Letting go
--              of L2 puts everything back.
-- Both also turn on the free pulse mode (fire without the emblem being Trico's interest) and point
-- Trico's tail at the cursor (OverwriteTailPulseDst).

CoopPulse = CoopPulse or {}

local TRIGGER_NAME = "game/objects/props/chara/tritrigger_01/tritrigger_01"
local CURSOR_SOURCE = "game/effects/trigger_mark_boy"
local CMD_READY = "ControlCommand_C_A_tail_pulse_ready"
local CMD_SHOT = "ControlCommand_C_A_tail_pulse_shot"
local CMD_CANCEL = "ControlCommand_C_A_tail_pulse_cancel"
local DEADZONE = 0.25
local SPEED = 8.0          -- cursor metres per second at full stick
local START_RANGE = 10.0   -- where the cursor appears, ahead of Trico
local AIM_RANGE = 15.0     -- "aimhold": metres from Trico's head to the emblem
local AIM_HEAD = 3.0       -- Trico's head above his position when the bone isn't found
local AIM_YAW_SPEED = 90   -- degrees per second at full stick
local AIM_PITCH_SPEED = 45
local AIM_PITCH_MIN = -35  -- degrees below level
local AIM_PITCH_MAX = 50
local MIN_RANGE = 4.0
local MAX_RANGE = 25.0
local HIDE_AFTER = 8.0     -- seconds without aiming before the cursor goes away
local FIRE_HOLD = 10.0     -- at most this many seconds of aim after R2; the aim ends earlier once
                           -- the pulse has gone through "Shot" (or the bolt has shown) and stopped
local SHOT_AFTER = 1.0     -- "command": seconds from ready to shot
local SHOT_AFTER_BOTH = 2.0  -- emblem methods: shot after this long even if the pulse never aims
local REPORT = 0.5

CoopPulse.method = CoopPulse.method or "aimhold"

function CoopPulse.Reset()
	CoopPulse.active = false      -- the cursor is shown
	CoopPulse.cursor = nil
	CoopPulse.idle = 0
	CoopPulse.firing = 0          -- seconds of aim left after R2
	CoopPulse.shotIn = nil
	CoopPulse.ownEmblem = false   -- the emblem fields are ours to clear
	CoopPulse.reportIn = 0
	CoopPulse.lastError = nil
	CoopPulse.shots = 0
	CoopPulse.shot = false        -- this R2's pulse has reached "Shot" (or shown the bolt)
	CoopPulse.refused = nil       -- why the last R2 did nothing
end

local function Props()
	local ok, props = pcall(function() return Ai2.Utils.GetCondorAIProps() end)
	if ok then return props end
	return nil
end

-- A stage helper from asc_boy.lua: the mission scripts call it as MS.<name>.
local function StageHelper(name)
	local f = MS and MS[name]
	if type(f) == "function" then return f end
	f = _G[name]
	if type(f) == "function" then return f end
	return nil
end

-- Does the boy have the shield? (true / false, or nil when the game cannot say)
function CoopPulse.HasShield()
	local have = StageHelper("BoyHaveTrigger")
	if have then
		local ok, has = pcall(have)
		if ok then return has and true or false end
	end
	if GameFlag then return GameFlag.HaveTrigger == 1 end
	return nil
end

local function PlayerInstruction()
	local ai = FindObject("PlayerAI")
	return ai and ai.instruction
end

-- The cursor's step for a right stick (x right, y up) and a camera forward (fx, fz).
function CoopPulse.Step(rx, ry, fx, fz, dt)
	local len = math.sqrt(rx * rx + ry * ry)
	if len < DEADZONE then return nil end
	if len > 1 then rx, ry = rx / len, ry / len end
	local rgx, rgz = -fz, fx   -- right of the forward, as in coop_trico.lua
	local s = SPEED * dt
	return (fx * ry + rgx * rx) * s, (fz * ry + rgz * rx) * s
end

-- "aimhold": turn the aim direction with the right stick (right turns right: the right of a
-- forward (x, z) is (-z, x), as in coop_trico.lua) and return the emblem's spot along it.
function CoopPulse.AimPoint(rx, ry, dt, origin)
	if math.sqrt(rx * rx + ry * ry) >= DEADZONE then
		CoopPulse.aimYaw = (CoopPulse.aimYaw - rx * AIM_YAW_SPEED * dt) % 360
		CoopPulse.aimPitch = math.max(AIM_PITCH_MIN, math.min(AIM_PITCH_MAX,
			CoopPulse.aimPitch + ry * AIM_PITCH_SPEED * dt))
	end
	local yaw, pitch = math.rad(CoopPulse.aimYaw), math.rad(CoopPulse.aimPitch)
	local flat = math.cos(pitch) * AIM_RANGE
	return Vector(origin.X + math.sin(yaw) * flat, origin.Y + math.sin(pitch) * AIM_RANGE,
		origin.Z + math.cos(yaw) * flat)
end

-- Trico's head, where the aim starts.
function CoopPulse.AimOrigin(pos)
	local ok, head = pcall(function()
		return GameCharacter.FindBone(CharacterMngr:GetTrico(), "jt_Head").worldPosition
	end)
	if ok and head then return Vector(head) end
	return Vector(pos.X, pos.Y + AIM_HEAD, pos.Z)
end

-- Keep the cursor between MIN_RANGE and MAX_RANGE from Trico (on the ground plane).
function CoopPulse.Clamp(trico, x, z)
	local dx, dz = x - trico.X, z - trico.Z
	local d = math.sqrt(dx * dx + dz * dz)
	if d < 0.001 then return trico.X + MIN_RANGE, z end
	local c = math.max(MIN_RANGE, math.min(MAX_RANGE, d))
	return trico.X + dx / d * c, trico.Z + dz / d * c
end

-- The ground under a point: the nearest navmesh position (Trico's), else the point itself.
local function Ground(props, pos)
	local ok, near = pcall(Ai2FindNearestPosition, props.currentInfo.ai2Ext, pos, Vector(3, 12, 3))
	if ok and near and near.found == 1 and near.pos then return Vector(near.pos), "nav" end
	return pos, "free"
end

local function CursorEffect()
	local name = CoopPulse.cursorName
	local obj = name and FindObject(name)
	if obj then return obj end
	local source = FindObject(CURSOR_SOURCE)
	if not source then return nil end
	local clone = Clone(CURSOR_SOURCE)
	obj = type(clone) == "string" and FindObject(clone) or clone
	if not obj then return nil end
	Connect(source.parent or "_world", obj)
	CoopPulse.cursorName = obj._object_name or clone
	return obj
end

local function ShowCursor(pos)
	local obj = CursorEffect()
	if not obj then CoopPulse.effect = "none" return end
	obj.entityFlags = "~Hide"
	SetWorldPosition(obj, tostring(pos))
	CoopPulse.effect = "shown"
end

local ClearEmblem

local function HideCursor()
	local name = CoopPulse.cursorName
	local obj = name and FindObject(name)
	if obj then obj.entityFlags = "|Hide" end
	CoopPulse.effect = "hidden"
	if CoopPulse.method == "aim" and CoopPulse.firing <= 0 then ClearEmblem() end
end

-- The shield's guide beam (PlayerAI.instruction.rayObj): hidden while the Trico player aims, its
-- own flags put back after.
local function HideBeam()
	local inst = PlayerInstruction()
	local ray = inst and inst.rayObj
	if not ray then CoopPulse.beam = "none" return end
	if CoopPulse.beamFlags == nil then
		local ok, flags = pcall(function() return ray.entityFlags end)
		CoopPulse.beamFlags = ok and type(flags) == "string" and flags or false
		CoopPulse.beamObj = ray
	end
	ray.entityFlags = "|Hide"
	CoopPulse.beam = "hidden"
end

local function RestoreBeam()
	local ray = CoopPulse.beamObj
	if ray then
		if CoopPulse.beamFlags then ray.entityFlags = CoopPulse.beamFlags
		else ray.entityFlags = "~Hide" end
	end
	CoopPulse.beamFlags = nil
	CoopPulse.beamObj = nil
	CoopPulse.beam = nil
end

local function SetEmblem(pos)
	local inst = PlayerInstruction()
	if not inst then error("no PlayerAI") end
	if inst.triggerObj and not CoopPulse.ownEmblem then return false end  -- the boy is aiming
	local trigger = FindObject(TRIGGER_NAME)
	if not trigger then error("no " .. TRIGGER_NAME) end
	inst.triggerObj = trigger
	inst.triggerOfs = Vector(0.25, 0, 0)
	inst.triggerBlendRatio = 1.0
	inst.triggerAimPos = pos
	inst.triggerIsDirectAim = true
	inst.triggerIsDemo = true
	CoopPulse.ownEmblem = true
	return true
end

function ClearEmblem()
	if not CoopPulse.ownEmblem then return end
	CoopPulse.ownEmblem = false
	local inst = PlayerInstruction()
	if inst then
		inst.triggerObj = nil
		inst.triggerIsDemo = false
	end
end

local function Fire(props)
	local cu = Ai2.CondorUtils
	local inst = PlayerInstruction()
	if inst and inst.triggerObj and not CoopPulse.ownEmblem then
		CoopPulse.refused = "boy-aiming"
		return
	end
	cu.EnableFreePulseMode(props)
	cu.OverwriteTailPulseDst(props, CoopPulse.cursor)
	if CoopPulse.method ~= "emblem" then
		Ai2.Utils.ReqCtrlCmd(props.currentInfo.ai2Ext, CMD_READY)
		CoopPulse.shotIn = CoopPulse.method == "command" and SHOT_AFTER or SHOT_AFTER_BOTH
	end
	if CoopPulse.method ~= "command" then SetEmblem(CoopPulse.cursor) end
	CoopPulse.firing = FIRE_HOLD
	CoopPulse.shot = false
	CoopPulse.shots = CoopPulse.shots + 1
	CoopPulse.refused = nil
end

local function EndFire(props, cancel)
	if CoopPulse.firing <= 0 and not CoopPulse.ownEmblem then return end
	CoopPulse.firing = 0
	CoopPulse.shotIn = nil
	ClearEmblem()
	if CoopPulse.beamObj then RestoreBeam() end
	if not props then return end
	local cu = Ai2.CondorUtils
	pcall(cu.ResetOverwriteTailPulseDst, props)
	pcall(cu.DisableFreePulseMode, props)
	if cancel and CoopPulse.method ~= "emblem" then
		pcall(Ai2.Utils.ReqCtrlCmd, props.currentInfo.ai2Ext, CMD_CANCEL)
	end
end

local function Report(props, shield)
	if not CoopDebug or not CoopDebug.enabled then return end
	local tp = props and props.currentInfo and props.currentInfo.ai2Ext
		and props.currentInfo.ai2Ext.tailPulse
	local okShoot, shooting = pcall(Ai2.CondorUtils.IsShootingPulse)
	local hold = StageHelper("BoyHoldTrigger")
	local okHold, holding = false, nil
	if hold then okHold, holding = pcall(hold) end
	local inst = PlayerInstruction()
	local c = CoopPulse.cursor
	CoopDebug.Print("PU %s sh=%s aim=%s st=%s md=%s tg=%s zap=%s cur=%s fx=%s fire=%.1f n=%d shot=%s own=%s boy=%s%s",
		CoopPulse.method, tostring(shield), okHold and tostring(holding) or "?",
		tostring(tp and tp.state), tostring(tp and tp.mode), tostring(tp and tp.targetEntName),
		okShoot and tostring(shooting) or "?",
		c and string.format("%.1f,%.1f,%.1f", c.X, c.Y, c.Z) or "-", tostring(CoopPulse.effect),
		CoopPulse.firing, CoopPulse.shots, tostring(CoopPulse.shot), tostring(CoopPulse.ownEmblem),
		tostring(inst and inst.triggerObj ~= nil), CoopPulse.refused and (" no=" .. CoopPulse.refused) or "")
	if CoopPulse.beam then CoopDebug.Print("PU beam %s", CoopPulse.beam) end
end

-- "aimhold": L2 held aims (the emblem is the cursor), R2 shoots at once, letting go puts it back.
local function StepAimHold(dt, input, props, pos, fx, fz)
	local info = props.currentInfo
	local aiming = input.hold and input.hold.L2
	if not aiming then
		if CoopPulse.active then HideCursor() end
		CoopPulse.active = false
		EndFire(props, not CoopPulse.shot)
		return
	end
	local inst = PlayerInstruction()
	if inst and inst.triggerObj and not CoopPulse.ownEmblem then
		CoopPulse.refused = "boy-aiming"
		return
	end
	local cu = Ai2.CondorUtils
	if not CoopPulse.active then
		CoopPulse.active = true
		-- start the way Trico faces (else the camera's way), level
		local dir = info.ai2Ext and info.ai2Ext.dirXZ
		local dx0, dz0 = fx, fz
		if dir and (dir.X ~= 0 or dir.Z ~= 0) then dx0, dz0 = dir.X, dir.Z end
		CoopPulse.aimYaw = math.deg(math.atan2(dx0, dz0))
		CoopPulse.aimPitch = 0
		CoopPulse.shot = false
		CoopPulse.shotIn = nil
		CoopPulse.shots = CoopPulse.shots + 1
		CoopPulse.refused = nil
		cu.EnableFreePulseMode(props)
	end
	CoopPulse.firing = FIRE_HOLD    -- for as long as L2 is held
	CoopPulse.cursor = CoopPulse.AimPoint(input.rx or 0, input.ry or 0, dt, CoopPulse.AimOrigin(pos))
	if CoopPulse.ownEmblem then inst.triggerAimPos = CoopPulse.cursor
	else SetEmblem(CoopPulse.cursor) end
	pcall(cu.OverwriteTailPulseDst, props, CoopPulse.cursor)
	HideBeam()

	local tp = info.ai2Ext and info.ai2Ext.tailPulse
	local state = tp and tp.state
	local okZap, zap = pcall(cu.IsShootingPulse)
	if state == "Shot" or (okZap and zap) then CoopPulse.shot = true end
	if input.down and input.down.R2 then
		Ai2.Utils.ReqCtrlCmd(info.ai2Ext, CMD_READY)
		Ai2.Utils.ReqCtrlCmd(info.ai2Ext, CMD_SHOT)
		-- and once more when the pulse aims, if it was still searching
		CoopPulse.shotIn = (state == "Aim" or CoopPulse.shot) and nil or SHOT_AFTER_BOTH
	elseif CoopPulse.shotIn then
		CoopPulse.shotIn = CoopPulse.shotIn - dt
		if state == "Aim" or CoopPulse.shotIn <= 0 then
			CoopPulse.shotIn = nil
			Ai2.Utils.ReqCtrlCmd(info.ai2Ext, CMD_SHOT)
		end
	end
end

local function Step(dt, input, props, shield)
	local info = props.currentInfo
	if not shield or not input then
		if CoopPulse.active then HideCursor() end
		CoopPulse.active = false
		EndFire(props, true)
		if input and input.down and input.down.R2 then CoopPulse.refused = "no-shield" end
		return
	end
	local pos = info.aiPos
	local fx, fz = nil, nil
	if CoopTrico and CoopTrico.CameraForward then
		local ok, x, z = pcall(CoopTrico.CameraForward, pos)
		if ok then fx, fz = x, z end
	end
	if not fx then
		local d = info.ai2Ext and info.ai2Ext.dirXZ
		fx, fz = d and d.X or 0, d and d.Z or 1
	end
	if CoopPulse.method == "aimhold" then return StepAimHold(dt, input, props, pos, fx, fz) end
	local dx, dz = CoopPulse.Step(input.rx or 0, input.ry or 0, fx, fz, dt)
	local fire = input.down and input.down.R2
	if dx or fire or (input.hold and input.hold.R2) then
		CoopPulse.idle = 0
		if not CoopPulse.active then
			CoopPulse.active = true
			-- back where it was if that is still in range, else ahead of Trico
			local c = CoopPulse.cursor
			local far = c and math.sqrt((c.X - pos.X) ^ 2 + (c.Z - pos.Z) ^ 2) or MAX_RANGE + 1
			if far > MAX_RANGE then
				CoopPulse.cursor = Vector(pos.X + fx * START_RANGE, pos.Y, pos.Z + fz * START_RANGE)
			end
		end
	elseif CoopPulse.active and CoopPulse.firing <= 0 then
		CoopPulse.idle = CoopPulse.idle + dt
		if CoopPulse.idle >= HIDE_AFTER then
			CoopPulse.active = false
			HideCursor()
			return
		end
	end
	if not CoopPulse.active then return end
	local c = CoopPulse.cursor
	local x, z = CoopPulse.Clamp(pos, c.X + (dx or 0), c.Z + (dz or 0))
	CoopPulse.cursor = Ground(props, Vector(x, c.Y, z))
	ShowCursor(CoopPulse.cursor)
	if CoopPulse.method == "aim" and CoopPulse.firing <= 0 and not fire then
		-- the emblem is the cursor; never over the boy's own aim
		local inst = PlayerInstruction()
		if inst and (CoopPulse.ownEmblem or not inst.triggerObj) then
			if CoopPulse.ownEmblem then inst.triggerAimPos = CoopPulse.cursor
			else SetEmblem(CoopPulse.cursor) end
		end
	end

	local held = input.hold and input.hold.R2
	if CoopPulse.method == "hold" then
		if held and CoopPulse.firing <= 0 then Fire(props) end
		if held and CoopPulse.firing > 0 then CoopPulse.firing = FIRE_HOLD end
		if not held and CoopPulse.firing > 0 then
			EndFire(props, not CoopPulse.shot)
			return
		end
	elseif fire and CoopPulse.firing <= 0 then
		Fire(props)
	end
	if CoopPulse.firing > 0 then
		CoopPulse.firing = CoopPulse.firing - dt
		if CoopPulse.ownEmblem then
			local inst = PlayerInstruction()
			if inst then inst.triggerAimPos = CoopPulse.cursor end
		end
		pcall(Ai2.CondorUtils.OverwriteTailPulseDst, props, CoopPulse.cursor)
		local tp = info.ai2Ext and info.ai2Ext.tailPulse
		local state = tp and tp.state
		if CoopPulse.shotIn then
			CoopPulse.shotIn = CoopPulse.shotIn - dt
			if CoopPulse.shotIn <= 0 or (CoopPulse.method ~= "command" and state == "Aim") then
				CoopPulse.shotIn = nil
				Ai2.Utils.ReqCtrlCmd(info.ai2Ext, CMD_SHOT)
			end
		end
		-- done once the pulse has shot and moved on
		local okZap, zap = pcall(Ai2.CondorUtils.IsShootingPulse)
		if state == "Shot" or (okZap and zap) then
			CoopPulse.shot = true
		elseif CoopPulse.shot and CoopPulse.method ~= "hold" then
			CoopPulse.firing = 0
		end
		if CoopPulse.firing <= 0 then EndFire(props, false) end
	end
end

-- Call once per frame with the Trico player's input (nil when nobody plays Trico).
function CoopPulse.Update(dt, input)
	dt = dt or 0
	local props = Props()
	local shield = CoopPulse.HasShield()
	if shield ~= CoopPulse.lastShield then
		CoopPulse.lastShield = shield
		if CoopDebug then CoopDebug.Print("PU shield %s", tostring(shield)) end
	end
	CoopPulse.reportIn = CoopPulse.reportIn - dt
	if CoopPulse.reportIn <= 0 then
		CoopPulse.reportIn = REPORT
		pcall(Report, props, shield)
	end
	if not props or not props.currentInfo or not props.currentInfo.aiPos then return end
	local ok, err = pcall(Step, dt, input, props, shield)
	if not ok and err ~= CoopPulse.lastError then
		CoopPulse.lastError = err
		if CoopDebug then CoopDebug.Print("PU error %s", tostring(err)) end
	end
end

-- Put everything back (when the player leaves the Trico role).
function CoopPulse.Release()
	local props = Props()
	pcall(EndFire, props, true)
	if CoopPulse.active then pcall(HideCursor) end
	CoopPulse.active = false
end

CoopPulse.Reset()
