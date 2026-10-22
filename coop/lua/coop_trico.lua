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
--   "direct":   no destination for his AI at all: every frame the stick is held, the free walk's
--               own path (the "freemove_intermediate" detail path) is set a few metres out in the
--               stick's way and the move manager is asked for a manual free move, the walk the
--               game's move code uses when it steers him itself (no stop decision, no interests).
--               The game still picks his gait, turns and footing.
--   "ctrl":     (shipped) the stick drives him directly. His own move planning is switched off
--               (approach.skipCreate, as the game does for scripted moments) and his own
--               forward move is set as his planned move, in the free walk's manual mode, towards
--               a pose 12-25 m out the stick's way, rebuilt every frame: his action layer walks
--               him there with his own gaits (set from the stick), footing and wall handling.
--               From a stand he turns on the spot (his move manager's turning slide) when the
--               stick is more than CTRL_WALK_ANGLE off his facing; walking, he curves round up
--               to CTRL_CURVE_ANGLE and is stopped first beyond it. Kept from stalling: his
--               destination is moved off where he stands, a forward request kicks him when he
--               stays standing, and with no progress for CTRL_UNSTICK his stuck requests are
--               dropped (and a standing Trico is woken with a moment of his planning). On
--               release: his walk stop and his planning back on.
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
-- Trico counts an interest as reached once he is within about 6.5 m of it (the developers'
-- auto-Trico places its guide goals 11-15 m away for that reason), so goals sit beyond that.
-- CoopTrico.distances: goal = metres ahead of Trico at full stick, min = at the edge of the
-- dead zone, turn = the side goal that starts a turn, behind = the least for a goal behind
-- Trico (so he turns round instead of shuffling under his own hindquarters).
local NAV_STEPS = { 1.0, 0.6, 0.35 }  -- shorter goals to try when the full one is off Trico's navmesh
local BEHIND = -0.3            -- cosine to Trico's facing below which a goal is behind it
local TURN_START = -0.17       -- cosine to Trico's facing (about 100 degrees) beyond which
                               -- "steer" starts the turn with a goal to the side
local TURN_DONE = 0.5          -- ... until Trico faces within 60 degrees of the stick's way
local TURNING = { steer = true }
-- The side goal that used to start a turn: off by default. In two-pad runs his AI lined up with
-- it after a turn on the spot (a slide back round) and stopped; the goal goes straight in the
-- stick's way instead, and his own approach turns him while walking.
if CoopTrico.sideTurns == nil then CoopTrico.sideTurns = false end
local REFRESH = 0.3            -- seconds between goal updates
local REGOAL = 2.0             -- metres the goal must change by before it is moved (so Trico's
                               -- path planner can commit to it)
local ARRIVING = 3.0           -- ... or how close Trico must get to it (distances.arrive, when set)
local MIN_PATH_GOAL = 2.0      -- shortest navmesh path end worth walking to
local REPORT = 0.5             -- seconds between debug reports

local FOCUS_TIME = 2.0         -- seconds after the Trico player's last input that Trico keeps
                               -- his mind off the boy
local BAN_TIME = 15.0          -- ... and keeps the idle-fidget bans below (a shake or a stretch
                               -- takes no commands for up to 7 s, so a pause between presses
                               -- must not let one start)
local FOCUS_REFRESH = 0.5
local FOCUS_LASTS = 2.5        -- each lowering of the boy's interest lapses by itself after this
local KEEP_ANGLE = 30.0        -- degrees the stick may wobble while the goal stays put
local RELATCH = 60.0           -- degrees the stick must swing before the camera is read again
local TURN_GAP = 2.0           -- seconds after a turn on the spot before another may be asked
                               -- for, so walking takes over from it
local YIELD = 1.5              -- seconds steering waits for a queued move (coop_actions.lua)

CoopTrico.strategy = CoopTrico.strategy or "goal"
CoopTrico.relative = CoopTrico.relative or "camera"
-- (arrive: the goal moves on once Trico is this close, beyond his 6.5 m "reached", so it leaps
-- ahead before he stops)
CoopTrico.distances = CoopTrico.distances or { goal = 14.0, min = 11.0, turn = 11.0, behind = 11.0, arrive = 8.0 }
-- (off: a forward request walks Trico straight along his facing, not to the goal, and repeated
-- while he walks it restarts him: start-stop stutter in the two-pad runs)
if CoopTrico.kick == nil then CoopTrico.kick = false end
if CoopTrico.keepLastPos == nil then CoopTrico.keepLastPos = true end   -- (the tests put Trico at 0,0,0)
CoopTrico.commands = {
	turn90Left = "ControlCommand_C_turn_90_left",
	turn90Right = "ControlCommand_C_turn_90_right",
	aboutLeft = "ControlCommand_C_turn_180_left",       -- 180 degrees on the spot (not the narrow
	                                                    -- turn: that one is for spots the level marks
	                                                    -- for it, and elsewhere it upsets his position)
	aboutRight = "ControlCommand_C_turn_180_right",
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

-- Trico's position: his entity's (work.GetMySelfPos, what the game's own AI scripts use), else
-- his AI's, else his character's. While some of his animations play (a change-of-direction
-- slide, c_slide_rot_*, and after a narrow turn) every one of these reads near 0,0,0 for a few
-- seconds, so a position that jumps more than JUMP metres from the last real one is not
-- believed (the last real one stands in), unless it stays there for JUMP_TIME (a real move,
-- such as a story scene placing him elsewhere).
local JUMP = 20.0
local JUMP_TIME = 3.0
local function Real(p) return p and (math.abs(p.X) > 0.01 or math.abs(p.Z) > 0.01) end
function CoopTrico.Position(props, dt)
	local info = props and props.currentInfo
	local p = info and info.aiPos
	local found
	local mine = info and info.work and info.work.GetMySelfPos
	if Real(mine) then found = Vector(mine)
	elseif Real(p) then found = p
	else
		local ok, w = pcall(function() return CharacterMngr:GetTrico().worldPosition end)
		if ok and Real(w) then found = Vector(w) end
	end
	local last = CoopTrico.keepLastPos and CoopTrico.lastPos
	if last and last.props ~= props then last = nil end
	if not found then return last and last.pos or p end
	if last then
		local dx, dz = found.X - last.pos.X, found.Z - last.pos.Z
		if dx * dx + dz * dz > JUMP * JUMP then
			last.away = (last.away or 0) + (dt or 0)
			if last.away < JUMP_TIME then return last.pos end
		end
	end
	CoopTrico.lastPos = { props = props, pos = found, away = 0 }
	return found
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
-- goal behind Trico (its facing is tricoDirX, tricoDirZ) stays distances.behind away.
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
	local d = CoopTrico.distances
	local dist = (d.min + (d.goal - d.min) * len) * (scale or 1)
	if dx * faceX + dz * faceZ < BEHIND and dist < d.behind then dist = d.behind end
	return tricoPos.X + dx * dist, tricoPos.Y, tricoPos.Z + dz * dist, dx, dz
end

-- Is `to` on Trico's navmesh island (no special edge on the way)? Unknown counts as yes. Also
-- returns where the straight navmesh path towards `to` ends.
local function Reachable(props, from, to)
	local ok, same, pathEnd = pcall(Ai2.Utils.Navmesh.CheckSameIsland, props, from, to, true)
	if not ok then return true, "?" end
	return same, same and "y" or "n", pathEnd
end

-- The nearest navmesh point to `to` (within SNAP_BOX), or nil.
local SNAP_BOX = Vector and Vector(8, 12, 8) or nil
local SNAP_MIN = 0.6           -- of distances.min: a snapped goal nearer is inside Trico's
                               -- arrival range (6.6 m with the default 11 m)
local function Snap(props, to)
	local ok, near = pcall(Ai2FindNearestPosition, props.currentInfo.ai2Ext, to, SNAP_BOX or Vector(8, 12, 8))
	if ok and near and near.found == 1 and near.pos then return Vector(near.pos) end
	return nil
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

-- "direct": the free walk's path to a pose ahead, and a manual free move (see the top).
-- The free walk's own slow-down and stop planning compares his stopping distance (about 7 m
-- from a walk) with what is left to the pose: a pose a few metres out made him stop at once. So
-- the pose stays well beyond it, moving on with him every frame; nearer means a slower gait.
local DIRECT_NEAR = 12.0       -- metres ahead at the edge of the dead zone
local DIRECT_FAR = 25.0        -- ... and at full stick
-- How far along (dirX, dirZ) from pos the ground stays walkable, up to `dist` metres: in REACH_STEP
-- steps, each must have his navmesh right under it (within REACH_BOX) and no more than
-- REACH_DROP up or down from the last. The manual free walk follows its pose without asking for
-- ground ("he walked on air off the cliff"), so the pose may never lie past this. A step that
-- misses is tried again REACH_SIDE either side of the line, so a narrow beam a little off it is
-- not taken for an edge. (Chaining each step from the ground point found before read 1 m of
-- ground everywhere in game: the found points are no place to step on from.)
local REACH_STEP = 1.0
local REACH_BOX = Vector(0.6, 2.5, 0.6)
local REACH_DROP = 1.5
local REACH_EVERY = 0.2        -- seconds between measures (kept while the way stays within 5 degrees)
local EDGE_MARGIN = 1.5        -- metres short of the edge the pose stops (CoopTrico.edgeMargin)
local REACH_SIDE = 0.4         -- metres either side of the line a missed step is tried again
local BACK_SLACK = 0.5         -- metres the built path may fall short of him before it counts
local function EdgeMargin() return tonumber(CoopTrico.edgeMargin) or EDGE_MARGIN end
local function GroundNear(ai, x, y, z)
	local probe = Vector(x, y, z)
	local ok, near = pcall(Ai2FindNearestPosition, ai, probe, REACH_BOX)
	-- (debug: what the last miss saw, for the TE report)
	if not ok or type(near) ~= "table" or near.found ~= 1 or not near.pos then
		CoopTrico.reachMiss = string.format("%.1f,%.1f,%.1f:%s", x, y, z, ok and type(near) == "table" and tostring(near.found) or "err")
		return nil
	end
	local dx, dz = near.pos.X - x, near.pos.Z - z
	if dx * dx + dz * dz > REACH_BOX.X * REACH_BOX.X * 2 then
		CoopTrico.reachMiss = string.format("%.1f,%.1f,%.1f:off%.1f,%.1f,%.1f", x, y, z, near.pos.X, near.pos.Y, near.pos.Z)
		return nil
	end
	return near.pos.Y
end

function CoopTrico.WalkableReach(props, pos, dirX, dirZ, dist)
	local ai = props.currentInfo.ai2Ext
	local lastY = pos.Y
	local d = 0
	while d + REACH_STEP <= dist do
		local x, z = pos.X + dirX * (d + REACH_STEP), pos.Z + dirZ * (d + REACH_STEP)
		local y = GroundNear(ai, x, lastY, z)
		if not y then y = GroundNear(ai, x - dirZ * REACH_SIDE, lastY, z + dirX * REACH_SIDE) end
		if not y then y = GroundNear(ai, x + dirZ * REACH_SIDE, lastY, z - dirX * REACH_SIDE) end
		if y and math.abs(y - lastY) > REACH_DROP then
			CoopTrico.reachMiss = string.format("%.1f,%.1f,%.1f:dy%.1f", x, lastY, z, y - lastY)
		end
		if not y or math.abs(y - lastY) > REACH_DROP then return d end
		lastY = y
		d = d + REACH_STEP
	end
	return dist
end

local function Reach(props, pos, dirX, dirZ, dist, dt)
	if CoopTrico.noReach then return dist end   -- (debug: the limit off, to show the old walk on air)
	local c = CoopTrico.reachCache
	CoopTrico.reachIn = (CoopTrico.reachIn or 0) - (dt or 0)
	if c and CoopTrico.reachIn > 0 and c.dirX * dirX + c.dirZ * dirZ > 0.996 and c.dist >= dist then
		return math.max(0, c.reach - DistanceXZ(c.pos, pos))
	end
	local r = CoopTrico.WalkableReach(props, pos, dirX, dirZ, dist)
	CoopTrico.reachCache = { pos = Vector(pos), dirX = dirX, dirZ = dirZ, dist = dist, reach = r }
	CoopTrico.reachIn = REACH_EVERY
	return r
end

-- The free walk's intermediate path (its "pose") DIRECT_NEAR..DIRECT_FAR ahead, built in place,
-- never past the walkable ground (the edge margin short of an edge; nearer than that, onto him).
-- Two checks, each pulling the pose in: the walkable reach (above), and the built path must
-- reach back from the pose to him (its BackRest: shorter, and a wall or the mesh's end lies
-- between, that far from the pose), rebuilt once nearer. The navmesh fit gives the pose its height.
local function BuildPose(ai, info, pos, dirX, dirZ, dist)
	local target = Vector(pos.X + dirX * dist, pos.Y, pos.Z + dirZ * dist)
	local fit = nil
	if Ai2FitNavmesh then
		local okF, f = pcall(Ai2FitNavmesh, ai.navMesh, target, Ai2.Approach.upperLimit, Ai2.Approach.pathLowerLimit)
		fit = okF and f or nil
	end
	local dir = Vector(dirX, 0, dirZ)
	local dp = Ai2.CondorUtils.GetPath(ai, Ai2.Actions.Condor.Path.Pack.FreeMoveIntermediate)
	local base = info.detailPath
	dp.Navmesh = base.Navmesh
	dp.Interval = base.Interval
	-- long both ways from the pose, so the path runs back through Trico (his slow-down planning
	-- measures what is left along it: with the usual 4 m it was always nearly used up)
	dp.FrontLen = CoopTrico.directFront or 30.0
	dp.BackLen = CoopTrico.directBack or 30.0
	dp.WallSensorRadiusMul = base.WallSensorRadiusMul
	dp.BasePos = target
	dp.BaseDir = dir
	dp.FrontGoal = base.FrontGoal
	dp.BackGoal = base.BackGoal
	dp.FrontGoalTransitOffset = base.FrontGoalTransitOffset
	dp.BackGoalTransitOffset = base.BackGoalTransitOffset
	dp.TurnRadius = base.TurnRadius
	return target, fit, dp, dir
end

function CoopTrico.BuildAhead(props, pos, dirX, dirZ, len, dt)
	local info = props.currentInfo
	local ai = info.ai2Ext
	local margin = EdgeMargin()
	local dist = DIRECT_NEAR + (DIRECT_FAR - DIRECT_NEAR) * (len or 1)
	CoopTrico.reachFrom = string.format("%.1f,%.1f,%.1f>%.2f,%.2f", pos.X, pos.Y, pos.Z, dirX, dirZ)
	local reach = Reach(props, pos, dirX, dirZ, dist, dt)
	CoopTrico.blocked = nil
	CoopTrico.edgeWhy = nil
	if reach < dist then
		CoopTrico.blocked, CoopTrico.edgeWhy = reach, "reach"
		dist = math.max(0, reach - margin)
	end
	-- (his own path follower calls the way unsafe: onto him, see CoopTrico.Unsafe)
	if CoopTrico.unsafeNow then
		CoopTrico.blocked, CoopTrico.edgeWhy = 0, "unsafe"
		dist = 0
	end
	CoopTrico.reach = reach
	local target, fit, dp, dir = BuildPose(ai, info, pos, dirX, dirZ, dist)
	-- (the fit only gives the pose his ground's height: as a test of the pose it failed on open
	-- ground in game, coop-h-pace6, and stopped him every few metres; the reach is the test)
	CoopTrico.fitTries = (type(fit) == "table" and fit.found == 1) and 0 or 1
	if type(fit) == "table" and fit.found == 1 and fit.pos then target = Vector(target.X, fit.pos.Y, target.Z) end
	dp.BasePos = target
	Ai2BuildPath(ai, dp, 0)
	-- the game's own verdict: the path from the pose back to him stops short (a wall, the mesh's
	-- end): the obstacle lies BackRest from the pose; once, the pose in short of it
	local back = tonumber(dp.BackRest)
	CoopTrico.backRest = back
	if back and dist > 0 and back + BACK_SLACK < DistanceXZ(target, pos) then
		local obstacle = math.max(0, dist - back)
		CoopTrico.blocked, CoopTrico.edgeWhy = obstacle, "path"
		dist = math.max(0, obstacle - margin)
		target, fit, dp, dir = BuildPose(ai, info, pos, dirX, dirZ, dist)
		if type(fit) == "table" and fit.found == 1 and fit.pos then target = Vector(target.X, fit.pos.Y, target.Z) end
		dp.BasePos = target
		Ai2BuildPath(ai, dp, 0)
	end
	info.apDestination = target
	info.apDestinationDir = dir
	CoopTrico.goal = target
	return target
end

function CoopTrico.DirectMove(props, pos, dirX, dirZ, len)
	local target = CoopTrico.BuildAhead(props, pos, dirX, dirZ, len)
	-- every frame: a request runs once, and with none pending his own cost calculation picks the
	-- next frame's move (a stop, with no destination of his own)
	-- forward, along neither his behaviour path nor a line path (a line-path flag left over from
	-- his own last move makes the action layer stop a free move once he walks: he set off and
	-- stopped at once)
	Ai2.CondorUtils.Path.ReqDiscretePathMode(props, false, false, false, false)
	props.moveMngr:RequestForward({ isFreeMove = true, manualMode = true, alongLinePath = false })
	CoopTrico.directRequests = (CoopTrico.directRequests or 0) + 1
	return target
end

-- "ctrl" walking: the pose ahead, and his own forward move set as the planned move (his planning
-- is off, so nothing replaces it), in the free walk's manual mode: the action layer's MoveForward
-- then walks him towards the pose with his own gaits, the rest to walk being the way to the pose
-- (with no path at all, the walk animation played on the spot).
-- His gaits from how far the stick is pushed (his planning, now off, would set them from his
-- interest; left alone they stay at his last idle walk-slow), and his own stop decision
-- suspended while the stick is held, as the game's scripted chases do (put back on release):
-- with it on he stopped half way through a hold and set off again slowly.
CoopTrico.manualMove = { isFreeMove = true, manualMode = true, alongLinePath = false }
local CTRL_FAST_PUSH = 0.9     -- stick length beyond which his quick acceleration is used
-- (a half push is his slow walk, about 1 m/s; his ordinary walk is about 2 m/s, coop-h-pace10)
local CTRL_TYPES = {
	{ 0.65, { "MV_C_WALKSTOP", "MV_C_WALKSLOW" } },
	{ 0.9, { "MV_C_WALKSTOP", "MV_C_WALKSLOW", "MV_C_WALK" } },
	{ 2.0, { "MV_C_WALKSTOP", "MV_C_WALKSLOW", "MV_C_WALK", "MV_C_WALKFAST" } },
}
-- ... and capped by the room to an edge (the walkable reach): with a fast walk's stopping distance
-- (about 7 m) more than the shortened pose left him, he stopped, set off fast again and stopped,
-- over and over (coop-h-pace10). Under FAST_ROOM metres no fast walk, under WALK_ROOM slow only.
local FAST_ROOM = 12.0
local WALK_ROOM = 7.0
function CoopTrico.CtrlTypes(len, room)
	local level = #CTRL_TYPES
	for i, g in ipairs(CTRL_TYPES) do
		if (len or 1) <= g[1] then level = i break end
	end
	if room and room < WALK_ROOM then level = 1
	elseif room and room < FAST_ROOM then level = math.min(level, 2) end
	return CTRL_TYPES[level][2]
end

function CoopTrico.ManualMove(props, pos, dirX, dirZ, len, dt)
	local target = CoopTrico.BuildAhead(props, pos, dirX, dirZ, len, dt)
	Ai2.CondorUtils.Path.ReqDiscretePathMode(props, false, false, false, false)
	-- (his gaits and acceleration: see the hooks above)
	local walk = props.moveMngr and props.moveMngr.walk
	local sk = CoopTrico.skipping
	if walk and sk and sk.stopDecision == nil then
		sk.stopDecision = walk.suspendStopDecision or false
		sk.walk = walk
	end
	if walk then walk.suspendStopDecision = true end
	props.isOnGoalInVolume = false
	props.plannedMove = "ForwardMove"
	props.moveParam = CoopTrico.manualMove
	return target
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
	local t = CoopTrico.distances.turn
	return Vector(pos.X + lx * t, pos.Y, pos.Z + lz * t), Vector(lx, 0, lz)
end

-- Turn Trico on the spot to face the way (wayX, wayZ): his move manager's own change-of-direction
-- slide to where he stands, facing that way (from a stand; from a walk it stops him first). The
-- raw turn commands (C_turn_90/180/narrow) must not be sent from here: they align to a target
-- pose his turn behaviour normally sets up, and without one they took him to the world origin
-- ("he vanished into a glitched area while turning").
function CoopTrico.TurnTo(props, wayX, wayZ)
	local pos = CoopTrico.Position(props) or props.currentInfo.aiPos
	props.moveMngr:RequestSlide({ destination = Vector(pos), destinationDir = Vector(wayX, 0, wayZ) })
	CoopTrico.turns = (CoopTrico.turns or 0) + 1
	return string.format("slide to face %.2f,%.2f", wayX, wayZ)
end

-- A quicker turn for 60 degrees and more: his jump turn (a hop round, slide_jump_turn_90/180),
-- which the game switches on for every stage (SetCondorJumpSlideFlag, "QuickJumpSlide") and his
-- own planning picks when it hurries. The slide request doesn't pass the jump-turn switch on, so
-- with his planning off the planned slide is set the way the request sets it, plus the switch.
-- False when the game's switch is off (then the plain turning slide).
function CoopTrico.JumpTurnTo(props, wayX, wayZ)
	local info = props.currentInfo
	local ai = info.ai2Ext
	local okF, flags = pcall(function() return tostring(ai.debugFlags) end)
	if not okF or not string.find(flags, "QuickJumpSlide", 1, true) then return false end
	local pos = CoopTrico.Position(props) or info.aiPos
	local mp = { goalPos = Vector(pos), goalDir = Vector(wayX, 0, wayZ), useJumpTurn = true }
	mp.frontGoal = Ai2.CondorUtils.GetFrontGoal(props, mp.goalPos, mp.goalDir)
	mp.backGoal = Ai2.CondorUtils.GetBackGoal(ai, mp.goalPos, mp.goalDir)
	props.plannedMove = "ChangeDirectionSlide"
	props.moveParam = mp
	CoopTrico.jumpTurns = (CoopTrico.jumpTurns or 0) + 1
	return true
end

-- The turn-on-the-spot command that turns Trico from facing (faceX, faceZ) towards the way
-- (wayX, wayZ): an about-turn beyond 120 degrees, else a quarter turn, on the way's side (the
-- right of a facing (x, z) is (-z, x)).
function CoopTrico.TurnCommand(faceX, faceZ, wayX, wayZ)
	local right = (-faceZ) * wayX + faceX * wayZ >= 0
	-- beyond 135 degrees an about-turn, else a quarter turn: either leaves at most 45 degrees
	if wayX * faceX + wayZ * faceZ < -0.7 then return right and "aboutRight" or "aboutLeft" end
	return right and "turn90Right" or "turn90Left"
