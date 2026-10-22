-- tlg-coop: the boy's R1 gestures as the Trico player's own buttons.
--
-- The boy's gestures (R1 + Cross / Square / Triangle) point Trico at a dummy interest of
-- subtype Instruction_Unique, whose action hierarchy (Ai2.IntActHierarchy.Condor.Instruction)
-- picks the action from the gesture once the eye-contact handshake has granted it. The Trico
-- player skips the handshake: the scripts put such an interest ahead of Trico and set what the
-- handshake sets (condorinstruction.lua), so the game's own action runs, approach and all. Sending
-- the action's control command alone does nothing while Trico idles (c_wait), as tried in game.
--   Cross:    jump. Ahead (the stick's way, else Trico's facing) the scripts look for a point on
--             another navmesh island that a special edge leads to; an auto-Trico style interest
--             there makes Trico take the jump its own way. With none, the jump gesture (Trico
--             pounces, as the boy's gesture does without a target).
--   Square:   the attack gesture (a side swipe, the side the game picks from the target's angle,
--             so the target goes on the stick's side).
--   Circle:   the boy's "Action" gesture, on whatever Trico is interested in (on a barrel box
--             Trico breaks it; edible barrels ignore gestures, Trico just eats them); with nothing
--             but the boy or a hint in its interest, on the dummy ahead (Trico tilts its head).
--             With the stick held left or right: a pull with that paw (C_touch_left/right_pull),
--             sent once Trico stands still (steering waits for it, at most PENDING_TIME).
--   Up:       rear up on the hind legs (C_rise, which Trico takes from idle too), held while the
--             button is.
-- Single moves (one control command each, sent only while Trico stands, waits or sits; refused
-- and logged otherwise):
--   Triangle stomp; R1 roar; L1 threat posture (held, ended on release); Down sit, then lie down,
--   then stand up; Left stretch (front, up, back in turn); Right hind-leg kick (left and right in
--   turn).
-- Context moves (eating, climbing, swimming...) stay with Trico's AI: buttons only add requests.
-- Nothing here runs while the Trico player aims the lightning (L2); Circle stays the boy's shield.

CoopActions = CoopActions or {}

local JUMP_NAME = "CoopActions_Jump"
local JUMP_INTEREST = "intr_AutoTorico"
local JUMP_LEVEL_NAME = "CoopActions_Jump"
local JUMP_DISTANCES = { 6.0, 10.0, 14.0 }
local JUMP_ANGLES = { 0, 25, -25 }            -- degrees from the jump's way
local JUMP_HEIGHTS = { 0, 5, -5 }             -- metres up and down (ledges)
local JUMP_ARRIVED = 4.0                      -- metres from the target that count as there
local JUMP_TIMEOUT = 10.0
local GESTURE_NAME = "CoopActions_Gesture"
local GESTURE_INTEREST = "Instruction_Unique"
local GESTURE_LEVEL_NAME = "CoopActions_Gesture"
local GESTURE_DISTANCE = 4.5                  -- metres ahead of Trico
local GESTURE_SIDE = 2.0                      -- metres to the side for a side swipe
local GESTURE_TIMEOUT = 5.0
local GESTURE_STARTS = {                      -- the animation states the gesture's actions start
	Gesture_Attack = { "ext_c_attack_yoko" },
	Gesture_Jump = { "ext_c_Gesture_attack_below" },
}
local RISE_REPEAT = 0.5
local PENDING_TIME = 2.0                      -- seconds a paw pull waits for Trico to stand still
local PULL_STICK = 0.5                        -- stick x beyond which Circle pulls with that paw
local WATCH = 3.0                             -- seconds of animation reports after an action
local DEADZONE = 0.3

CoopActions.commands = {
	rise = "ControlCommand_C_rise",
	roar = "ControlCommand_C_roar",
	threat = "ControlCommand_C_threat_posture",
	threatEnd = "ControlCommand_C_threat_posture_end",
	stretchFront = "ControlCommand_C_stretch_front",
	stretchUp = "ControlCommand_C_stretch_up",
	stretchBack = "ControlCommand_C_stretch_back",
	sit = "ControlCommand_C_sit",
	lieDown = "ControlCommand_C_lie_down",
	pullLeft = "ControlCommand_C_touch_left_pull",
	pullRight = "ControlCommand_C_touch_right_pull",
	kickLeft = "ControlCommand_C_attack_hind_left",
	kickRight = "ControlCommand_C_attack_hind_right",
	stomp = "ControlCommand_C_attack_below",
}
local STRETCHES = { "stretchFront", "stretchUp", "stretchBack" }
-- animation states a single move may start from
local IDLE = { "c_stand", "c_wait", "c_idle" }
local SEATED = { "c_sit", "ext_c_sit", "c_lie", "ext_c_lie", "c_liedown", "ext_c_liedown" }

function CoopActions.Reset()
	CoopActions.jump = nil        -- { target, timer } while a jump target is held
	CoopActions.gesture = nil     -- { type, timer } while a gesture waits for its action
	CoopActions.pending = nil     -- { name, timer } a single move waiting for Trico to stand still
	CoopActions.riseIn = nil
	CoopActions.lastSwipe = "right"
	CoopActions.watch = 0
	CoopActions.lastAction = nil
	CoopActions.lastAnim = nil
	CoopActions.lastError = nil
end

local function Props()
	local ok, props = pcall(function() return Ai2.Utils.GetCondorAIProps() end)
	if ok then return props end
	return nil
end

local function Say(fmt, ...)
	if CoopDebug then CoopDebug.Print(fmt, ...) end
end

local function Command(props, name)
	local cmd = CoopActions.commands[name]
	if name == "stand" then cmd = props.standCommand or "ControlCommand_C_stand" end
	Ai2.Utils.ReqCtrlCmd(props.currentInfo.ai2Ext, cmd)
	return cmd
end

-- The way an action goes: the stick's camera-relative way, else Trico's facing.
function CoopActions.Way(props, input)
	local pos = props.currentInfo.aiPos
	local d = props.currentInfo.ai2Ext and props.currentInfo.ai2Ext.dirXZ
	local faceX, faceZ = d and d.X or 0, d and d.Z or 1
	local sx, sy = nil, nil
	if CoopTrico and CoopTrico.Stick then sx, sy = CoopTrico.Stick(input) end
	if not sx then return faceX, faceZ end
	local fx, fz = nil, nil
	if CoopTrico.CameraForward then
		local ok, x, z = pcall(CoopTrico.CameraForward, pos)
		if ok then fx, fz = x, z end
	end
	if not fx then fx, fz = faceX, faceZ end
	local wx, wz = fx * sy - fz * sx, fz * sy + fx * sx  -- right is (-fz, fx), as in coop_trico.lua
	local len = math.sqrt(wx * wx + wz * wz)
	if len < 0.001 then return faceX, faceZ end
	return wx / len, wz / len
end

-- A point a special edge leads to (another navmesh island, reachable only through the edge).
function CoopActions.FindJumpTarget(props, wayX, wayZ)
	local pos = props.currentInfo.aiPos
	local check = Ai2.Utils.Navmesh.CheckSameIsland
	for _, dist in ipairs(JUMP_DISTANCES) do
		for _, deg in ipairs(JUMP_ANGLES) do
			local a = math.rad(deg)
			local x = wayX * math.cos(a) - wayZ * math.sin(a)
			local z = wayX * math.sin(a) + wayZ * math.cos(a)
			for _, h in ipairs(JUMP_HEIGHTS) do
				local p = Vector(pos.X + x * dist, pos.Y + h, pos.Z + z * dist)
				local okA, walk = pcall(check, props, pos, p, true)
				local okB, edge = pcall(check, props, pos, p, false)
				if okA and okB and not walk and edge then return p end
			end
		end
	end
	return nil
end

-- The gesture target entity, made like auto-Trico makes its interests.
local function NewInterest(name, subType)
	local root = FindObject(name)
	if root then return root end
	root = NewObject("SceneRoot")
	Connect("_world", root)
	RenameObject(root._object_name, name)
	local intr = NewObject("GameObjectExt")
	intr.Type = "Interest"
	intr.SubType = subType
	Connect(root, intr)
	RenameObject(intr._object_name, name .. "_Interest")
	return root
end

-- What the eye-contact handshake sets once it succeeds (condorinstruction.lua EyeContact).
local function GrantGesture(work, gType, target, info)
	work.EyeContact_GestureMode = gType
	work.EyeContact_RecentGesture = gType
	work.EyeContact_LastGesture = gType
	work.EyeContact_GesturePermitAction = true
	work.EyeContact_GesturePermitActionTarget = target
	work.EyeContact_GesturePermitAction_BeforeExecAct = info.execAction and info.execAction.name or nil
	work.EyeContact_GesturePermitAction_BeforeExecSpAct = nil
	work.InterestEntity_ByInstruction = target
	work.GestureType_ByInstruction = gType
	work.InterestDecision_ByInstruction = true
end

local function DropGesture(props, why)
	local g = CoopActions.gesture
	if not g then return end
	CoopActions.gesture = nil
	local info = props and props.currentInfo
	local work = info and info.work
	if work and work.EyeContact_GestureMode == g.type then
		work.EyeContact_GestureMode = nil
		work.EyeContact_GesturePermitAction = false
		work.EyeContact_GesturePermitActionTarget = nil
		work.InterestEntity_ByInstruction = nil
		work.GestureType_ByInstruction = nil
		work.InterestDecision_ByInstruction = false
	end
	local root = FindObject(GESTURE_NAME)
	local ai = info and info.ai2Ext
	if root and ai then
		pcall(Ai2.Utils.ResetInterestLevelAddition, ai, root, GESTURE_LEVEL_NAME)
		pcall(Ai2.Utils.InterestExclusionSwitching, ai, root, true)
	end
	Say("AC %s end %s", g.type, why)
end

-- Put the gesture target ahead (dx, dz is the way, side shifts it) and start the gesture.
local function Gesture(props, gType, wayX, wayZ, side)
	DropGesture(props, "replaced")
	local info = props.currentInfo
	local pos = info.aiPos
	side = side or 0
	local gx = pos.X + wayX * GESTURE_DISTANCE - wayZ * side
	local gz = pos.Z + wayZ * GESTURE_DISTANCE + wayX * side
	local ai = info.ai2Ext
	local root = NewInterest(GESTURE_NAME, GESTURE_INTEREST)
	SetWorldPosition(root, tostring(Vector(gx, pos.Y, gz)))
	Ai2.Utils.RegisterInterest(ai, root)
	Ai2.Utils.InterestExclusionSwitching(ai, root, false)
	Ai2.Utils.InterestLevelAddition(ai, root, GESTURE_LEVEL_NAME, "addMax")
	CoopActions.gesture = { type = gType, timer = GESTURE_TIMEOUT }
	return string.format("target %.1f,%.1f", gx, gz)
end

local function StartsWith(anim, prefixes)
	for _, prefix in ipairs(prefixes) do
		if string.sub(anim, 1, string.len(prefix)) == prefix then return true end
	end
	return false
end

local function Lying(anim) return string.find(anim, "lie") ~= nil end

-- A single move: one command, only from a standing, waiting (or, for the posture moves, seated)
-- Trico. Returns the detail for the report.
local function Move(props, name, anim, seatedToo)
	if not (StartsWith(anim, IDLE) or (seatedToo and StartsWith(anim, SEATED))) then
		return "refused " .. anim
	end
	return Command(props, name)
end

-- Circle: the Action gesture on Trico's current interest, else on the dummy ahead.
local function Action(props, input)
	local info = props.currentInfo
	local intr = info.interestEntity
	local okName, name = pcall(function() return intr and intr:GetName() end)
	local plain = not okName or not name or name == "Player" or string.find(name, "^Hint")
		or string.find(name, "^Coop")
	if plain then
		local wayX, wayZ = CoopActions.Way(props, input)
		return "dummy " .. Gesture(props, "Gesture_Action", wayX, wayZ)
	end
	DropGesture(props, "replaced")
	CoopActions.gesture = { type = "Gesture_Action", timer = GESTURE_TIMEOUT, target = name }
	if info.work then GrantGesture(info.work, "Gesture_Action", intr, info) end
	return "on " .. name
end

local function Started(anim, gType)
	for _, prefix in ipairs(GESTURE_STARTS[gType] or {}) do
		if string.sub(anim, 1, string.len(prefix)) == prefix then return true end
	end
	return false
end

-- While a gesture waits: grant it once Trico's interest is the target; drop it once its action
-- has started, or when the time is up.
local function TickGesture(dt, props, anim)
	local g = CoopActions.gesture
	if not g then return end
	g.timer = g.timer - dt
	if Started(anim, g.type) then
		DropGesture(props, "started " .. anim)
		return
	end
	if g.timer <= 0 then
		DropGesture(props, "timeout")
		return
	end
	local info = props.currentInfo
	local intr = info.interestEntity
	local okName, name = pcall(function() return intr and intr:GetName() end)
	if okName and name == (g.target or GESTURE_NAME) and info.work then
		GrantGesture(info.work, g.type, intr, info)
		g.granted = true
	end
end

local function DropJump(props, why)
	local j = CoopActions.jump
	if not j then return end
	CoopActions.jump = nil
	local root = FindObject(JUMP_NAME)
	local ai = props and props.currentInfo and props.currentInfo.ai2Ext
	if root and ai then
		pcall(Ai2.Utils.ResetInterestLevelAddition, ai, root, JUMP_LEVEL_NAME)
		pcall(Ai2.Utils.InterestExclusionSwitching, ai, root, true)
	end
	Say("AC jump end %s", why)
end

local function Jump(props, input)
	local wayX, wayZ = CoopActions.Way(props, input)
	local target = CoopActions.FindJumpTarget(props, wayX, wayZ)
	if not target then
		return "gesture " .. Gesture(props, "Gesture_Jump", wayX, wayZ)
	end
	local ai = props.currentInfo.ai2Ext
	local root = NewInterest(JUMP_NAME, JUMP_INTEREST)
	SetWorldPosition(root, tostring(target))
	Ai2.Utils.RegisterInterest(ai, root)
	Ai2.Utils.InterestExclusionSwitching(ai, root, false)
	Ai2.Utils.InterestLevelAddition(ai, root, JUMP_LEVEL_NAME, "addMax")
	CoopActions.jump = { target = target, timer = JUMP_TIMEOUT }
	return string.format("edge %.1f,%.1f,%.1f", target.X, target.Y, target.Z)
end

local function Attack(props, input)
	local side
	if input.x and input.x > DEADZONE then side = "right"
	elseif input.x and input.x < -DEADZONE then side = "left"
	else side = CoopActions.lastSwipe == "right" and "left" or "right" end
	CoopActions.lastSwipe = side
	local d = props.currentInfo.ai2Ext and props.currentInfo.ai2Ext.dirXZ
	local faceX, faceZ = d and d.X or 0, d and d.Z or 1
	-- right of the facing is (-faceZ, faceX) rotated, as in coop_trico.lua: shift by -side for right
	local shift = side == "right" and GESTURE_SIDE or -GESTURE_SIDE
	return side .. " " .. Gesture(props, "Gesture_Attack", faceX, faceZ, shift)
end

local function AnimState(props)
	local ok, st = pcall(Ai2.Utils.GetCurAnimState, props.currentInfo.ai2Ext)
	return ok and tostring(st) or "?"
end

local function Step(dt, input, props, anim)
	TickGesture(dt, props, anim)
	-- a held jump target: until Trico is there (or on its island), or the time is up
	local j = CoopActions.jump
	if j then
		j.timer = j.timer - dt
		local pos = props.currentInfo.aiPos
		local dx, dz = pos.X - j.target.X, pos.Z - j.target.Z
		local okSame, same = pcall(Ai2.Utils.Navmesh.CheckSameIsland, props, pos, j.target, true)
		if math.sqrt(dx * dx + dz * dz) < JUMP_ARRIVED or (okSame and same) then
			DropJump(props, "arrived")
		elseif j.timer <= 0 then
			DropJump(props, "timeout")
		end
	end
	if not input then
		CoopActions.riseIn = nil
		return
	end
	local down, hold = input.down or {}, input.hold or {}
	if hold.L2 then CoopActions.riseIn = nil return end   -- aiming the lightning
	local pend = CoopActions.pending
	if pend then
		pend.timer = pend.timer - dt
		if StartsWith(anim, IDLE) then
			CoopActions.pending = nil
			Say("AC pull %s anim=%s", Command(props, pend.name), anim)
			CoopActions.lastAction, CoopActions.watch, CoopActions.lastAnim = "pull", WATCH, nil
		elseif pend.timer <= 0 then
			CoopActions.pending = nil
			Say("AC pull refused %s", anim)
		end
	end

	local did, detail = nil, nil
	if down.Cross then did, detail = "jump", Jump(props, input)
	elseif down.Square then did, detail = "attack", Attack(props, input)
	elseif down.Up then
		Command(props, "rise")
		CoopActions.riseIn = RISE_REPEAT
		did, detail = "rise", "start"
	elseif down.Triangle then did, detail = "stomp", Move(props, "stomp", anim)
	elseif down.Circle and math.abs(input.x or 0) > PULL_STICK and math.abs(input.x) > math.abs(input.y or 0) then
		local name = input.x > 0 and "pullRight" or "pullLeft"
		CoopActions.pending = { name = name, timer = PENDING_TIME }
		did, detail = "pull", name .. " waiting"
	elseif down.Circle then did, detail = "action", Action(props, input)
	elseif down.R1 then did, detail = "roar", Move(props, "roar", anim)
	elseif down.L1 then
		did, detail = "threat", Move(props, "threat", anim)
		CoopActions.threat = string.sub(detail, 1, 7) ~= "refused"
	elseif down.Down then
		if Lying(anim) then did, detail = "stand", Move(props, "stand", anim, true)
		elseif StartsWith(anim, SEATED) then did, detail = "lie down", Move(props, "lieDown", anim, true)
		else did, detail = "sit", Move(props, "sit", anim) end
	elseif down.Left then
		CoopActions.stretch = (CoopActions.stretch or 0) % #STRETCHES + 1
		did, detail = "stretch", Move(props, STRETCHES[CoopActions.stretch], anim)
	elseif down.Right then
		CoopActions.kick = CoopActions.kick == "kickLeft" and "kickRight" or "kickLeft"
		did, detail = "kick", Move(props, CoopActions.kick, anim)
	end
	if CoopActions.threat and not hold.L1 then
		CoopActions.threat = false
		Command(props, "threatEnd")
		did, detail = did or "threat", detail or "end"
	end
	if hold.Up and CoopActions.riseIn and not did then
		CoopActions.riseIn = CoopActions.riseIn - dt
		if CoopActions.riseIn <= 0 then
			Command(props, "rise")
			CoopActions.riseIn = RISE_REPEAT
		end
	elseif not hold.Up then
		CoopActions.riseIn = nil
	end
	if did then
		CoopActions.lastAction = did
		CoopActions.watch = WATCH
		CoopActions.lastAnim = nil
		Say("AC %s %s anim=%s", did, tostring(detail), AnimState(props))
	end
end

-- Call once per frame with the Trico player's input (nil when nobody plays Trico).
function CoopActions.Update(dt, input)
	dt = dt or 0
	local props = Props()
	if not props or not props.currentInfo or not props.currentInfo.aiPos then return end
	local anim = AnimState(props)
	local ok, err = pcall(Step, dt, input, props, anim)
	if not ok and err ~= CoopActions.lastError then
		CoopActions.lastError = err
		Say("AC error %s", tostring(err))
	end
	-- after an action, report the animation it led to as it changes
	if CoopActions.watch > 0 then
		CoopActions.watch = CoopActions.watch - dt
		local anim = AnimState(props)
		if anim ~= CoopActions.lastAnim then
			CoopActions.lastAnim = anim
			Say("AC %s anim=%s", tostring(CoopActions.lastAction), anim)
		end
	end
end

-- Give Trico back (when the player leaves the Trico role).
function CoopActions.Release()
	local props = Props()
	CoopActions.pending = nil
	pcall(DropJump, props, "released")
	pcall(DropGesture, props, "released")
	CoopActions.riseIn = nil
end

CoopActions.Reset()
