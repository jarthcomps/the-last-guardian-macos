-- tlg-coop: the boy's R1 gestures as the Trico player's own buttons.
--
-- The boy's gestures (R1 + Cross / Square / Triangle) point Trico at a dummy interest of
-- subtype Instruction_Unique, whose action hierarchy (Ai2.IntActHierarchy.Condor.Instruction)
-- picks the action from the gesture once the eye-contact handshake has granted it. The Trico
-- player skips the handshake: the scripts put such an interest ahead of Trico and set what the
-- handshake sets (condorinstruction.lua), so the game's own action runs, approach and all. Sending
-- the action's control command alone does nothing while Trico idles (c_wait), as tried in game.
--   Cross:    jump. First the level's own way on: the hint interest the level last gave Trico
--             (what walked him up the fallen pillar, round and over the jump by himself), when it
--             lies the stick's way (else his facing's) within EDGE_CONE and HINT_RANGE and was
--             seen within HINT_MEMORY; it gets the top interest level until he is there or
--             HINT_TIMEOUT. Then the level's own jump spots: the Instruction interests (where the boy's
--             "jump" gesture sends Trico) inside auto-Trico's search volume that he can't simply
--             walk to (those a special edge, a jump, is known to lead to first), the one nearest
--             the stick's way (else Trico's facing) within EDGE_CONE; it gets the top interest
--             level, as auto-Trico's special-edge mode does, so Trico walks up and takes the jump
--             his own way (forward jumps onto pillars and ledges). Pushing the stick again after
--             the press lets the spot go (steering waits while it is held). Else a point ahead on such an island
--             (a grid of distances, angles and heights) gets an auto-Trico style interest. With
--             neither, the jump gesture (Trico pounces, as the boy's gesture does without a
--             target).
--   Square:   the attack gesture (a side swipe, the side the game picks from the target's angle,
--             so the target goes on the stick's side).
--   Circle:   eat: the nearest edible barrel Trico knows (within BARREL_REACH, or in its view
--             within BARREL_VIEW) gets the top interest level for a while, so Trico walks over and
--             runs its own eating action. With no such barrel, the boy's "Action" gesture, on
--             whatever Trico is interested in (on a barrel box Trico breaks it); with nothing but
--             the boy or a hint in its interest, on the dummy ahead (Trico tilts its head).
--             With the stick held left or right: a pull with that paw (C_touch_left/right_pull),
--             sent once Trico stands still (steering waits for it; queued like the moves below).
--   Up:       rear up on the hind legs (C_rise, which Trico takes from idle too), held while the
--             button is.
-- Single moves (one control command each). Sent at once while Trico stands or waits; while it
-- sits or lies Trico is first asked to stand, and while it walks steering waits: the move then
-- goes as soon as Trico stands, within QUEUE_TIME of the press (dropped and logged otherwise).
-- Presses made meanwhile queue up behind it (at most QUEUE_MAX waiting; a press beyond that is
-- dropped), and each queued move goes once Trico stands again, at least SEND_GAP after the last:
--   Triangle stomp; R1 roar; L1 threat posture (held, ended on release); Down sit, then lie down,
--   then stand up; Left stretch (front, up, back in turn); Right hind-leg kick (left and right in
--   turn).
--   L3 tapped alone (the emulator counts the taps): turn on the spot to face the stick's way
--   (camera-relative, as steering), or right round with the stick centred; queued like a move.
-- Context moves (eating, climbing, swimming...) stay with Trico's AI: buttons only add requests.
-- Nothing here runs while the Trico player aims the lightning (L2); Circle stays the boy's shield.

CoopActions = CoopActions or {}

local JUMP_NAME = "CoopActions_Jump"
local JUMP_INTEREST = "intr_AutoTorico"
local JUMP_LEVEL_NAME = "CoopActions_Jump"
local JUMP_DISTANCES = { 6.0, 10.0, 14.0 }
local JUMP_ANGLES = { 0, 25, -25 }            -- degrees from the jump's way
local JUMP_HEIGHTS = { 0, 5, -5, 10, -10 }    -- metres up and down (ledges, rock piles)
local JUMP_ARRIVED = 4.0                      -- metres from the target that count as there
local JUMP_TIMEOUT = 10.0
local EDGE_VOLUME = "autoTrico_findSpEdgeVol"   -- auto-Trico's volume for finding jump spots
local EDGE_TYPES = { "Instruction", "Instruction_30s" }
local EDGE_CONE = 90.0                          -- degrees either side of the jump's way
local EDGE_RANGE = 45.0                         -- metres at most
local EDGE_TIMEOUT = 20.0                       -- seconds to walk up and jump
local EDGE_ARRIVED = 5.5                        -- metres from the spot that count as there (as auto-Trico)
local HINT_RANGE = 60.0                         -- metres at most to the level's hint
local HINT_MEMORY = 120.0                       -- seconds a seen hint is remembered
local HINT_TIMEOUT = 30.0
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
local BARREL_REACH = 15.0                     -- metres: an edible barrel this close is eaten on Circle
local BARREL_VIEW = 30.0                      -- ... or this close while Trico sees it
local EAT_TIME = 15.0                         -- seconds the barrel keeps the top interest level
local QUEUE_TIME = 8.0                        -- seconds a move waits for Trico to stand still (getting
                                              -- up from sitting or stopping a walk takes 3-8 s)
local QUEUE_MAX = 2                           -- moves waiting at once
local SEND_GAP = 0.4                          -- seconds after a move before the next one goes, so
                                              -- Trico has started the first
local RETRY_AFTER = 0.5                       -- seconds without the animation changing before a sent
local TRIES = 3                               -- move is sent again (at most TRIES sends in all)
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
	turn90Left = "ControlCommand_C_turn_90_left",
	turn90Right = "ControlCommand_C_turn_90_right",
	aboutLeft = "ControlCommand_C_turn_180_left",
	aboutRight = "ControlCommand_C_turn_180_right",
	sitEnd = "ControlCommand_C_sit_end",     -- up from sitting (as the game's own scripts do)
}
local STAND_AGAIN = 2.0                       -- seconds between stand-up requests while a move waits
CoopActions.queueTime = QUEUE_TIME
local FACING = 0.7                            -- cosine: a turn already this close is not needed
local SWAP_QUIET = 1.5                        -- seconds after a swap in which L3 taps are ignored
local STRETCHES = { "stretchFront", "stretchUp", "stretchBack" }
-- animation states a single move may start from (not the walk starts: c_stand_walk_fastC...)
local IDLE = { "c_stand", "c_wait", "c_idle" }
local MOVING = { "walk", "run", "gallop", "turn", "slide", "jump",
	-- and Trico's own fidgets from standing (c_stand_shake_high...), which take no commands
	"shake", "stretch", "yawn", "excite", "roar", "attack", "look", "scratch", "sniff" }
local SEATED = { "c_sit", "ext_c_sit", "c_lie", "ext_c_lie", "c_liedown", "ext_c_liedown" }

function CoopActions.Reset()
	CoopActions.jump = nil        -- { target, timer } while a jump target is held
	CoopActions.gesture = nil     -- { type, timer } while a gesture waits for its action
	CoopActions.queue = {}        -- { name, timer } moves waiting for Trico to stand still, in order
	CoopActions.pending = nil     -- the first of them (steering waits while there is one)
	CoopActions.gap = 0
	CoopActions.riseIn = nil
	CoopActions.lastSwipe = "right"
	CoopActions.watch = 0
	CoopActions.lastAction = nil
	CoopActions.lastAnim = nil
	CoopActions.lastError = nil
end

-- Trico's position, as steering reads it (coop_trico.lua: aiPos can read zero).
local function TricoAt(props)
	if CoopTrico and CoopTrico.Position then return CoopTrico.Position(props) or props.currentInfo.aiPos end
	return props.currentInfo.aiPos
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
	local pos = TricoAt(props)
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

-- The level's hint interests (their type "CommonHint"), remembered as Trico has them in mind.
function CoopActions.NoteHint(props)
	local info = props.currentInfo
	local t = tostring(info.interestType or "")
	if not string.find(t, "Hint", 1, true) then return end
	local intr = info.interestEntity
	local okP, p = pcall(function() return intr.worldPosition end)
	if not intr or not okP or not p then return end
	CoopActions.hint = { entity = intr, pos = Vector(p), seen = CoopActions.clock or 0 }
end

-- The remembered hint, when it lies the way (wayX, wayZ) within EDGE_CONE and HINT_RANGE.
function CoopActions.HintAhead(props, wayX, wayZ)
	local h = CoopActions.hint
	if not h or (CoopActions.clock or 0) - h.seen > HINT_MEMORY then return nil end
	local pos = TricoAt(props)
	local dx, dz = h.pos.X - pos.X, h.pos.Z - pos.Z
	local d = math.sqrt(dx * dx + dz * dz)
	if d > HINT_RANGE or d < EDGE_ARRIVED then return nil end
	if (dx * wayX + dz * wayZ) / d < math.cos(math.rad(EDGE_CONE)) then return nil end
	return h
end

-- The search volume is attached as soon as the Trico player is there (an attach is only a request
-- the engine carries out later, so it can't wait for the press).
function CoopActions.AttachEdgeVolume(props)
	if CoopActions.edgeVolume then return CoopActions.edgeVolume end
	local vol = Ai2.Utils.GetVolumeByAssetName(props, EDGE_VOLUME)
	if not vol then return nil end
	Ai2.Utils.AttachVolume(props, vol)
	CoopActions.edgeVolume = vol
	CoopActions.edgeProps = props
	return vol
end

function CoopActions.DetachEdgeVolume()
	local vol, props = CoopActions.edgeVolume, CoopActions.edgeProps
	CoopActions.edgeVolume, CoopActions.edgeProps = nil, nil
	if vol and props then pcall(Ai2.Utils.DetachVolume, props, vol) end
end

-- A level jump spot (an Instruction interest a special edge leads to) the stick's way: the
-- entity and its position, or nil.
function CoopActions.FindEdgeInterest(props, wayX, wayZ)
	local vol = CoopActions.edgeVolume
	if not vol then return nil end
	local found, list = Ai2.Utils.IsInVolumeSubType(vol, EDGE_TYPES)
	if not found or type(list) ~= "table" then
		Say("AC edge search: no Instructions in the volume")
		return nil
	end
	local pos = TricoAt(props)
	local check = Ai2.Utils.Navmesh.CheckSameIsland
	local best, bestPos, bestScore, bestD = nil, nil, nil, nil
	local cosCone = math.cos(math.rad(EDGE_CONE))
	for _, intr in ipairs(list) do
		local okP, p = pcall(function() return intr.worldPosition end)
		if okP and p then
			local dx, dz = p.X - pos.X, p.Z - pos.Z
			local d = math.sqrt(dx * dx + dz * dz)
			local cos = d > 0.01 and (dx * wayX + dz * wayZ) / d or 1
			if CoopDebug and CoopDebug.enabled then
				local okA, walk = pcall(check, props, pos, p, true)
				local okB, edge = pcall(check, props, pos, p, false)
				local okN, name = pcall(function() return intr:GetName() end)
				local okS, sp = pcall(Ai2GetStraightPath, props.currentInfo.ai2Ext, pos, p,
					Ai2.Approach and Ai2.Approach.findNearPosExt, Ai2.Approach and Ai2.Approach.findNearPosExt)
				local spInfo = "-"
				if okS and type(sp) == "table" and sp.path then
					local last = sp.path[#sp.path]
					spInfo = string.format("n=%d sp=%s end=%.1f short=%.1f", #sp.path, tostring(sp.spEdge),
						sp.endOnPoly and last and Vector.Distance3(sp.endOnPoly, last) or -1,
						last and math.sqrt((last.X - p.X) ^ 2 + (last.Z - p.Z) ^ 2) or -1)
				end
				Say("AC edge spot %s %.1f m %.0f deg up %.1f walk=%s edge=%s path %s", okN and tostring(name) or "?", d,
					math.deg(math.acos(math.max(-1, math.min(1, cos)))), p.Y - pos.Y, tostring(walk), tostring(edge), spInfo)
			end
			if d <= EDGE_RANGE and cos >= cosCone then
				local okA, walk = pcall(check, props, pos, p, true)
				local okB, edge = pcall(check, props, pos, p, false)
				-- (a level's jump spot is reached through its own special approach, which the
				-- navmesh checks don't see: from the ledge before the pillar bridge the spot past
				-- the jump read neither walkable nor across a special edge, so any spot he can't
				-- simply walk to counts, those the edge check confirms first)
				if okA and walk == false then
					-- the nearest to the stick's way first, then the nearest
					local score = (1 - cos) * 40 + d + ((okB and edge == true) and 0 or 10)
					if not bestScore or score < bestScore then best, bestPos, bestScore, bestD = intr, p, score, d end
				end
			end
		end
	end
	Say("AC edge search: %d Instructions, %s", #list, best and string.format("best %.1f m away", bestD) or "none across an edge the stick's way")
	return best, bestPos
end

-- A point a special edge leads to (another navmesh island, reachable only through the edge).
function CoopActions.FindJumpTarget(props, wayX, wayZ)
	local pos = TricoAt(props)
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
	local pos = TricoAt(props)
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

-- Ask a seated Trico to stand: the end of sitting from a sit, the stand command from lying.
local function StandUp(props, anim)
	CoopActions.standIn = STAND_AGAIN
	return Command(props, (string.find(anim, "sit") and not Lying(anim)) and "sitEnd" or "stand")
end

local function Idle(anim)
	if not StartsWith(anim, IDLE) then return false end
	for _, word in ipairs(MOVING) do
		if string.find(anim, word, 1, true) then return false end
	end
	return true
end

-- Put a move at the back of the queue. Returns false when the queue is full.
local function Queue(name, seatedToo, pressedAt)
	local q = CoopActions.queue
	if #q >= QUEUE_MAX then return false end
	local now = CoopActions.clock or 0
	pressedAt = pressedAt or now
	-- the QUEUE_TIME counts from the press, also for a move refused and queued later
	q[#q + 1] = { name = name, timer = QUEUE_TIME - (now - pressedAt), seated = seatedToo, pressedAt = pressedAt }
	CoopActions.pending = q[1]
	return true
end

-- Send a single move and watch for Trico to start it (see Watch).
local function Send(props, name, anim, pressedAt, seatedToo)
	CoopActions.gap = SEND_GAP
	CoopActions.sent = { name = name, t = 0, tries = 1, anim = anim, eager = not Idle(anim),
		pressedAt = pressedAt or CoopActions.clock, seated = seatedToo }
	return Command(props, name)
end

-- A sent move counts as started once Trico's animation changes; until then it is sent again
-- every RETRY_AFTER seconds, TRIES times in all, then given up (both logged).
local function Watch(dt, props, anim)
	local w = CoopActions.sent
	if not w then return end
	w.t = w.t + dt
	if anim ~= w.anim then
		CoopActions.sent = nil
		CoopActions.started = (CoopActions.started or 0) + 1
		Say("AC %s started anim=%s %.2f s after the press, %d send(s)", w.name, anim,
			(CoopActions.clock or 0) - (w.pressedAt or 0), w.tries)
	elseif w.t >= RETRY_AFTER then
		if w.tries >= TRIES then
			CoopActions.sent = nil
			if w.eager and not w.requeued then
				-- sent while Trico was busy and refused: wait for him after all
				Queue(w.name, w.seated, w.pressedAt)
				Say("AC %s refused by %s: waits for Trico", w.name, anim)
			else
				CoopActions.missed = (CoopActions.missed or 0) + 1
				Say("AC %s not started anim=%s", w.name, anim)
			end
		else
			w.tries, w.t = w.tries + 1, 0
			Command(props, w.name)
		end
	end
end

-- A single move: at once from a standing or waiting (or, for the posture moves, seated) Trico
-- with nothing queued; otherwise queued until Trico stands (asked to, when it sits or lies).
-- Returns the report detail.
-- Sent at once whatever Trico is doing (most of his own actions take a command and give way),
-- unless he sits and the move needs him standing; refused ones then wait (see Watch).
local function Move(props, name, anim, seatedToo)
	local free = #CoopActions.queue == 0 and CoopActions.gap <= 0
	if free and (seatedToo or not StartsWith(anim, SEATED)) then
		return Send(props, name, anim, nil, seatedToo)
	end
	if not Queue(name, seatedToo) then return name .. " dropped, " .. QUEUE_MAX .. " waiting" end
	local place = #CoopActions.queue > 1 and (" behind " .. CoopActions.queue[1].name) or ""
	if StartsWith(anim, SEATED) and not seatedToo then
		StandUp(props, anim)
		return name .. " waiting" .. place .. ", standing up from " .. anim
	end
	return name .. " waiting" .. place .. " " .. anim
end

-- The queue, each frame: drop the moves that waited too long, send the first once Trico stands.
local function TickQueue(dt, props, anim)
	CoopActions.gap = math.max(0, CoopActions.gap - dt)
	local q = CoopActions.queue
	for i = #q, 1, -1 do
		q[i].timer = q[i].timer - dt
		if q[i].timer <= 0 then
			Say("AC %s refused %s", q[i].name, anim)
			table.remove(q, i)
		end
	end
	local first = q[1]
	-- still seated with a standing move waiting: ask again now and then
	CoopActions.standIn = (CoopActions.standIn or 0) - dt
	if first and not first.seated and StartsWith(anim, SEATED) and CoopActions.standIn <= 0 then
		StandUp(props, anim)
	end
	if first and CoopActions.gap <= 0 and (Idle(anim) or (first.seated and StartsWith(anim, SEATED))) then
		table.remove(q, 1)
		Say("AC %s sent %s anim=%s", first.name, Send(props, first.name, anim, first.pressedAt, first.seated), anim)
		if CoopActions.sent then CoopActions.sent.requeued = true end
		CoopActions.lastAction, CoopActions.watch, CoopActions.lastAnim = first.name, WATCH, nil
	end
	CoopActions.pending = q[1]
end

-- The edible barrel to eat: the nearest of Trico's Barrel interests in reach, or in view.
function CoopActions.EdibleBarrel(props)
	local info = props.currentInfo
	local ok, list = pcall(Ai2.Utils.FindInterestByType, info.ai2Ext, "Barrel", true)
	if not ok or type(list) ~= "table" then return nil end
	local pos = TricoAt(props)
	local best, bestDist = nil, nil
	for _, intr in ipairs(list) do
		local ent = intr.sensorEntity
		local okPos, p = pcall(function() return ent and ent.worldPosition end)
		if okPos and p then
			local d = math.sqrt((p.X - pos.X) ^ 2 + (p.Z - pos.Z) ^ 2)
			local okView, seen = pcall(Ai2IsInterestViewable, intr)
			local visible = okView and seen == 1
			if (d <= BARREL_REACH or (visible and d <= BARREL_VIEW)) and (not bestDist or d < bestDist) then
				best, bestDist = ent, d
			end
		end
	end
	return best, bestDist
end

-- Circle: eat a barrel in reach; else the Action gesture on Trico's current interest, else on
-- the dummy ahead.
local function Action(props, input)
	local barrel, dist = CoopActions.EdibleBarrel(props)
	if barrel then
		local ai = props.currentInfo.ai2Ext
		Ai2.Utils.InterestExclusionSwitching(ai, barrel, false)
		Ai2.Utils.InterestLevelAddition(ai, barrel, "CoopActions_Eat", "addMax", EAT_TIME)
		local okName, name = pcall(function() return barrel:GetName() end)
		return string.format("eat %s %.1f m", okName and tostring(name) or "?", dist)
	end
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
	if j.entity then
		pcall(Ai2.Utils.ResetInterestLevelAddition, props.currentInfo.ai2Ext, j.entity, JUMP_LEVEL_NAME)
		if CoopActions.gesture and CoopActions.gesture.spot then DropGesture(props, "jump " .. why) end
		Say("AC jump end %s", why)
		return
	end
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
	local ai = props.currentInfo.ai2Ext
	local okH, hint = pcall(CoopActions.HintAhead, props, wayX, wayZ)
	if okH and hint then
		DropJump(props, "replaced")
		Ai2.Utils.RegisterInterest(ai, hint.entity)
		Ai2.Utils.InterestExclusionSwitching(ai, hint.entity, false)
		Ai2.Utils.InterestLevelAddition(ai, hint.entity, JUMP_LEVEL_NAME, "addMax")
		CoopActions.jump = { target = Vector(hint.pos), timer = HINT_TIMEOUT, entity = hint.entity, arrived = EDGE_ARRIVED }
		local okN, name = pcall(function() return hint.entity:GetName() end)
		return string.format("hint %s %.1f,%.1f,%.1f", okN and tostring(name) or "?", hint.pos.X, hint.pos.Y, hint.pos.Z)
	end
	local okE, spot, spotPos = pcall(CoopActions.FindEdgeInterest, props, wayX, wayZ)
	if okE and spot then
		DropJump(props, "replaced")
		Ai2.Utils.RegisterInterest(ai, spot)
		Ai2.Utils.InterestExclusionSwitching(ai, spot, false)
		Ai2.Utils.InterestLevelAddition(ai, spot, JUMP_LEVEL_NAME, "addMax")
		CoopActions.jump = { target = Vector(spotPos), timer = EDGE_TIMEOUT, entity = spot, arrived = EDGE_ARRIVED }
		local okN, name = pcall(function() return spot:GetName() end)
		-- and the boy's jump gesture at it, granted as the eye-contact handshake would once the
		-- spot is what Trico has in mind, so his Instruction action takes the jump
		if okN and name then
			DropGesture(props, "replaced")
			CoopActions.gesture = { type = "Gesture_Jump", timer = EDGE_TIMEOUT, target = name, spot = true }
		end
		return string.format("spot %s %.1f,%.1f,%.1f", okN and tostring(name) or "?", spotPos.X, spotPos.Y, spotPos.Z)
	end
	local target = CoopActions.FindJumpTarget(props, wayX, wayZ)
	if not target then
		return "gesture " .. Gesture(props, "Gesture_Jump", wayX, wayZ)
	end
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

-- L3: the turn that faces Trico the stick's way, or right round. Returns the report detail.
local function Turn(props, input, anim)
	local info = props.currentInfo
	local face = info.ai2Ext and info.ai2Ext.dirXZ
	if not face then return "no facing" end
	local wayX, wayZ = -face.X, -face.Z
	local sx, sy
	if CoopTrico then sx, sy = CoopTrico.Stick(input) end   -- (an `and` would keep only sx)
	if sx then
		local fx, fz = CoopTrico.Frame(sx, sy, TricoAt(props), face)
		fx, fz = fx or face.X, fz or face.Z
		local x, z = fx * sy - fz * sx, fz * sy + fx * sx   -- the right of (x, z) is (-z, x)
		local len = math.sqrt(x * x + z * z)
		if len < 0.001 then return "no way" end
		wayX, wayZ = x / len, z / len
		if wayX * face.X + wayZ * face.Z > FACING then return "already facing" end
	end
	-- the move manager's own turning slide (never the raw turn commands: see CoopTrico.TurnTo)
	return CoopTrico.TurnTo(props, wayX, wayZ)
end

local function AnimState(props)
	local ok, st = pcall(Ai2.Utils.GetCurAnimState, props.currentInfo.ai2Ext)
	return ok and tostring(st) or "?"
end

-- Debug only (CoopActions.testEdges = { at = seconds, names = { ... } }, with the debug channel
-- on): switch on level jump edges the way a mission script does once the boy has done his part,
-- so a jump can be tried in a test run without playing the boy there.
local function TestEdges(dt)
	local te = CoopActions.testEdges
	if not te or te.done or not (CoopDebug and CoopDebug.enabled) then return end
	te.clock = (te.clock or 0) + dt
	if te.clock < (te.at or 0) or not MS or not MS.SetEnableSpPathEdge then return end
	te.done = true
	for _, name in ipairs(te.names or {}) do
		local ok, err = pcall(MS.SetEnableSpPathEdge, name)
		Say("AC test edge %s %s", name, ok and "on" or tostring(err))
	end
	-- (te.interest: what the canyon script does once the boy rides Trico at the pillar: his
	-- waiting pose let go and the level's guidance interest woken at the top level)
	if te.interest then
		local okA = pcall(MS.SetGameObjectFlagsByTypeAndSubType, te.interest, "Interest", "Guidance", "~Disable")
		local okB = pcall(MS.CondorStartInterest, te.interest)
		local okC = pcall(MS.CondorAddInterestLevel, te.interest, "CoopTest", "addMax")
		local okD = pcall(Ai2.CondorUtils.ResetOverwriteStay, Ai2.Utils.GetAIProps("Condor"))
		Say("AC test interest %s %s %s %s %s", te.interest, tostring(okA), tostring(okB), tostring(okC), tostring(okD))
	end
end

local function Step(dt, input, props, anim)
	pcall(TestEdges, dt)
	pcall(CoopActions.NoteHint, props)
	TickGesture(dt, props, anim)
	Watch(dt, props, anim)
	-- a held jump target: until Trico is there (or on its island), or the time is up
	local j = CoopActions.jump
	-- the stick let go and pushed again after the press: the player wants to steer instead
	if j and input then
		local pushed = CoopTrico and CoopTrico.Stick(input) ~= nil
		if not pushed then j.stickFree = true
		elseif j.stickFree then DropJump(props, "stick") j = nil end
	end
	if j then
		j.timer = j.timer - dt
		local pos = TricoAt(props)
		local dx, dz = pos.X - j.target.X, pos.Z - j.target.Z
		local okSame, same = pcall(Ai2.Utils.Navmesh.CheckSameIsland, props, pos, j.target, true)
		if math.sqrt(dx * dx + dz * dz) < (j.arrived or JUMP_ARRIVED) or (okSame and same) then
			DropJump(props, "arrived")
		elseif j.timer <= 0 then
			DropJump(props, "timeout")
		end
	end
	if not input then
		CoopActions.riseIn = nil
		return
	end
	if not CoopActions.edgeVolume then pcall(CoopActions.AttachEdgeVolume, props) end
	local down, hold = input.down or {}, input.hold or {}
	if hold.L2 then CoopActions.riseIn = nil return end   -- aiming the lightning
	TickQueue(dt, props, anim)

	local did, detail = nil, nil
	-- (a turn right after a swap is the swap's own L3, not a tap)
	local turn = false
	CoopActions.sinceSwap = (CoopActions.sinceSwap or 99) + dt
	for _, e in ipairs((CoopInput and CoopInput.events) or {}) do
		if e.kind == "swap" then CoopActions.sinceSwap = 0 end
		if e.kind == "turn" and CoopActions.sinceSwap > SWAP_QUIET then turn = true end
	end
	if turn then did, detail = "turn", Turn(props, input, anim)
	elseif down.Cross then did, detail = "jump", Jump(props, input)
	elseif down.Square then did, detail = "attack", Attack(props, input)
	elseif down.Up then
		Command(props, "rise")
		CoopActions.riseIn = RISE_REPEAT
		did, detail = "rise", "start"
	elseif down.Triangle then did, detail = "stomp", Move(props, "stomp", anim)
	elseif down.Circle and math.abs(input.x or 0) > PULL_STICK and math.abs(input.x) > math.abs(input.y or 0) then
		local name = input.x > 0 and "pullRight" or "pullLeft"
		did, detail = "pull", Queue(name) and name .. " waiting" or name .. " dropped, " .. QUEUE_MAX .. " waiting"
	elseif down.Circle then did, detail = "action", Action(props, input)
	elseif down.R1 then did, detail = "roar", Move(props, "roar", anim)
	elseif down.L1 then
		did, detail = "threat", Move(props, "threat", anim)
		-- held until L1 is let go (unless the press was dropped: nothing to end then)
		CoopActions.threat = not string.find(detail, "dropped", 1, true)
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
		-- let go before the posture was sent: take it out of the queue instead of ending it
		local q, unqueued = CoopActions.queue, false
		for i = #q, 1, -1 do
			if q[i].name == "threat" then table.remove(q, i) unqueued = true end
		end
		CoopActions.pending = q[1]
		if not unqueued then Command(props, "threatEnd") end
		did, detail = did or "threat", detail or (unqueued and "let go before it started" or "end")
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

-- A single move asked for from elsewhere (steering's turns on the spot): queued like a press,
-- unless one of those moves already waits or is being started. Returns the report detail.
function CoopActions.Request(name)
	local props = Props()
	if not props or not props.currentInfo then return "no Trico" end
	for _, q in ipairs(CoopActions.queue) do
		if q.name == name then return name .. " already waiting" end
	end
	if CoopActions.sent and CoopActions.sent.name == name then return name .. " already sent" end
	local detail = Move(props, name, AnimState(props))
	Say("AC %s requested: %s", name, tostring(detail))
	return detail
end

-- Call once per frame with the Trico player's input (nil when nobody plays Trico).
function CoopActions.Update(dt, input)
	dt = dt or 0
	CoopActions.clock = (CoopActions.clock or 0) + dt
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
	CoopActions.queue = {}
	CoopActions.pending = nil
	CoopActions.sent = nil
	pcall(DropJump, props, "released")
	pcall(DropGesture, props, "released")
	CoopActions.DetachEdgeVolume()
	CoopActions.riseIn = nil
end

CoopActions.Reset()