end

-- The frame the stick is read in, as a forward (x, z) and its source: Trico's facing
-- ("trico" steering), else the camera's, latched while the stick is held.
function CoopTrico.Frame(sx, sy, pos, dirXZ)
	if CoopTrico.relative == "trico" and dirXZ then return dirXZ.X, dirXZ.Z, "trico" end
	-- (debug, for measured runs: "up" on the stick is a fixed world way, CoopTrico.worldForward)
	if CoopTrico.relative == "world" and type(CoopTrico.worldForward) == "table" then
		local w = CoopTrico.worldForward
		local len = math.sqrt(w[1] * w[1] + w[2] * w[2])
		if len > 0 then return w[1] / len, w[2] / len, "world" end
	end
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
	pcall(CoopTrico.Unskip, props)
	CoopTrico.stoodUp = nil
	CoopTrico.steering = false
	CoopTrico.goal = nil
	CoopTrico.turning = false
	CoopTrico.latch = nil
	CoopTrico.spunFor = nil
	CoopTrico.goalStick = nil
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
	local p = CoopTrico.Position(props) or info.aiPos
	-- (whether he stands on his navmesh: how far the nearest point is, across and up or down)
	if Ai2FindNearestPosition then
		local okG, ground = pcall(Ai2FindNearestPosition, info.ai2Ext, p, Vector(2, 6, 2))
		if okG and type(ground) == "table" and ground.found == 1 and ground.pos then
			CoopDebug.Print("TG on xz=%.2f dy=%.2f", DistanceXZ(ground.pos, p), p.Y - ground.pos.Y)
		else
			CoopDebug.Print("TG off")
		end
	end
	local g = CoopTrico.goal
	local intr = info.interestEntity
	local okName, intrName = pcall(function() return intr and intr:GetName() end)
	local run = props.moveMngr and props.moveMngr.runningRequest
	local okAnim, anim = pcall(Ai2.Utils.GetCurAnimState, info.ai2Ext)
	CoopDebug.Print("TR %s %s p=%.1f,%.1f,%.1f g=%s pm=%s rq=%s in=%s it=%s md=%s an=%s",
		CoopTrico.strategy, CoopTrico.steering and "on" or "off", p.X, p.Y, p.Z,
		g and string.format("%.1f,%.1f", g.X, g.Z) or "-",
		tostring(props.plannedMove), tostring(run and run.name),
		okName and tostring(intrName) or "?", tostring(info.interestType),
		tostring(info.aiMode and info.aiMode.name), okAnim and tostring(anim) or "?")
	if CoopTrico.steering and CoopTrico.strategy == "ctrl" then
		local pg = info.pathGen
		CoopDebug.Print("TE reach=%s fit=%s back=%s edge=%s why=%s sf=%s ut=%s stops=%s from=%s miss=%s",
			CoopTrico.reach and string.format("%.1f", CoopTrico.reach) or "-", tostring(CoopTrico.fitTries),
			CoopTrico.backRest and string.format("%.1f", CoopTrico.backRest) or "-",
			CoopTrico.blocked and string.format("%.1f", CoopTrico.blocked) or "-", tostring(CoopTrico.edgeWhy),
			tostring(type(pg) == "table" and pg.isSafeFollowPath), tostring(props.moveMngr and props.moveMngr.unsafeTrunkPath),
			tostring(CoopTrico.hookStops), tostring(CoopTrico.reachFrom), tostring(CoopTrico.reachMiss))
	end

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
	-- the other facings, to find one that stays true through turns
	local ad = info.aiDir
	local okR, rf = pcall(function() return Vector(0, 0, 1):Rotate(info.work.GetMySelfRot) end)
	CoopDebug.Print("TF aiDir=%s rot+z=%s an=%s", ad and string.format("%.2f,%.2f", ad.X, ad.Z) or "-",
		okR and rf and string.format("%.2f,%.2f", rf.X, rf.Z) or "-",
		tostring(select(2, pcall(Ai2.Utils.GetCurAnimState, info.ai2Ext))))
