-- tlg-coop: Trico driven by a player's left stick.
--
-- The stick picks a goal a few metres ahead of Trico, relative to the camera, and Trico's own
-- AI walks there (paths, turns, jumps and animation stay the game's). Several ways of handing the
-- goal to the AI are kept while they are compared on the real game; CoopTrico.strategy picks one:
--   "goal":     the way the developers' auto-Trico leads Trico (condordebugautotrico.lua): an
--               interest entity of type intr_AutoTorico sits on the goal with the top event
--               level (addMax, above the boy's loudest call). Trico's interest system picks it
--               and its approach walks there (Escape approach data, 1 m); nothing else of the AI
--               is overridden. The entity is excluded again when the stick is released.
--   "interest": the boy's pointing instruction target is moved to the goal and Trico is told
--               to be interested in it; the approach destination is overwritten with the goal.
--   "forward":  the move manager is asked for a free forward move towards the overwritten
--               approach destination (the game stops free moves while Trico is injured).
--   "steer":    "goal" plus the approach overwrite (destination and direction), and a turn
--               start: a goal far round from Trico's facing is first replaced by one to the
--               side, re-placed as Trico turns, until Trico faces the stick's way. While Trico
--               has no move planned (standing, waiting for his interests to pick the goal) the
--               move manager is also asked for a forward move to it (CoopTrico.kick), so he
--               sets off at once.
-- With the stick released, Trico is asked to stop and its AI is left alone.
--
-- The stick is camera-relative by default, with the camera's direction latched when the stick
-- leaves the dead zone and kept until it comes back (or the stick swings round more than
-- RELATCH degrees), so a camera that moves under a steady thumb doesn't move the goal.
-- CoopTrico.relative = "trico" makes it Trico-relative instead: up walks the way Trico faces,
-- left and right turn him. Where the side goal that starts a turn is off Trico's navmesh (a
-- narrow ledge or beam), Trico is asked to turn on the spot (the game's turn commands) instead.

CoopTrico = CoopTrico or {}

local PROXY_NAME = "PlayerAi2Instruction_InstProxyEntity"
local GOAL_NAME = "CoopTrico_Goal"
local GOAL_INTEREST = "intr_AutoTorico"
local GOAL_LEVEL_NAME = "CoopTrico_Steer"
local GOAL_LEVEL = "addMax"
local CAMERA_NAME = "_trico_camera"
local DEADZONE = 0.3
local GOAL_DISTANCE = 8.0      -- metres ahead of Trico at full stick
local MIN_GOAL_DISTANCE = 4.0
local NAV_STEPS = { 1.0, 0.6, 0.35 }  -- shorter goals to try when the full one is off Trico's navmesh
local BEHIND = -0.3            -- cosine to Trico's facing below which a goal is behind it
local TURN_START = -0.17       -- cosine to Trico's facing (about 100 degrees) beyond which
                               -- "steer" starts the turn with a goal to the side
local TURN_DONE = 0.5          -- ... until Trico faces within 60 degrees of the stick's way
local TURN_DISTANCE = 5.0
local TURNING = { steer = true }
local BEHIND_MIN = 7.0         -- a goal behind Trico stays at least a body length away, so it
                               -- turns round instead of shuffling under its own hindquarters
local REFRESH = 0.3            -- seconds between goal updates
local REGOAL = 2.0             -- metres the goal must change by before it is moved (so Trico's
                               -- path planner can commit to it)
local ARRIVING = 3.0           -- ... or how close Trico must get to it
local MIN_PATH_GOAL = 2.0      -- shortest navmesh path end worth walking to
local REPORT = 0.5             -- seconds between debug reports

local RELATCH = 60.0           -- degrees the stick must swing before the camera is read again
local TURN_GAP = 1.5           -- seconds between turn-on-the-spot requests
local YIELD = 1.5              -- seconds steering waits for a queued move (coop_actions.lua)

CoopTrico.strategy = CoopTrico.strategy or "goal"
CoopTrico.relative = CoopTrico.relative or "camera"
if CoopTrico.kick == nil then CoopTrico.kick = true end
CoopTrico.commands = {
	turn90Left = "ControlCommand_C_turn_90_left",
	turn90Right = "ControlCommand_C_turn_90_right",
	aboutLeft = "ControlCommand_C_turn_narrow_left",    -- 180 degrees on the spot (fits ledges)
	aboutRight = "ControlCommand_C_turn_narrow_right",
}

function CoopTrico.Reset()
	CoopTrico.steering = false
	CoopTrico.goal = nil
	CoopTrico.goalLive = false     -- the goal entity has Trico's interest
	CoopTrico.refreshIn = 0
	CoopTrico.reportIn = 0
	CoopTrico.lastError = nil
	CoopTrico.forward = nil        -- { x, z, source } of the last goal
	CoopTrico.nav = nil
	CoopTrico.turning = false
	CoopTrico.latch = nil          -- { fx, fz, source, stick angle } while the stick is held
	CoopTrico.turnIn = 0
end

local function Props()
	local ok, props = pcall(function() return Ai2.Utils.GetCondorAIProps() end)
	if ok then return props end
	return nil
end

-- Stick vector (x right, y up), 0..1 long, or nil inside the dead zone. `input` is the Trico
-- player's input from CoopInput (x, y in -1..1).
function CoopTrico.Stick(input)
	if not input then return nil end
	local x, y = input.x or 0, input.y or 0
	local len = math.sqrt(x * x + y * y)
	if len < DEADZONE then return nil end
	if len > 1 then x, y, len = x / len, y / len, 1 end
	return x, y, len
end

local function Flat(x, z)
	local len = math.sqrt(x * x + z * z)
	if len > 0.1 then return x / len, z / len end
	return nil
end

local function CameraPos()
	local cam = FindObject and FindObject(CAMERA_NAME)
	if cam and cam.worldPosition then return Vector(cam.worldPosition) end
	local cm = FindObject and FindObject("CameraManager")
	cam = cm and cm.camera
	return cam and cam.worldPosition
end

-- Camera forward on the ground plane, as (x, z, source): the game camera's view direction, else
-- the direction from the camera to Trico. The camera looks along its local -Z (in the game, with
-- the camera behind the boy facing Trico, local +Z pointed away from both).
function CoopTrico.CameraForward(tricoPos)
	local cam = FindObject and FindObject(CAMERA_NAME)
	local rot = cam and cam.worldRotation
	if rot then
		local view = Vector(0, 0, -1):Rotate(rot)
		local fx, fz = Flat(view.X, view.Z)
		if fx then return fx, fz, "view" end
	end
	local pos = CameraPos()
	if pos then
		local fx, fz = Flat(tricoPos.X - pos.X, tricoPos.Z - pos.Z)
		if fx then return fx, fz, "cam" end
	end
	return nil
end

-- The goal for a stick vector: camera-relative, on Trico's height. `scale` shortens it, but a
-- goal behind Trico (its facing is tricoDirX, tricoDirZ) stays BEHIND_MIN away.
function CoopTrico.GoalFor(tricoPos, tricoDirX, tricoDirZ, sx, sy, len, fx, fz, scale)
	local faceX, faceZ = tricoDirX, tricoDirZ
	if not fx then fx, fz = tricoDirX, tricoDirZ end
	-- right = forward rotated -90 degrees about Y (looking along +Z, right is -X)
	local rx, rz = -fz, fx
	local dx = fx * sy + rx * sx
	local dz = fz * sy + rz * sx
	local dlen = math.sqrt(dx * dx + dz * dz)
	if dlen < 0.001 then return nil end
	dx, dz = dx / dlen, dz / dlen
	local dist = (MIN_GOAL_DISTANCE + (GOAL_DISTANCE - MIN_GOAL_DISTANCE) * len) * (scale or 1)
	if dx * faceX + dz * faceZ < BEHIND and dist < BEHIND_MIN then dist = BEHIND_MIN end
	return tricoPos.X + dx * dist, tricoPos.Y, tricoPos.Z + dz * dist, dx, dz
end

-- Is `to` on Trico's navmesh island (no special edge on the way)? Unknown counts as yes. Also
-- returns where the straight navmesh path towards `to` ends.
local function Reachable(props, from, to)
	local ok, same, pathEnd = pcall(Ai2.Utils.Navmesh.CheckSameIsland, props, from, to, true)
	if not ok then return true, "?" end
	return same, same and "y" or "n", pathEnd
end

local function DistanceXZ(a, b)
	local dx, dz = a.X - b.X, a.Z - b.Z
	return math.sqrt(dx * dx + dz * dz)
end

local function OverwriteApproach(props, goal, dir)
	Ai2.Approach.Overwrite:Request(props, {
		destination = goal,
		destinationDir = dir,
		condition = function() return CoopTrico.steering end,
	})
end

-- The goal interest entity (made the way auto-Trico makes its own), created once per world.
local function GoalEntity()
	local root = FindObject(GOAL_NAME)
	if root then return root, false end
	root = NewObject("SceneRoot")
	Connect("_world", root)
	RenameObject(root._object_name, GOAL_NAME)
	local intr = NewObject("GameObjectExt")
	intr.Type = "Interest"
	intr.SubType = GOAL_INTEREST
	Connect(root, intr)
	RenameObject(intr._object_name, GOAL_NAME .. "_Interest")
	return root, true
end

local Steer = {}

function Steer.goal(props, goal, dir)
	local ai = props.currentInfo.ai2Ext
	local root, created = GoalEntity()
	SetWorldPosition(root, tostring(goal))
	if created or not CoopTrico.goalLive then
		Ai2.Utils.RegisterInterest(ai, root)
		Ai2.Utils.InterestExclusionSwitching(ai, root, false)
		CoopTrico.goalLive = true
	end
	-- the same name re-sets the addition, so this keeps the level while steering
	Ai2.Utils.InterestLevelAddition(ai, root, GOAL_LEVEL_NAME, GOAL_LEVEL)
end

function Steer.interest(props, goal, dir)
	local ai = props.currentInfo.ai2Ext
	local proxy = FindObject(PROXY_NAME)
	if not proxy then error("no " .. PROXY_NAME) end
	SetWorldPosition(proxy, tostring(goal))
	Ai2.Utils.RegisterInterest(ai, proxy)
	Ai2.Utils.InstructionAddition(props, proxy, "Interest")
	OverwriteApproach(props, goal, dir)
end

function Steer.forward(props, goal, dir)
	OverwriteApproach(props, goal, dir)
	props.currentInfo.apDestination = goal
	props.currentInfo.apDestinationDir = dir
	props.moveMngr:RequestForward({ isFreeMove = true, goalPos = goal, goalDir = dir })
end

function Steer.steer(props, goal, dir)
	Steer.goal(props, goal, dir)
	OverwriteApproach(props, goal, dir)
	if CoopTrico.kick and props.plannedMove == nil and not props.isInjuredMovementMode then
		props.currentInfo.apDestination = goal
		props.currentInfo.apDestinationDir = dir
		props.moveMngr:RequestForward({ isFreeMove = true, goalPos = goal, goalDir = dir })
		CoopTrico.kicks = (CoopTrico.kicks or 0) + 1
	end
end

-- The side goal that starts a turn from Trico's facing (faceX, faceZ) towards the way (dx, dz),
-- or nil when Trico already faces close enough. `turning` keeps a started turn going until
-- TURN_DONE.
function CoopTrico.TurnGoal(pos, faceX, faceZ, dx, dz, turning)
	local cos = dx * faceX + dz * faceZ
	if cos >= (turning and TURN_DONE or TURN_START) then return nil end
	-- the perpendicular of the facing on the side of the wanted way
	local lx, lz = -faceZ, faceX
	if lx * dx + lz * dz < 0 then lx, lz = faceZ, -faceX end
	return Vector(pos.X + lx * TURN_DISTANCE, pos.Y, pos.Z + lz * TURN_DISTANCE), Vector(lx, 0, lz)
end

-- The turn-on-the-spot command that turns Trico from facing (faceX, faceZ) towards the way
-- (wayX, wayZ): an about-turn beyond 120 degrees, else a quarter turn, on the way's side (the
-- right of a facing (x, z) is (-z, x)).
function CoopTrico.TurnCommand(faceX, faceZ, wayX, wayZ)
	local right = (-faceZ) * wayX + faceX * wayZ >= 0
	if wayX * faceX + wayZ * faceZ < -0.5 then return right and "aboutRight" or "aboutLeft" end
	return right and "turn90Right" or "turn90Left"
end

-- The frame the stick is read in, as a forward (x, z) and its source: Trico's facing
-- ("trico" steering), else the camera's, latched while the stick is held.
function CoopTrico.Frame(sx, sy, pos, dirXZ)
	if CoopTrico.relative == "trico" and dirXZ then return dirXZ.X, dirXZ.Z, "trico" end
	local angle = math.deg(math.atan2(sx, sy))
	local l = CoopTrico.latch
	if l then
		local swing = math.abs((angle - l[4] + 180) % 360 - 180)
		if swing <= RELATCH then return l[1], l[2], l[3] end
	end
	local ok, fx, fz, src = pcall(CoopTrico.CameraForward, pos)
	if not ok or not fx then return nil end
	CoopTrico.latch = { fx, fz, src .. "*", angle }
	return fx, fz, src .. "*"
end

local function DropGoal(props)
	if not CoopTrico.goalLive then return end
	CoopTrico.goalLive = false
	local root = FindObject(GOAL_NAME)
	local ai = props and props.currentInfo and props.currentInfo.ai2Ext
	if not root or not ai then return end
	pcall(Ai2.Utils.ResetInterestLevelAddition, ai, root, GOAL_LEVEL_NAME)
	pcall(Ai2.Utils.InterestExclusionSwitching, ai, root, true)
end

local function Release(props)
	CoopTrico.steering = false
	CoopTrico.goal = nil
	CoopTrico.turning = false
	CoopTrico.latch = nil
	DropGoal(props)
	if not props then return end
	if CoopTrico.strategy == "interest" then
		pcall(Ai2.Utils.ResetAllInstructionLevel, props.currentInfo.ai2Ext)
	end
	pcall(function() props.moveMngr:RequestStop(nil, props) end)
end

local function Vec(v)
	if not v then return "-" end
	return string.format("%.1f,%.1f,%.1f", v.X, v.Y, v.Z)
end

local function Report(props)
	if not CoopDebug or not CoopDebug.enabled then return end
	local info = props and props.currentInfo
	if not info or not info.aiPos then return end
	local p = info.aiPos
	local g = CoopTrico.goal
	local intr = info.interestEntity
	local okName, intrName = pcall(function() return intr and intr:GetName() end)
	local run = props.moveMngr and props.moveMngr.runningRequest
	CoopDebug.Print("TR %s %s p=%.1f,%.1f,%.1f g=%s pm=%s rq=%s in=%s it=%s md=%s",
		CoopTrico.strategy, CoopTrico.steering and "on" or "off", p.X, p.Y, p.Z,
		g and string.format("%.1f,%.1f", g.X, g.Z) or "-",
		tostring(props.plannedMove), tostring(run and run.name),
		okName and tostring(intrName) or "?", tostring(info.interestType),
		tostring(info.aiMode and info.aiMode.name))

	local okStage, stage = pcall(Ai2.Condor.StageScripts.GetBoyStageID)
	local okBoy, boy = pcall(function()
		local work = info.work
		if work and work.GetPlayerPos then return work.GetPlayerPos end
		return Ai2.Utils.GetPlayerAI().pos
	end)
	local okCam, cam = pcall(CameraPos)
	local face = info.ai2Ext and info.ai2Ext.dirXZ
	local okF, fx, fz, src = pcall(CoopTrico.CameraForward, p)
	local f = CoopTrico.forward
	CoopDebug.Print("TS st=%s inj=%s boy=%s cam=%s f=%s nav=%s goal=%s dir=%s",
		tostring(okStage and stage or "?"), tostring(props.isInjuredMovementMode),
		okBoy and Vec(boy) or "?", okCam and Vec(cam) or "?",
		okF and fx and string.format("%.2f,%.2f,%s", fx, fz, src) or "-",
		tostring(CoopTrico.nav), f and string.format("%.2f,%.2f,%s", f[1], f[2], f[3]) or "-",
		face and string.format("%.2f,%.2f", face.X, face.Z) or "-")
end

-- Call once per frame with the Trico player's input (nil when nobody plays Trico).
function CoopTrico.Update(dt, input)
	dt = dt or 0
	local props = Props()
	CoopTrico.reportIn = CoopTrico.reportIn - dt
	if CoopTrico.reportIn <= 0 then
		CoopTrico.reportIn = REPORT
		pcall(Report, props)
	end
	if not props or not props.currentInfo or not props.currentInfo.aiPos then return end

	local sx, sy, len = CoopTrico.Stick(input)
	-- a jump or a gesture (coop_actions.lua) has its own target: steering waits for it (the stick
	-- then only picks the side or the way)
	-- (a queued move only for its first YIELD seconds: after that the stick wins and the move
	-- waits for Trico to stop)
	local p = CoopActions and CoopActions.pending
	local yielding = p and (CoopActions.queueTime or 8) - (p.timer or 0) < YIELD
	if CoopActions and (CoopActions.jump or CoopActions.gesture or yielding) then sx = nil end
	CoopTrico.turnIn = math.max(0, (CoopTrico.turnIn or 0) - dt)
	if not sx then
		if CoopTrico.steering then Release(props) end
		if not CoopTrico.Stick(input) then CoopTrico.latch = nil end
		return
	end

	CoopTrico.refreshIn = CoopTrico.refreshIn - dt
	if CoopTrico.steering and CoopTrico.refreshIn > 0 then return end
	CoopTrico.refreshIn = REFRESH

	local info = props.currentInfo
	local pos = info.aiPos
	local dirXZ = info.ai2Ext and info.ai2Ext.dirXZ
	local fx, fz, src = CoopTrico.Frame(sx, sy, pos, dirXZ)
	CoopTrico.forward = { fx or 0, fz or 0, src or "trico" }
	local goal, dir, nav, fallback
	for _, scale in ipairs(NAV_STEPS) do
		local gx, gy, gz, dx, dz = CoopTrico.GoalFor(pos, dirXZ and dirXZ.X or 0,
			dirXZ and dirXZ.Z or 1, sx, sy, len, fx, fz, scale)
		if not gx then return end
		local candidate = Vector(gx, gy, gz)
		local ok, why, pathEnd = Reachable(props, pos, candidate)
		if ok then
			goal, dir, nav = candidate, Vector(dx, 0, dz), why .. string.format("%.2f", scale)
			break
		end
		if not fallback then
			-- nothing on the island yet: the full goal, or where the navmesh path towards it ends
			fallback = { candidate, Vector(dx, 0, dz), "n1.00" }
			if pathEnd and DistanceXZ(pos, pathEnd) >= MIN_PATH_GOAL then
				fallback = { Vector(pathEnd), Vector(dx, 0, dz), "e1.00" }
			end
		end
	end
	if not goal then goal, dir, nav = fallback[1], fallback[2], fallback[3] end
	local wasTurning = CoopTrico.turning
	CoopTrico.turning = false
	if TURNING[CoopTrico.strategy] and dirXZ then
		local wayX, wayZ = Flat(goal.X - pos.X, goal.Z - pos.Z)
		local turnGoal, turnDir = nil, nil
		if wayX then
			turnGoal, turnDir = CoopTrico.TurnGoal(pos, dirXZ.X, dirXZ.Z, wayX, wayZ, wasTurning)
		end
		if turnGoal and not Reachable(props, pos, turnGoal) then
			-- no room to walk round (a ledge, a beam): turn on the spot, and let the turn play
			if CoopTrico.turnIn <= 0 then
				CoopTrico.turnIn = TURN_GAP
				local name = CoopTrico.TurnCommand(dirXZ.X, dirXZ.Z, wayX, wayZ)
				pcall(Ai2.Utils.ReqCtrlCmd, info.ai2Ext, CoopTrico.commands[name])
				CoopTrico.nav = "spot-" .. name
				DropGoal(props)
				CoopTrico.steering = true
				CoopTrico.goal = nil
				CoopTrico.turning = true
			end
			return
		end
		if turnGoal then
			goal, dir, nav = turnGoal, turnDir, "turn"
			CoopTrico.turning = true
		end
	end
	-- keep the goal Trico is already walking to unless the stick asks for somewhere else (a
	-- turn goal follows Trico round, and the real goal replaces it at once)
	local old = CoopTrico.steering and CoopTrico.goal
	if old and not CoopTrico.turning and not wasTurning and DistanceXZ(old, goal) < REGOAL
		and DistanceXZ(pos, old) > ARRIVING then
		goal, dir = old, CoopTrico.goalDir or dir
	else
		CoopTrico.nav = nav
	end
	CoopTrico.steering = true
	CoopTrico.goal = goal
	CoopTrico.goalDir = dir
	local steer = Steer[CoopTrico.strategy] or Steer.goal
	local ok, err = pcall(steer, props, goal, dir)
	if not ok and err ~= CoopTrico.lastError then
		CoopTrico.lastError = err
		if CoopDebug then CoopDebug.Print("TR error %s", tostring(err)) end
	end
end

-- Give Trico back to its AI (when the player leaves the role).
function CoopTrico.Release()
	if CoopTrico.steering then Release(Props()) end
end

CoopTrico.Reset()
