-- tlg-coop: Trico driven by a player's left stick.
--
-- The stick picks a goal a few metres ahead of Trico, relative to the camera, and Trico's own
-- AI walks there (paths, turns, jumps and animation stay the game's). Several ways of handing the
-- goal to the AI are kept while they are compared on the real game; CoopTrico.strategy picks one:
--   "interest": the boy's pointing instruction target is moved to the goal and Trico is told
--               to be interested in it; the approach destination is overwritten with the goal.
--   "forward":  the move manager is asked for a free forward move towards the overwritten
--               approach destination.
--   "slide":    the move manager is asked to slide to the goal (short adjustments only).
-- With the stick released, Trico's AI is left alone.

CoopTrico = CoopTrico or {}

local PROXY_NAME = "PlayerAi2Instruction_InstProxyEntity"
local DEADZONE = 0.3
local GOAL_DISTANCE = 6.0      -- metres ahead of Trico at full stick
local MIN_GOAL_DISTANCE = 3.0
local REFRESH = 0.3            -- seconds between goal updates
local REPORT = 0.5             -- seconds between debug reports

CoopTrico.strategy = CoopTrico.strategy or "interest"

function CoopTrico.Reset()
	CoopTrico.steering = false
	CoopTrico.goal = nil
	CoopTrico.refreshIn = 0
	CoopTrico.reportIn = 0
	CoopTrico.lastError = nil
end

local function Props()
	local ok, props = pcall(function() return Ai2.Utils.GetCondorAIProps() end)
	if ok then return props end
	return nil
end

-- Stick vector (x right, y up), 0..1 long, or nil inside the dead zone.
function CoopTrico.Stick(pad)
	if not pad then return nil end
	local mag = pad.LMag or 0
	if mag < DEADZONE then return nil end
	local x = (pad.LX or 0) * mag
	local y = (pad.LY or 0) * mag
	local len = math.sqrt(x * x + y * y)
	if len < DEADZONE then return nil end
	if len > 1 then x, y, len = x / len, y / len, 1 end
	return x, y, len
end

-- Camera forward on the ground plane, as (x, z), from the camera to Trico.
local function CameraForward(tricoPos)
	local cm = FindObject and FindObject("CameraManager")
	local cam = cm and cm.camera
	local pos = cam and cam.worldPosition
	if pos then
		local dx, dz = tricoPos.X - pos.X, tricoPos.Z - pos.Z
		local len = math.sqrt(dx * dx + dz * dz)
		if len > 0.01 then return dx / len, dz / len end
	end
	return nil
end

-- The goal for a stick vector: camera-relative, on Trico's height.
function CoopTrico.GoalFor(tricoPos, tricoDirX, tricoDirZ, sx, sy, len, fx, fz)
	if not fx then fx, fz = tricoDirX, tricoDirZ end
	-- right = forward rotated -90 degrees about Y (looking along +Z, right is -X)
	local rx, rz = -fz, fx
	local dx = fx * sy + rx * sx
	local dz = fz * sy + rz * sx
	local dlen = math.sqrt(dx * dx + dz * dz)
	if dlen < 0.001 then return nil end
	dx, dz = dx / dlen, dz / dlen
	local dist = MIN_GOAL_DISTANCE + (GOAL_DISTANCE - MIN_GOAL_DISTANCE) * len
	return tricoPos.X + dx * dist, tricoPos.Y, tricoPos.Z + dz * dist, dx, dz
end

local function OverwriteApproach(props, goal, dir)
	Ai2.Approach.Overwrite:Request(props, {
		destination = goal,
		destinationDir = dir,
		condition = function() return CoopTrico.steering end,
	})
end

local Steer = {}

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

function Steer.slide(props, goal, dir)
	props.moveMngr:RequestSlide({ destination = goal, destinationDir = dir })
end

local function Release(props)
	CoopTrico.steering = false
	CoopTrico.goal = nil
	if not props then return end
	pcall(Ai2.Utils.ResetAllInstructionLevel, props.currentInfo.ai2Ext)
	pcall(function() props.moveMngr:RequestStop(nil, props) end)
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
	CoopDebug.Print("TR %s %s p=%.1f,%.1f,%.1f g=%s pm=%s rq=%s in=%s md=%s",
		CoopTrico.strategy, CoopTrico.steering and "on" or "off", p.X, p.Y, p.Z,
		g and string.format("%.1f,%.1f", g.X, g.Z) or "-",
		tostring(props.plannedMove), tostring(run and run.name),
		tostring(okName and intrName or "?"),
		tostring(info.aiMode and info.aiMode.name))
end

-- Call once per frame with the Trico player's pad (nil when nobody plays Trico).
function CoopTrico.Update(dt, pad)
	dt = dt or 0
	local props = Props()
	CoopTrico.reportIn = CoopTrico.reportIn - dt
	if CoopTrico.reportIn <= 0 then
		CoopTrico.reportIn = REPORT
		pcall(Report, props)
	end
	if not props or not props.currentInfo or not props.currentInfo.aiPos then return end

	local sx, sy, len = CoopTrico.Stick(pad)
	if not sx then
		if CoopTrico.steering then Release(props) end
		return
	end

	CoopTrico.refreshIn = CoopTrico.refreshIn - dt
	if CoopTrico.steering and CoopTrico.refreshIn > 0 then return end
	CoopTrico.refreshIn = REFRESH

	local info = props.currentInfo
	local pos = info.aiPos
	local dirXZ = info.ai2Ext and info.ai2Ext.dirXZ
	local fx, fz = CameraForward(pos)
	local gx, gy, gz, dx, dz = CoopTrico.GoalFor(pos, dirXZ and dirXZ.X or 0, dirXZ and dirXZ.Z or 1,
		sx, sy, len, fx, fz)
	if not gx then return end
	local goal = Vector(gx, gy, gz)
	local dir = Vector(dx, 0, dz)
	CoopTrico.steering = true
	CoopTrico.goal = goal
	local steer = Steer[CoopTrico.strategy] or Steer.interest
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