end

-- While the Trico player is giving input (stick or a button, within FOCUS_TIME), the boy's
-- interest for Trico is lowered (subMax, as the game's own scripts lower interests for a while),
-- so Trico doesn't wander over to sit by him, nuzzle him or follow him instead of the player.
-- Each lowering lapses by itself after FOCUS_LASTS, so nothing needs undoing and a story script's
-- own exclusion of the boy is never touched.
local function Active(input)
	if not input then return false end
	if CoopTrico.Stick(input) then return true end
	if math.abs(input.rx or 0) > DEADZONE or math.abs(input.ry or 0) > DEADZONE then return true end
	for _, held in pairs(input.hold or {}) do if held then return true end end
	return false
end

-- Also while focused: the game's own bans on Trico's idle actions (scratching, stretching, sitting
-- down by himself) and on his boy-affection behaviours, and its "prefer walking" switch (no jump
-- slides). Each is only set if it was off, and only those are cleared again afterwards, so a story
-- script's own bans are never touched.
local BANS = { "IdlingBan", "SitIdlingBan", "BoyDaisukiProcBan" }

function CoopTrico.Unban()
	local b = CoopTrico.banned
	if not b then return end
	CoopTrico.banned = nil
	local work = b.props.currentInfo and b.props.currentInfo.work
	for flag in pairs(b.set) do
		if flag == "prioSelect" then
			if b.props.moveMngr then b.props.moveMngr.prioSelect = false end
		elseif work and work[flag] == true then
			work[flag] = false
		end
	end
end

local function Ban(props)
	if CoopTrico.banned and CoopTrico.banned.props == props then return end
	CoopTrico.Unban()   -- (bans recorded for another Trico, say after his AI was made again)
	local work = props.currentInfo.work
	local set = {}
	if work then
		for _, flag in ipairs(BANS) do
			if work[flag] == false then work[flag] = true set[flag] = true end
		end
	end
	if props.moveMngr and not props.moveMngr.prioSelect then
		props.moveMngr.prioSelect = true
		set.prioSelect = true
	end
	CoopTrico.banned = { props = props, set = set }
end


function CoopTrico.Focus(dt, input, props)
	if Active(input) then CoopTrico.idleFor = 0 else CoopTrico.idleFor = (CoopTrico.idleFor or 99) + dt end
	CoopTrico.focusIn = (CoopTrico.focusIn or 0) - dt
	CoopTrico.focused = CoopTrico.idleFor < FOCUS_TIME
	if CoopTrico.idleFor < BAN_TIME then
		Ban(props)
		-- the bans keep new idle actions away, not one already set out (a head shake that takes
		-- no commands for 7 s, picked up as soon as his planning runs again): that one goes the
		-- game's own way
		local work = props.currentInfo.work
		if work and work.IdlingIntrPoped and Ai2.CommonScript and Ai2.CommonScript.Condor then
			pcall(Ai2.CommonScript.Condor.DelIdlingIntr, props)
			CoopTrico.idlesDropped = (CoopTrico.idlesDropped or 0) + 1
		end
	else
		CoopTrico.Unban()
	end
	if not CoopTrico.focused or CoopTrico.focusIn > 0 then return end
	CoopTrico.focusIn = FOCUS_REFRESH
	local ai = props.currentInfo.ai2Ext
	local boy = FindObject and FindObject("Player")
	if ai and boy then
		pcall(Ai2.Utils.InterestLevelAddition, ai, boy, "CoopTrico_Focus", "subMax", FOCUS_LASTS)
		CoopTrico.focusCount = (CoopTrico.focusCount or 0) + 1
	end
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
	if CoopTrico.logAI and CoopDebug and CoopDebug.enabled then pcall(CoopTrico.HookAILog, props, dt) end
	pcall(CoopTrico.Focus, dt, input, props)

	local sx, sy, len = CoopTrico.Stick(input)
	-- a jump or a gesture (coop_actions.lua) has its own target: steering waits for it (the stick
	-- then only picks the side or the way)
	-- (a queued move only for its first YIELD seconds: after that the stick wins and the move
	-- waits for Trico to stop)
	local p = CoopActions and CoopActions.pending
	local yielding = p and (CoopActions.queueTime or 8) - (p.timer or 0) < YIELD
	if CoopActions and (CoopActions.jump or CoopActions.gesture) then sx = nil end
	CoopTrico.turnIn = math.max(0, (CoopTrico.turnIn or 0) - dt)
	if sx and yielding then
		-- step back for the queued move without asking Trico to stop (that could cut off a
		-- turn he has just started)
		if CoopTrico.steering then
			CoopTrico.steering = false
			CoopTrico.goal = nil
			DropGoal(props)
		end
		return
	end
	if not sx then
		if CoopTrico.steering then Release(props) end
		if not CoopTrico.Stick(input) then CoopTrico.latch = nil end
		return
	end
	if CoopTrico.strategy == "direct" then return CoopTrico.Direct(dt, props, sx, sy, len) end
	if CoopTrico.strategy == "ctrl" then return CoopTrico.Ctrl(dt, props, sx, sy, len) end

	CoopTrico.refreshIn = CoopTrico.refreshIn - dt
	if CoopTrico.steering and CoopTrico.refreshIn > 0 then return end
	CoopTrico.refreshIn = REFRESH

	local info = props.currentInfo
	local pos = CoopTrico.Position(props, dt)
	if not pos then return end
	-- sitting: stand up first (the game's own end of sitting), then he can walk
	local okA, anim = pcall(Ai2.Utils.GetCurAnimState, info.ai2Ext)
	anim = okA and tostring(anim) or ""
	CoopTrico.standIn = math.max(0, (CoopTrico.standIn or 0) - REFRESH)
	if CoopTrico.StandUp(props, anim) then
		CoopTrico.steering = true
		return
	end
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
		-- off the navmesh (a ledge, a beam): the nearest walkable point, if it's on Trico's
		-- island and still far enough to walk to
		local snapped = Snap(props, candidate)
		if snapped and DistanceXZ(pos, snapped) >= SNAP_MIN * CoopTrico.distances.min
			and Reachable(props, pos, snapped) then
			local sx2, sz2 = Flat(snapped.X - pos.X, snapped.Z - pos.Z)
			goal, dir, nav = snapped, Vector(sx2 or dx, 0, sz2 or dz), "s" .. string.format("%.2f", scale)
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
		if wayX and CoopTrico.sideTurns then
			turnGoal, turnDir = CoopTrico.TurnGoal(pos, dirXZ.X, dirXZ.Z, wayX, wayZ, wasTurning)
		end
		-- a turn on the spot is a fixed 90 or 180 degrees, so it is only asked for while the
		-- way is still more than TURN_START round (fresh each time, not the walk's TURN_DONE): the
		-- rest of the turn is walked, or Trico would turn back and forth for ever
		local far = wayX and (wayX * dirXZ.X + wayZ * dirXZ.Z) < TURN_START
		-- one spot turn per stick direction: Trico's facing reads stale for a moment after a
		-- turn, which would otherwise start another one (the stick must swing RELATCH degrees,
		-- or be let go, before the next)
		local stickAngle = math.deg(math.atan2(sx, sy))
		local spun = CoopTrico.spunFor and math.abs((stickAngle - CoopTrico.spunFor + 180) % 360 - 180) <= RELATCH
		local standing = props.plannedMove == nil or props.plannedMove == "MoveStop"
		if far and not spun and (standing or (turnGoal and not Reachable(props, pos, turnGoal))) then
			-- standing: turn on the spot, and let the turn play (a walking Trico is turned by his own
			-- approach towards the goal); never a second turn while one still plays, or it would
			-- turn him back
			local okAnim, anim = pcall(Ai2.Utils.GetCurAnimState, info.ai2Ext)
			local spinning = okAnim and (string.find(tostring(anim), "turn", 1, true) or string.find(tostring(anim), "slide_rot", 1, true)) ~= nil
			if spinning then CoopTrico.turnIn = math.max(CoopTrico.turnIn, 0.5) end
			if CoopTrico.turnIn <= 0 then
				CoopTrico.turnIn = TURN_GAP
				local name = CoopTrico.TurnCommand(dirXZ.X, dirXZ.Z, wayX, wayZ)
				CoopTrico.spunFor = stickAngle
				pcall(CoopTrico.TurnTo, props, wayX, wayZ)
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
	-- (also while the stick stays within KEEP_ANGLE of where the goal was set: a thumb wobbles,
	-- and the snapped goal then flips between spots a few metres apart, each change replanning
	-- Trico's route, so he walked a step and stopped)
	local old = CoopTrico.steering and CoopTrico.goal
	local stickNow = math.deg(math.atan2(sx, sy))
	local sameWay = CoopTrico.goalStick and math.abs((stickNow - CoopTrico.goalStick + 180) % 360 - 180) <= KEEP_ANGLE
	if old and not CoopTrico.turning and not wasTurning and (DistanceXZ(old, goal) < REGOAL or sameWay)
		and DistanceXZ(pos, old) > (CoopTrico.distances.arrive or ARRIVING) then
		goal, dir = old, CoopTrico.goalDir or dir
	else
		CoopTrico.nav = nav
		CoopTrico.goalStick = stickNow
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

-- Seated or lying, the stick first stands him up (every 2 s at most): from sitting with sit_end
-- (as the game's own scripts do), from lying with his stand command. True while that is so.
function CoopTrico.StandUp(props, anim)
	local seated = (string.find(anim, "_sit", 1, true) or string.find(anim, "_lie", 1, true))
		and not string.find(anim, "_end", 1, true)
	if not seated then return false end
	if (CoopTrico.standIn or 0) <= 0 then
		CoopTrico.standIn = 2.0
		local cmd = string.find(anim, "_sit", 1, true) and "ControlCommand_C_sit_end"
			or props.standCommand or "ControlCommand_C_stand"
		pcall(Ai2.Utils.ReqCtrlCmd, props.currentInfo.ai2Ext, cmd)
	end
	CoopTrico.stoodUp = true   -- (ctrl wakes him as soon as he stands: see CoopTrico.Ctrl)
	CoopTrico.nav = "stand-up"
	return true
end

-- Hooks into his free walk's own decisions, for "ctrl" (installed once, active only while the
-- stick steers him in manual mode; his own free walks run the originals). Writing his props from
-- here lost to his AI tick: his movement mode rebuilds curMoveTypes from his interest every tick
-- (low with the boy's interest lowered: walk-slow only) and sets his acceleration low.
--   FreeMove.Main: the stick's gaits as the decision's own useMoveTypes, his acceleration high at
--                  a full push (props.walk, which the decision reads).
--   FreeMove.StopDecision: in manual mode the game counts the pose as arrived whenever he has
--                  nearly reached his predicted step (its destination is the pose itself), so he
--                  stopped and set off again over and over; here he stops only once his own
--                  stopping distance reaches what is left to the pose (an edge, a wall, release).
CoopTrico.hooks = CoopTrico.hooks or {}
local JUMP_APPROACH_TYPES = { "MV_C_WALKSTOP", "MV_C_WALKSLOW", "MV_C_WALK" }
local function SteeringManual(t)
	return CoopTrico.steering and CoopTrico.strategy == "ctrl" and type(t) == "table" and t.manualMode
		and not t.forcedStop
end

function CoopTrico.InstallHooks()
	local fm = Ai2 and Ai2.Actions and Ai2.Actions.Condor and Ai2.Actions.Condor.FreeMove
	if type(fm) ~= "table" then return false end
	local h = CoopTrico.hooks
	if type(fm.Main) == "function" and fm.Main ~= h.main then
		local orig = fm.Main
		h.main = function(self, t, ...)
			if SteeringManual(t) then
				local len = CoopTrico.stickLen or 1
				local types = CoopTrico.CtrlTypes(len, CoopTrico.blocked)
				-- (his current gait stays allowed when only the edge cap left it out: one left out
				-- sets the decision's slowDown, which the game's stop decision takes as a stop, so
				-- the cap only keeps him from shifting up, and his stop by distance brakes him at
				-- the edge. The stick asking for a slower gait is a slow-down the game makes with a
				-- stop and a slower start: manual mode never shifts down by itself.)
				local cur = t.curType
				if cur and cur ~= "MV_C_WALKSTOP" then
					local listed, wanted = false, false
					for _, m in ipairs(types) do if m == cur then listed = true end end
					for _, m in ipairs(CoopTrico.CtrlTypes(len)) do if m == cur then wanted = true end end
					if wanted and not listed then
						local copy = {}
						for i, m in ipairs(types) do copy[i] = m end
						copy[#copy + 1] = cur
						types = copy
					end
				end
				t.useMoveTypes = types
				t.coopMoveTypes = true
				local walk = t.props and t.props.walk
				if walk then
					walk.accelLevel = (len > CTRL_FAST_PUSH and types[#types] == "MV_C_WALKFAST") and "high" or "low"
					walk.stopAccelLevel = "low"
				end
			elseif type(t) == "table" and not t.forcedStop and CoopActions and CoopActions.jump
				and not CoopTrico.steering then
				-- a Cross jump held: his own approach to the take-off point in his ordinary walk
				-- (his movement mode, with the boy's interest lowered, allowed only the slow walk:
				-- 9 s for 4.5 m, coop-h-jump4)
				t.useMoveTypes = JUMP_APPROACH_TYPES
				t.coopMoveTypes = true
			elseif type(t) == "table" and t.coopMoveTypes then
				-- (his action's own state outlives the hold: our gaits go with it)
				t.useMoveTypes, t.coopMoveTypes = nil, nil
			end
			return orig(self, t, ...)
		end
		fm.Main = h.main
	end
	if type(fm.StopDecision) == "function" and fm.StopDecision ~= h.stop then
		local orig = fm.StopDecision
		h.stop = function(self, t, parentReport, ...)
			if SteeringManual(t) and not t.requestStop and not t.slowDown then
				local okD, toStop, anims = pcall(self.GetDistanceToStop, self, t, t.curAnimStName, true)
				if okD and (t.progressed or 0) + (toStop or 0) >= (t.rest or 0) - 0.3 then
					local props = t.props
					t.ctrlCmd = props.ctrlCmds and props.ctrlCmds.MV_C_WALKSTOP or props.stopCommand
					t.transAnims = anims or {}
					t.transGears = {}
					for i = 1, #t.transAnims do t.transGears[i] = self.stopGearName end
					CoopTrico.hookStops = (CoopTrico.hookStops or 0) + 1
					return true
				end
				return false
			end
			return orig(self, t, parentReport, ...)
		end
		fm.StopDecision = h.stop
	end
	return true
end

-- (Off unless CoopTrico.useUnsafe: in game, coop-h-pace8, unsafeTrunkPath fired 12 m short of
-- any edge and stopped him for 2 s; it reads his own detail path, which is stale while his
-- planning is off. The walkable reach stops him at real edges.)
-- His own path follower's alarms while he walks (the game stops his own walks on them): the
-- followed path is unsafe (isSafeFollowPath 0), or his trunk path runs into a wall right ahead
-- (unsafeTrunkPath). Only while a walk plays, as the game's own check: standing, they may be
-- left over from the last walk, and he would never set off again.
function CoopTrico.Unsafe(props, info, anim)
	if not CoopTrico.useUnsafe then return false end   -- (off: see above)
	if not string.find(anim, "walk", 1, true) or string.find(anim, "stand_walk", 1, true) then return false end
	local pg = info.pathGen
	local unsafe = (type(pg) == "table" and pg.isSafeFollowPath == 0)
		or (props.moveMngr and props.moveMngr.unsafeTrunkPath == true)
	if unsafe then CoopTrico.unsafeCount = (CoopTrico.unsafeCount or 0) + 1 end
	return unsafe and true or false
end

-- A stage interest's action of his (the canyon's wing stretch) holding him while the stick is
-- held: not idling, so the bans leave it be; his stop command every ACTION_STOP_EVERY seconds once
-- it has held him ACTION_STOP_AFTER (never in an event: a scripted scene's actions stay)
local ACTION_STOP_AFTER = 1.0
local ACTION_STOP_EVERY = 2.0
function CoopTrico.EndStageAction(props, info, anim, dt)
	local act = info.execAction
	local moving = string.find(anim, "walk", 1, true) or string.find(anim, "slide", 1, true)
		or string.find(anim, "turn", 1, true) or string.find(anim, "gallop", 1, true)
	-- (sitting, lying and getting up are his own: the stand-up handles those; standing or waiting,
	-- the action's own animation is over though the game may still list it, coop-h-pace12: the
	-- stick's walk takes over)
	local seated = string.find(anim, "_sit", 1, true) or string.find(anim, "_lie", 1, true)
	local plain = string.sub(anim, 1, 7) == "c_stand" or string.sub(anim, 1, 6) == "c_wait"
	if not act or moving or seated or plain or info.controlMode == "EVENT" then
		CoopTrico.actionFor = 0
		return false
	end
	CoopTrico.actionFor = (CoopTrico.actionFor or 0) + dt
	CoopTrico.actionStopIn = math.max(0, (CoopTrico.actionStopIn or 0) - dt)
	if CoopTrico.actionFor < ACTION_STOP_AFTER then return false end
	if CoopTrico.actionStopIn <= 0 then
		CoopTrico.actionStopIn = ACTION_STOP_EVERY
		CoopTrico.actionStops = (CoopTrico.actionStops or 0) + 1
		pcall(Ai2.Utils.ReqCtrlCmd, info.ai2Ext, props.stopCommand or CoopTrico.ctrlStop)
		if CoopDebug then CoopDebug.Print("TR action-stop %s %s", tostring(act.name), anim) end
	end
	CoopTrico.nav = "ctrl-action"
	return true
end

-- "ctrl", every frame the stick is held (see the top).
local CTRL_KICK = 0.5          -- seconds standing into a walk before his forward request
local CTRL_UNSTICK = 4.0       -- seconds without progress before his own stuck requests are dropped
local CTRL_PROGRESS = 0.3      -- metres that count as progress
local WAKE_TIME = 2.0          -- seconds his planning may run to get him going again
local CTRL_WALK_ANGLE = 35.0   -- degrees off the stick's way he sets off walking; beyond: turn
                               -- (his turning slide can fall short, 55 of 105 degrees: a
                               -- second one beats setting off crooked)
local CTRL_CURVE_ANGLE = 100.0 -- ... and walking, keeps walking (curving round); beyond: stop, turn
local JUMP_TURN_COS = math.cos(math.rad(60))   -- this far round or more: his jump turn
local GALLOP_PUSH = 0.95       -- stick length that counts as a full push (CoopTrico.gallop)
local GALLOP_AFTER = 1.5       -- seconds of walking at a full push before the gallop
local CTRL_GEARS = { { 0.55, "ControlCommand_C_walk_slow" }, { 0.9, "ControlCommand_C_walk" },
	{ 2.0, "ControlCommand_C_walk_fast" } }
CoopTrico.ctrlStop = "ControlCommand_C_walk_stop"

function CoopTrico.CtrlGear(len)
	for _, g in ipairs(CTRL_GEARS) do
		if (len or 1) <= g[1] then return g[2] end
	end
	return CTRL_GEARS[#CTRL_GEARS][2]
end

function CoopTrico.Ctrl(dt, props, sx, sy, len)
	CoopTrico.stickLen = len
	pcall(CoopTrico.InstallHooks)
	local info = props.currentInfo
	local ai = info.ai2Ext
	local pos = CoopTrico.Position(props, dt)
	if not pos or not ai then return end
	if props.approach and not CoopTrico.skipping then
		CoopTrico.skipping = { approach = props.approach, was = props.approach.skipCreate }
		props.approach.skipCreate = true
	end
	CoopTrico.steering = true
	local okA, anim = pcall(Ai2.Utils.GetCurAnimState, ai)
	anim = okA and tostring(anim) or ""
	-- not CTRL_PROGRESS metres further CTRL_UNSTICK into a hold (turns and slides aside, which
	-- move him on the spot): a request of his that never finishes (a slide asked for while he
	-- sat went to his sitting shuffle) keeps every later one waiting behind it, or his own walk
	-- runs on a path that no longer moves (walking on the spot), so his running request and
	-- planned move are dropped and the turn or walk asked again
	local standing = string.find(anim, "stand", 1, true) and not string.find(anim, "walk", 1, true)
	CoopTrico.ctrlStill = standing and (CoopTrico.ctrlStill or 0) + dt or 0
	local spinning = string.find(anim, "slide", 1, true) or string.find(anim, "turn", 1, true)
		or string.find(anim, "_sit", 1, true) or string.find(anim, "_lie", 1, true)   -- (getting up)
		or string.find(anim, "stand_walk", 1, true)   -- (setting off: 2 s at a crawl)
		or string.find(anim, "_stop", 1, true)        -- (stopping or setting off from a stop)
	-- (at an edge he stands by design: not stuck)
	local atEdge = CoopTrico.blocked and CoopTrico.blocked < EdgeMargin() + 1.0 or CoopTrico.nav == "ctrl-unsafe"
	local from = CoopTrico.progressFrom
	if not from or spinning or atEdge or DistanceXZ(from, pos) > CTRL_PROGRESS then
		CoopTrico.progressFrom = Vector(pos)
		CoopTrico.stuckFor = 0
	else
		CoopTrico.stuckFor = (CoopTrico.stuckFor or 0) + dt
	end
	CoopTrico.unstickIn = math.max(0, (CoopTrico.unstickIn or 0) - dt)
	if CoopTrico.stuckFor >= CTRL_UNSTICK and CoopTrico.unstickIn <= 0 and props.moveMngr then
		CoopTrico.unstickIn = 2.0
		CoopTrico.unsticks = (CoopTrico.unsticks or 0) + 1
		local m = props.moveMngr
		m.runningRequest = nil
		m.requests = {}
		pcall(function() m:CancelPlannedMove(props) end)
		CoopTrico.turnIn = 0
		-- and standing (after a sit his own action still holds him and his move manager runs
		-- nothing at all), his planning back on for a moment with a steering goal the stick's
		-- way, as the steer strategy gives him, until he moves (WAKE_TIME at most)
		if standing then CoopTrico.waking = WAKE_TIME end
		if CoopDebug then CoopDebug.Print("TR unstick %s", anim) end
	end
	-- stood up from a sit or lie: his own action still holds him (his move manager runs nothing
	-- until it lets go), so the wake-up starts at once rather than after CTRL_UNSTICK
	if CoopTrico.stoodUp and standing then
		CoopTrico.stoodUp = nil
		CoopTrico.waking = WAKE_TIME
	end
	if CoopTrico.waking then
		local moving = string.find(anim, "walk", 1, true) or string.find(anim, "slide", 1, true)
			or string.find(anim, "turn", 1, true)
		CoopTrico.waking = CoopTrico.waking - dt
		if moving or CoopTrico.waking <= 0 then
			CoopTrico.waking = nil
			DropGoal(props)
			if CoopTrico.skipping then props.approach.skipCreate = true end
			if CoopDebug then CoopDebug.Print("TR woke %s", anim) end
		else
			if CoopTrico.skipping then props.approach.skipCreate = CoopTrico.skipping.was end
			local dirW = ai.dirXZ
			local fx, fz = CoopTrico.Frame(sx, sy, pos, dirW)
			fx, fz = fx or (dirW and dirW.X) or 0, fz or (dirW and dirW.Z) or 1
			local gx, gz = Flat(fx * sy - fz * sx, fz * sy + fx * sx)
			if gx then
				local d = CoopTrico.distances.goal or 14
				local goal = Vector(pos.X + gx * d, pos.Y, pos.Z + gz * d)
				if props.plannedMove == "CoopManual" then props.plannedMove = nil end
				pcall(Steer.steer, props, goal, Vector(gx, 0, gz))
				CoopTrico.goal = goal
			end
			CoopTrico.nav = "ctrl-wake"
			return
		end
	end
	-- a planned move none of his actions handle: with none at all (nil) his action layer runs its
	-- stop every frame and stops the walk commands below
	if props.plannedMove == nil or props.plannedMove == "MoveStop" then
		props.plannedMove = "CoopManual"
		props.moveParam = nil
	end
	CoopTrico.standIn = math.max(0, (CoopTrico.standIn or 0) - dt)
	if CoopTrico.StandUp(props, anim) then return end
	if CoopTrico.EndStageAction(props, info, anim, dt) then return end
	if string.find(anim, "jump_turn", 1, true) then CoopTrico.jumpTurnSeen = true end
	if (string.find(anim, "turn", 1, true) or string.find(anim, "slide_rot", 1, true)) and not string.find(anim, "walk", 1, true) then
		CoopTrico.nav = "turning"
		return
	end
	if string.find(anim, "_sit", 1, true) or string.find(anim, "_lie", 1, true) then
		CoopTrico.nav = "getting-up"   -- (a turn asked now can't play, and is lost)
		return
	end
	local dirXZ = ai.dirXZ
	local fx, fz, src = CoopTrico.Frame(sx, sy, pos, dirXZ)
	if not fx then
		if not dirXZ then return end
		fx, fz, src = dirXZ.X, dirXZ.Z, "trico"
	end
	CoopTrico.forward = { fx, fz, src }
	local wx, wz = Flat(fx * sy - fz * sx, fz * sy + fx * sx)
	if not wx or not dirXZ then return end
	local cos = wx * dirXZ.X + wz * dirXZ.Z
	CoopTrico.goal = Vector(pos.X + wx * 10, pos.Y, pos.Z + wz * 10)
	-- walking, he curves round to the stick's way as he goes up to CTRL_CURVE_ANGLE (a stop, a
	-- turn and a new start took seconds); from a stand, or further round, a turn
	local walking = string.find(anim, "walk", 1, true) and not string.find(anim, "stop", 1, true)
		and not string.find(anim, "back", 1, true)
	local limit = walking and CTRL_CURVE_ANGLE or CTRL_WALK_ANGLE
	if cos < math.cos(math.rad(limit)) then
		-- too far round: a turn on the spot, one at a time; walking, his stop command first
		-- (the turn's own stop let him walk out his whole stopping distance first)
		-- (his destination ahead the stick's way, not where he stands: on his destination he
		-- counts as arrived, and his slide action then does nothing)
		info.apDestination = Vector(pos.X + wx * 20, pos.Y, pos.Z + wz * 20)
		info.apDestinationDir = Vector(wx, 0, wz)
		props.isOnGoalInVolume = false
		if walking then
			pcall(Ai2.Utils.ReqCtrlCmd, ai, props.stopCommand or CoopTrico.ctrlStop)
			CoopTrico.nav = "ctrl-brake"
			return
		end
		if CoopTrico.turnIn <= 0 then
			CoopTrico.turnIn = 1.0
			local name = CoopTrico.TurnCommand(dirXZ.X, dirXZ.Z, wx, wz)
			-- 60 degrees and more: his jump turn, unless the last one asked for this hold never
			-- showed (then his plain turning slide)
			local jumped = false
			local poised = string.find(anim, "stand", 1, true) or string.find(anim, "stop", 1, true)
			if cos < JUMP_TURN_COS and CoopTrico.jumpTurnOk ~= false and poised then
				if CoopTrico.jumpTurnAsked and not CoopTrico.jumpTurnSeen then
					CoopTrico.jumpTurnOk = false
				else
					local okJ, did = pcall(CoopTrico.JumpTurnTo, props, wx, wz)
					jumped = okJ and did
					if jumped then CoopTrico.jumpTurnAsked = true end
				end
			end
			if not jumped then pcall(CoopTrico.TurnTo, props, wx, wz) end
			CoopTrico.nav = "ctrl-" .. (jumped and "jump-" or "") .. name
		end
		return
	end
	CoopTrico.unsafeNow = CoopTrico.Unsafe(props, info, anim)
	local ok, err = pcall(CoopTrico.ManualMove, props, pos, wx, wz, len, dt)
	-- after he has sat (and stood up), his action layer leaves the planned move alone until a
	-- move request comes through: still standing CTRL_KICK into the walk, his own forward
	-- request (the same move), once a second at most
	CoopTrico.kickIn = math.max(0, (CoopTrico.kickIn or 0) - dt)
	local plain = string.sub(anim, 1, 7) == "c_stand" and not string.find(anim, "walk", 1, true)
		or string.sub(anim, 1, 6) == "c_wait"
	if ok and plain and CoopTrico.ctrlStill >= CTRL_KICK and CoopTrico.kickIn <= 0 then
		CoopTrico.kickIn = 1.0
		CoopTrico.kicks = (CoopTrico.kicks or 0) + 1
		pcall(function() props.moveMngr:RequestForward(CoopTrico.manualMove) end)
		if CoopDebug then CoopDebug.Print("TR kick %s", anim) end
	end
	-- (experimental, CoopTrico.gallop: a full push held while he walks breaks into his gallop;
	-- the free walk leaves gallops out of its gaits, so his own command is sent on top)
	local striding = string.find(anim, "walk", 1, true) and not string.find(anim, "stop", 1, true)
		or string.find(anim, "gallop", 1, true)
	CoopTrico.strideFor = (striding and (len or 0) > GALLOP_PUSH) and (CoopTrico.strideFor or 0) + dt or 0
	if ok and CoopTrico.gallop and CoopTrico.strideFor >= GALLOP_AFTER then
		local cmd = props.ctrlCmds and props.ctrlCmds[CoopTrico.gallop == true and "MV_C_GALLOPSLOW" or CoopTrico.gallop]
		if cmd then pcall(Ai2.Utils.ReqCtrlCmd, ai, cmd) end
		CoopTrico.nav = "ctrl-gallop"
		return
	end
	if ok and CoopTrico.unsafeNow then
		CoopTrico.nav = "ctrl-unsafe"
	elseif ok then
		CoopTrico.nav = CoopTrico.blocked and string.format("ctrl-edge%.0f", CoopTrico.blocked) or "ctrl-walk"
	else
		-- (no path to be had: his walk commands, which at least start him)
		pcall(Ai2.Utils.ReqCtrlCmd, ai, CoopTrico.CtrlGear(len))
		CoopTrico.nav = "ctrl-cmd"
		if err ~= CoopTrico.lastError then
			CoopTrico.lastError = err
			if CoopDebug then CoopDebug.Print("TR error %s", tostring(err)) end
		end
	end
end

function CoopTrico.Unskip(props)
	local sk = CoopTrico.skipping
	if not sk then return end
	CoopTrico.skipping = nil
	CoopTrico.waking = nil
	CoopTrico.jumpTurnAsked, CoopTrico.jumpTurnSeen, CoopTrico.jumpTurnOk = nil, nil, nil
	CoopTrico.strideFor = 0
	CoopTrico.stoodUp = nil
	CoopTrico.progressFrom, CoopTrico.stuckFor = nil, 0
	CoopTrico.unsafeNow, CoopTrico.actionFor = nil, 0
	sk.approach.skipCreate = sk.was
	if sk.walk then sk.walk.suspendStopDecision = sk.stopDecision end
	if props and (props.plannedMove == "CoopManual" or props.moveParam == CoopTrico.manualMove) then
		props.plannedMove = nil
		props.moveParam = nil
	end
	if props and props.currentInfo and props.currentInfo.ai2Ext then
		pcall(Ai2.Utils.ReqCtrlCmd, props.currentInfo.ai2Ext, CoopTrico.ctrlStop)
	end
end

-- "direct", every frame the stick is held: stand up if he sits, one turn on the spot if he is
-- standing and faces far from the stick's way, otherwise the manual free move towards it.
function CoopTrico.Direct(dt, props, sx, sy, len)
	local info = props.currentInfo
	local pos = CoopTrico.Position(props, dt)
	if not pos then return end
	local okA, anim = pcall(Ai2.Utils.GetCurAnimState, info.ai2Ext)
	anim = okA and tostring(anim) or ""
	CoopTrico.steering = true
	CoopTrico.standIn = math.max(0, (CoopTrico.standIn or 0) - dt)
	if CoopTrico.StandUp(props, anim) then return end
	if (string.find(anim, "turn", 1, true) or string.find(anim, "slide_rot", 1, true)) and not string.find(anim, "walk", 1, true) then
		CoopTrico.nav = "turning"
		return   -- let a turn on the spot play out
	end
	local dirXZ = info.ai2Ext and info.ai2Ext.dirXZ
	local fx, fz, src = CoopTrico.Frame(sx, sy, pos, dirXZ)
	if not fx then
		if not dirXZ then return end
		fx, fz, src = dirXZ.X, dirXZ.Z, "trico"
	end
	CoopTrico.forward = { fx, fz, src }
	local wx, wz = Flat(fx * sy - fz * sx, fz * sy + fx * sx)   -- the right of (x, z) is (-z, x)
	if not wx then return end
	if dirXZ then
		local stickAngle = math.deg(math.atan2(sx, sy))
		local spun = CoopTrico.spunFor and math.abs((stickAngle - CoopTrico.spunFor + 180) % 360 - 180) <= RELATCH
		local standing = props.plannedMove == nil or props.plannedMove == "MoveStop"
		if not spun and standing and (wx * dirXZ.X + wz * dirXZ.Z) < TURN_START then
			local name = CoopTrico.TurnCommand(dirXZ.X, dirXZ.Z, wx, wz)
			CoopTrico.spunFor = stickAngle
			pcall(CoopTrico.TurnTo, props, wx, wz)
			CoopTrico.nav = "spot-" .. name
			return
		end
	end
	local ok, err = pcall(CoopTrico.DirectMove, props, pos, wx, wz, len)
	if ok then
		CoopTrico.nav = "direct"
	elseif err ~= CoopTrico.lastError then
		CoopTrico.lastError = err
		if CoopDebug then CoopDebug.Print("TR error %s", tostring(err)) end
	end
end

-- Debug (CoopTrico.logAI = true, with the debug channel on): turn on Trico's own move log and
-- pass its stop and request messages on, each distinct one at most once a second.
function CoopTrico.HookAILog(props, dt)
	local log = props and props.log
	if not log then return end
	if not CoopTrico.aiLogHooked then
		CoopTrico.aiLogHooked = true
		log.valid = true
		local add = log.AddMessage
		local seen = {}
		CoopTrico.aiLogSeen = seen
		log.AddMessage = function(self, report, fmt, ...)
			local ok, msg = pcall(string.format, tostring(fmt), ...)
			msg = ok and msg or tostring(fmt)
			if string.find(msg, "stop") or string.find(msg, "request") or string.find(msg, "cancel")
				or string.find(msg, "slide") or string.find(msg, "skip") or string.find(msg, "fail") then
				local key = tostring(report and report.name) .. ":" .. msg
				if not seen[key] then
					seen[key] = true
					if CoopDebug then CoopDebug.Print("AI %s: %s", tostring(report and report.name), string.sub(msg, 1, 90)) end
				end
			end
			return add(self, report, fmt, ...)
		end
	end
	CoopTrico.aiLogClear = (CoopTrico.aiLogClear or 1) - dt
	if CoopTrico.aiLogClear <= 0 then
		CoopTrico.aiLogClear = 1
		for k in pairs(CoopTrico.aiLogSeen) do CoopTrico.aiLogSeen[k] = nil end
	end
end

-- Give Trico back to its AI (when the player leaves the role).
function CoopTrico.Release()
	if CoopTrico.steering then Release(Props()) end
	pcall(CoopTrico.Unban)
	CoopTrico.idleFor = 99
end

CoopTrico.Reset()
