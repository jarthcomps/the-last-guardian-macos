dofile(string.match(debug.getinfo(1, "S").source, "^@(.*/)") .. "harness.lua")

Load("game/scripts/coop/coop_debug")
CoopTrico = { keepLastPos = false }   -- the tests put Trico at the origin on purpose
Load("game/scripts/coop/coop_trico")
-- the default goal distances sit beyond Trico's 6.5 m "reached" range (as auto-Trico's do)
check(CoopTrico.distances.min > 6.5 and CoopTrico.distances.turn > 6.5 and CoopTrico.distances.goal >= 14
	and CoopTrico.distances.arrive > 6.5, "goals beyond the arrival range, moved on before it")
-- the tests below use shorter distances, as the first steering did
CoopTrico.distances = { goal = 8.0, min = 4.0, turn = 5.0, behind = 7.0 }

local function near(a, b) return math.abs(a - b) < 1e-3 end

-- stick: dead zone, x/y in -1..1, clamped to 1
check(CoopTrico.Stick(nil) == nil, "no pad")
check(CoopTrico.Stick({ x = 0.2, y = 0 }) == nil, "dead zone")
local sx, sy, len = CoopTrico.Stick({ x = 0, y = 0.5 })
check(near(sx, 0) and near(sy, 0.5) and near(len, 0.5), "half up")
sx, sy, len = CoopTrico.Stick({ x = 1, y = 1 })
check(near(len, 1) and near(sx, sy), "clamped diagonal")

-- goals are camera-relative: forward along +Z, right is -X (looking along +Z)
local p = Vector(10, 2, 20)
local gx, gy, gz, dx, dz = CoopTrico.GoalFor(p, 0, 1, 0, 1, 1, 0, 1)
check(near(gx, 10) and near(gy, 2) and near(gz, 28) and near(dz, 1), "full forward is 8 m ahead")
gx, gy, gz, dx, dz = CoopTrico.GoalFor(p, 0, 1, 1, 0, 0.3, 0, 1)
check(near(gx, 10 - 5.2) and near(gz, 20) and near(dx, -1), "light right")
gx, gy, gz = CoopTrico.GoalFor(p, 0, 1, 0, -1, 1, nil, nil)
check(near(gz, 12), "no camera: Trico's facing is forward, back goes behind")
gx, gy, gz = CoopTrico.GoalFor(p, 0, 1, 0, -1, 1, nil, nil, 0.35)
check(near(gz, 13), "a shortened goal behind Trico stays a body length away")
gx, gy, gz = CoopTrico.GoalFor(p, 0, 1, 1, 0, 1, nil, nil, 0.35)
check(near(gx, 10 - 2.8), "a shortened goal beside Trico is not stretched")
gx, gy, gz = CoopTrico.GoalFor(p, 0, 1, 0, 1, 1, 0, 1, 0.5)
check(near(gz, 24), "scaled goal")

-- camera forward: the game camera's view direction, else camera -> Trico, else nothing
local s45 = math.sqrt(0.5)
Harness.objects._trico_camera = { worldRotation = { X = 0, Y = s45, Z = 0, W = s45 } }
local fx, fz, src = CoopTrico.CameraForward(Vector(0, 0, 0))
check(near(fx, -1) and near(fz, 0) and src == "view", "view along local -Z turned 90 degrees: -X")
Harness.objects._trico_camera = { worldRotation = { X = 0, Y = 1, Z = 0, W = 0 } }
fx, fz, src = CoopTrico.CameraForward(Vector(0, 0, 0))
check(near(fx, 0) and near(fz, 1) and src == "view", "turned around: forward is +Z")
Harness.objects._trico_camera = { worldRotation = { X = s45, Y = 0, Z = 0, W = s45 },
	worldPosition = Vector(0, 3, -8) }
fx, fz, src = CoopTrico.CameraForward(Vector(0, 0, 0))
check(near(fx, 0) and near(fz, 1) and src == "cam", "looking straight down: camera to Trico")
Harness.objects._trico_camera = nil
check(CoopTrico.CameraForward(Vector(0, 0, 0)) == nil, "no camera")

-- steering through the goal strategy (the default)
check(CoopTrico.strategy == "goal", "goal is the default")
Harness.tricoProps = Harness.NewTricoProps(0, 0, 0)
Harness.objects._trico_camera = { worldPosition = Vector(0, 3, -8) }
local pad = { x = 0, y = 1 }
CoopTrico.Update(0.016, pad)
check(CoopTrico.steering, "steering")
local root = Harness.objects.CoopTrico_Goal
check(root and root.class == "SceneRoot", "goal entity created")
local intr = root.children[1]
check(intr.Type == "Interest" and intr.SubType == "intr_AutoTorico", "auto-Trico interest")
check(Harness.objects.CoopTrico_Goal_Interest == intr, "interest named")
check(#Harness.moved == 1 and Harness.moved[1][1] == root and Harness.moved[1][2] == "0,0,8",
	"goal entity moved 8 m ahead")
check(#Harness.CallsNamed("interest") == 1 and Harness.CallsNamed("interest")[1][3] == root,
	"goal registered as an interest")
local ex = Harness.CallsNamed("exclude")
check(#ex == 1 and ex[1][4] == false, "goal not excluded")
local lv = Harness.CallsNamed("level")
check(#lv == 1 and lv[1][4] == "CoopTrico_Steer" and lv[1][5] == "addMax", "top interest level")
check(#Harness.CallsNamed("overwrite") == 0, "approach left to the AI")
check(CoopTrico.forward[3] == "cam*", "forward source kept for the report (latched)")

-- goal refreshes every 0.3 s, not every frame; the entity is reused
CoopTrico.Update(0.1, pad)
check(#Harness.moved == 1, "no refresh yet")
CoopTrico.Update(0.25, pad)
check(#Harness.moved == 2 and #Harness.created == 2, "refreshed with the same entity")
check(#Harness.CallsNamed("interest") == 1 and #Harness.CallsNamed("level") == 2,
	"registered once, level kept")

-- a goal off Trico's navmesh island is shortened
Harness.reachable = function(from, to) return to.Z < 5.5 end
CoopTrico.refreshIn = 0
CoopTrico.goalStick = nil   -- (a fresh goal, not the one held for the same stick way)
CoopTrico.Update(0.016, pad)
check(Harness.moved[#Harness.moved][2] == "0,0,4.8" and CoopTrico.nav == "y0.60", "shortened goal")
Harness.reachable = function() return false end
CoopTrico.refreshIn = 0
CoopTrico.goalStick = nil   -- (a fresh goal, not the one held for the same stick way)
CoopTrico.Update(0.016, pad)
check(Harness.moved[#Harness.moved][2] == "0,0,8" and CoopTrico.nav == "n1.00",
	"nothing reachable: full goal")
Harness.reachable = function() return false, Vector(0, 0, 3) end
CoopTrico.refreshIn = 0
CoopTrico.goalStick = nil   -- (a fresh goal, not the one held for the same stick way)
CoopTrico.Update(0.016, pad)
check(Harness.moved[#Harness.moved][2] == "0,0,3" and CoopTrico.nav == "e1.00",
	"nothing reachable: where the navmesh path ends")
Harness.reachable = function() return false, Vector(0, 0, 1) end
CoopTrico.refreshIn = 0
CoopTrico.goalStick = nil   -- (a fresh goal, not the one held for the same stick way)
CoopTrico.Update(0.016, pad)
check(Harness.moved[#Harness.moved][2] == "0,0,8", "a path end too close is not used")
Harness.reachable = function() return true end

-- the goal stays put while the stick asks for about the same place and Trico is not there yet
Harness.tricoProps.currentInfo.aiPos = Vector(0, 0, 1)
CoopTrico.refreshIn = 0
CoopTrico.Update(0.016, pad)
check(Harness.moved[#Harness.moved][2] == "0,0,8", "goal kept (new one 1 m away)")
Harness.tricoProps.currentInfo.aiPos = Vector(0, 0, 5.5)
CoopTrico.refreshIn = 0
CoopTrico.Update(0.016, pad)
check(Harness.moved[#Harness.moved][2] == "0,0,13.5", "arriving: the goal moves on")
Harness.tricoProps.currentInfo.aiPos = Vector(0, 0, 0)
CoopTrico.refreshIn = 0
CoopTrico.Update(0.016, { x = 1, y = 0 })
check(Harness.moved[#Harness.moved][2] == "-8,0,0", "new direction: the goal moves")
-- a wobbling thumb (20 degrees either way) keeps the goal where it was set
local wobbleMoves = #Harness.moved
for i = 1, 10 do
	CoopTrico.refreshIn = 0
	local a = math.rad(90 + (i % 2 == 0 and 20 or -20))
	CoopTrico.Update(0.016, { x = math.sin(a), y = math.cos(a) })
	check(Harness.moved[#Harness.moved][2] == "-8,0,0", "wobble " .. i .. ": goal held")
end

-- release: the goal loses its level and is excluded, Trico is asked to stop, once
CoopTrico.Update(0.016, { x = 0, y = 0 })
check(not CoopTrico.steering and not CoopTrico.goalLive, "released")
check(#Harness.CallsNamed("levelReset") == 1, "level taken away")
ex = Harness.CallsNamed("exclude")
check(ex[#ex][4] == true, "goal excluded")
check(#Harness.CallsNamed("stop") == 1 and #Harness.CallsNamed("reset") == 0, "stop only")
CoopTrico.Update(0.016, nil)
check(#Harness.CallsNamed("stop") == 1, "released only once")

-- steering again brings the goal back in
CoopTrico.Update(0.016, pad)
check(#Harness.CallsNamed("interest") == 2 and Harness.CallsNamed("exclude")[3][4] == false,
	"goal live again")
CoopTrico.Release()

-- a new world (the entity is gone) makes a new goal entity
Harness.objects.CoopTrico_Goal = nil
CoopTrico.Update(0.016, pad)
check(#Harness.created == 4 and Harness.objects.CoopTrico_Goal ~= root, "goal entity remade")
CoopTrico.Release()

-- interest strategy: the instruction proxy, an instruction and the approach overwrite
CoopTrico.strategy = "interest"
Harness.objects.PlayerAi2Instruction_InstProxyEntity = { name = "proxy" }
local moved = #Harness.moved
CoopTrico.Update(0.016, pad)
check(Harness.moved[moved + 1][1].name == "proxy", "proxy moved to the goal")
check(#Harness.CallsNamed("instruction") == 1, "instruction")
check(#Harness.CallsNamed("overwrite") == 1, "approach overwritten")
local ow = Harness.CallsNamed("overwrite")[1][4]
check(ow.condition() == true and near(ow.destination.Z, 8), "overwrite holds while steering")
CoopTrico.Update(0.016, { x = 0, y = 0 })
check(not ow.condition() and #Harness.CallsNamed("reset") == 1, "instruction levels reset")

-- forward strategy
CoopTrico.strategy = "forward"
CoopTrico.Update(0.016, pad)
local fw = Harness.CallsNamed("forward")
check(#fw == 1 and fw[1][3].isFreeMove and near(fw[1][3].goalPos.Z, 8), "forward request")
check(near(Harness.tricoProps.currentInfo.apDestination.Z, 8), "approach destination set")
CoopTrico.Release()

-- turn start: a goal far round from Trico's facing becomes one to the side
local tg, td = CoopTrico.TurnGoal(Vector(0, 0, 0), 0, 1, 0, -1)
check(tg and near(math.abs(tg.X), 5) and near(tg.Z, 0) and near(math.abs(td.X), 1), "straight behind: a side goal")
tg = CoopTrico.TurnGoal(Vector(0, 0, 0), 0, 1, -0.6, -0.8)
check(tg and near(tg.X, -5), "behind on the right (-X): turn right")
tg = CoopTrico.TurnGoal(Vector(0, 0, 0), 0, 1, 0.6, -0.8)
check(tg and near(tg.X, 5), "behind on the left (+X): turn left")
check(CoopTrico.TurnGoal(Vector(0, 0, 0), 0, 1, 1, 0) == nil, "90 degrees: no turn start")
check(CoopTrico.TurnGoal(Vector(0, 0, 0), 0, 1, 1, 0, true) ~= nil, "a started turn goes on past 90")
check(CoopTrico.TurnGoal(Vector(0, 0, 0), 0, 1, 0.6, 0.8, true) == nil, "within 60 degrees: done")

-- steer: the goal interest plus the approach overwrite, with the turn start
CoopTrico.strategy = "steer"
Harness.calls = {}
Harness.tricoProps = Harness.NewTricoProps(0, 0, 0)   -- facing +Z
Harness.objects._trico_camera = { worldPosition = Vector(0, 3, -8) }  -- forward is +Z
-- standing: turns on the spot at once
CoopTrico.Update(0.016, { x = 0, y = -1 })  -- back: straight behind Trico
check(CoopTrico.turning and CoopTrico.nav == "spot-aboutRight" and #Harness.CallsNamed("slide") == 1, "standing: turns on the spot")
CoopTrico.Update(0.016, { x = 0, y = 0 })
Harness.calls = {}
-- walking, with the old side goals on (CoopTrico.sideTurns): curves round through a side goal
CoopTrico.sideTurns = true
Harness.tricoProps.plannedMove = "MoveForward"
CoopTrico.Update(0.016, { x = 0, y = -1 })
check(CoopTrico.turning and CoopTrico.nav == "turn", "walking: turn started")
check(near(math.abs(CoopTrico.goal.X), 5) and near(CoopTrico.goal.Z, 0), "side goal")
check(#Harness.CallsNamed("level") == 1 and #Harness.CallsNamed("overwrite") == 1, "interest and overwrite")
ow = Harness.CallsNamed("overwrite")[1][4]
check(near(math.abs(ow.destination.X), 5) and ow.condition(), "overwrite on the side goal")
-- Trico has turned round: the real goal, 8 m behind where it started
Harness.tricoProps.currentInfo.ai2Ext.dirXZ = Vector(0, 0, -1)
CoopTrico.refreshIn = 0
CoopTrico.Update(0.016, { x = 0, y = -1 })
check(not CoopTrico.turning and near(CoopTrico.goal.Z, -8) and CoopTrico.nav == "y1.00", "real goal after the turn")
CoopTrico.Update(0.016, { x = 0, y = 0 })
check(not ow.condition() and not CoopTrico.turning, "released")
Harness.tricoProps.currentInfo.ai2Ext.dirXZ = Vector(0, 0, 1)
CoopTrico.sideTurns = false
-- by default no side goal: walking, the goal goes straight in the stick's way (behind him) and
-- his own approach turns him
Harness.calls = {}
Harness.tricoProps.plannedMove = "MoveForward"
CoopTrico.Update(0.016, { x = 0, y = -1 })
check(not CoopTrico.turning and CoopTrico.goal and near(CoopTrico.goal.Z, -8) and #Harness.CallsNamed("slide") == 0,
	"walking: the goal goes straight behind, no side goal, no spot turn")
CoopTrico.Update(0.016, { x = 0, y = 0 })
Harness.tricoProps.plannedMove = nil

-- steer (with CoopTrico.kick on) kicks a standing Trico off with a forward move to the goal;
-- not while he already moves
check(CoopTrico.kick == false, "the kick is off by default")
CoopTrico.kick = true
CoopTrico.strategy = "steer"
Harness.calls = {}
Harness.tricoProps.plannedMove = nil
CoopTrico.Update(0.016, { x = 0, y = 1 })
local kick = Harness.CallsNamed("forward")
check(#kick == 1 and kick[1][3].isFreeMove and near(kick[1][3].goalPos.Z, 8)
	and near(Harness.tricoProps.currentInfo.apDestination.Z, 8), "standing: kicked off towards the goal")
Harness.tricoProps.plannedMove = "MoveForward"
CoopTrico.refreshIn = 0
CoopTrico.Update(0.016, { x = 0, y = 1 })
check(#Harness.CallsNamed("forward") == 1, "already moving: no kick")
Harness.tricoProps.plannedMove = nil
Harness.tricoProps.isInjuredMovementMode = true
CoopTrico.refreshIn = 0
CoopTrico.Update(0.016, { x = 0, y = 1 })
check(#Harness.CallsNamed("forward") == 1, "injured: no free move")
Harness.tricoProps.isInjuredMovementMode = nil
CoopTrico.Update(0.016, { x = 0, y = 0 })
CoopTrico.kick = false

-- on a narrow beam the side goal is off the navmesh: Trico turns on the spot instead
Harness.reachable = function(from, to) return math.abs(to.X) < 1 end   -- 2 m wide, along Z
Harness.calls = {}
CoopTrico.turnIn = 0
CoopTrico.Update(0.016, { x = 0, y = -1 })
local spot = Harness.CallsNamed("slide")
check(#spot == 1 and near(spot[1][3].destinationDir.Z, -1) and CoopTrico.nav == "spot-aboutRight"
	and #Harness.CallsNamed("level") == 0, "about-turn on the spot (his own slide, facing back), no goal")
CoopTrico.refreshIn = 0
CoopTrico.Update(0.016, { x = 0, y = -1 })
check(#Harness.CallsNamed("slide") == 1, "not asked again within 1.5 s")
for _ = 1, 21 do CoopTrico.Update(0.1, { x = 0, y = -1 }) end
check(#Harness.CallsNamed("slide") == 1, "one spot turn per stick direction (his facing reads stale after one)")
CoopTrico.Update(0.016, { x = 0, y = 0 })
CoopTrico.turnIn = 0
CoopTrico.Update(0.016, { x = 0, y = -1 })
check(#Harness.CallsNamed("slide") == 2, "let go and pushed again: another")
Harness.tricoProps.currentInfo.ai2Ext.dirXZ = Vector(0, 0, -1)
CoopTrico.refreshIn = 0
CoopTrico.Update(0.016, { x = 0, y = -1 })
check(not CoopTrico.turning and near(CoopTrico.goal.Z, -8) and #Harness.CallsNamed("slide") == 2, "turned: walks along the beam")
CoopTrico.Update(0.016, { x = 0, y = 0 })
Harness.tricoProps.currentInfo.ai2Ext.dirXZ = Vector(0, 0, 1)
Harness.reachable = function(from, to) return true end
-- the spot turn is his own slide, one per stick way, never a second while one plays
CoopTrico.turnIn = 0
Harness.calls = {}
Harness.reachable = function(from, to) return math.abs(to.X) < 1 end
CoopTrico.Update(0.016, { x = 0, y = -1 })
check(#Harness.CallsNamed("slide") == 1, "the spot turn is his own slide")
local savedAnim = Ai2.Utils.GetCurAnimState
Ai2.Utils.GetCurAnimState = function() return "c_slide_rot_right" end
for _ = 1, 30 do CoopTrico.Update(0.1, { x = 0, y = -1 }) end
check(#Harness.CallsNamed("slide") == 1, "no second turn while one plays")
Ai2.Utils.GetCurAnimState = function() return "c_stand" end
for _ = 1, 6 do CoopTrico.Update(0.1, { x = 0, y = -1 }) end
check(#Harness.CallsNamed("slide") == 1, "after it, the same stick way: no second turn")
CoopTrico.Update(0.016, { x = 0, y = 0 })
CoopTrico.turnIn = 0
CoopTrico.Update(0.016, { x = 0, y = -1 })
check(#Harness.CallsNamed("slide") == 2, "pushed again: another")
Ai2.Utils.GetCurAnimState = savedAnim
CoopTrico.Update(0.016, { x = 0, y = 0 })
Harness.reachable = function(from, to) return true end
-- during a turn on the spot the AI's position reads 0,0,0: the character's stands in for it
CharacterMngr = { GetTrico = function() return { worldPosition = Vector(5, 1, 6) } end }
local zero = { currentInfo = { aiPos = Vector(0, 0, 0) } }
local at = CoopTrico.Position(zero)
check(at and near(at.X, 5) and near(at.Z, 6), "zero AI position: the character's")
check(near(CoopTrico.Position({ currentInfo = { aiPos = Vector(2, 0, 3) } }).X, 2), "a real AI position is kept")
CharacterMngr = nil
check(CoopTrico.Position(zero).X == 0, "no real position ever for this Trico: the AI's as it is")
CoopTrico.keepLastPos = true
local same = { currentInfo = { aiPos = Vector(2, 0, 3) } }
CoopTrico.Position(same)
same.currentInfo.aiPos = Vector(0, 0, 0)
check(near(CoopTrico.Position(same).X, 2), "all zero: this Trico's last real position")
-- a slide reads near the origin for a while: not believed, unless it lasts 3 s
CoopTrico.lastPos = nil
same.currentInfo.aiPos = Vector(-70, 7, -80)
CoopTrico.Position(same, 0.1)
same.currentInfo.aiPos = Vector(-0.6, 7, -0.6)
check(near(CoopTrico.Position(same, 0.5).X, -70), "a 100 m jump is not believed")
check(near(CoopTrico.Position(same, 2.0).X, -70), "still not after 2.5 s")
check(near(CoopTrico.Position(same, 1.0).X, -0.6), "after 3 s it is")
CoopTrico.keepLastPos = false
local mineProps = { currentInfo = { aiPos = Vector(0, 0, 0), work = { GetMySelfPos = Vector(7, 1, 8) } } }
check(near(CoopTrico.Position(mineProps).X, 7), "the entity position comes first")
-- a goal off a narrow beam snaps onto it (the nearest walkable point), if far enough to walk to
Harness.reachable = function(from, to) return math.abs(to.X) < 1 end   -- 2 m wide, along Z
local savedNearest = Ai2FindNearestPosition
Ai2FindNearestPosition = function(ai, pos, ext) return { found = 1, pos = Vector(0, 0.5, pos.Z) } end
CoopTrico.strategy = "goal"
Harness.objects._trico_camera = { worldPosition = Vector(0, 3, -8) }   -- forward +Z
Harness.tricoProps.currentInfo.ai2Ext.dirXZ = Vector(0, 0, 1)
CoopTrico.Update(0.016, { x = 0.6, y = 0.8 })   -- forward and to the right: off the beam
check(CoopTrico.goal and near(CoopTrico.goal.X, 0) and CoopTrico.goal.Z > 4 and CoopTrico.nav == "s1.00",
	"snapped onto the beam: " .. tostring(CoopTrico.nav))
CoopTrico.Update(0.016, { x = 0, y = 0 })
Ai2FindNearestPosition = savedNearest
Harness.reachable = function(from, to) return true end
-- while the Trico player gives input, the boy's interest is lowered for a while (it lapses by
-- itself), refreshed twice a second, and left alone 2 s after the last input
Harness.objects.Player = { _object_name = "Player" }
Harness.calls = {}
CoopTrico.focusIn, CoopTrico.idleFor = 0, 99
local function focusCalls()
	local n = 0
	for _, c in ipairs(Harness.CallsNamed("level")) do
		if c[4] == "CoopTrico_Focus" then
			n = n + 1
			check(c[3] == Harness.objects.Player and c[5] == "subMax" and c[6] == 2.5, "the boy's interest, subMax, 2.5 s")
		end
	end
	return n
end
CoopTrico.Update(0.016, { x = 0, y = 1 })
check(focusCalls() == 1 and CoopTrico.focused, "stick: the boy's interest lowered")
for _ = 1, 12 do CoopTrico.Update(0.1, { x = 0, y = 0, hold = { Cross = true } }) end
check(focusCalls() == 3, "a held button keeps it, refreshed twice a second: " .. focusCalls())
CoopTrico.Update(0.016, { x = 0, y = 0 })
for _ = 1, 25 do CoopTrico.Update(0.1, { x = 0, y = 0 }) end
local after = focusCalls()
for _ = 1, 20 do CoopTrico.Update(0.1, { x = 0, y = 0 }) end
check(focusCalls() == after and not CoopTrico.focused, "2 s without input: left alone")
CoopTrico.Update(0.1, nil)
check(focusCalls() == after, "nobody playing Trico: nothing")
Harness.objects.Player = nil
-- no turning loop: with the way 120 degrees off, one quarter turn, then he walks the last 30
-- degrees round instead of being turned again (an about-turn that overshoots is the same: under
-- 100 degrees off, only walking)
CoopTrico.strategy = "steer"
Harness.calls = {}
CoopTrico.turnIn = 0
Harness.tricoProps.plannedMove = nil
Harness.tricoProps.currentInfo.ai2Ext.dirXZ = Vector(0, 0, 1)
local wx, wz = math.sin(math.rad(120)), math.cos(math.rad(120))   -- 120 degrees to the left (+X side)
Harness.objects._trico_camera = { worldPosition = Vector(0, 3, -8) }   -- forward +Z: stick = way
CoopTrico.latch = nil
CoopTrico.Update(0.016, { x = -wx, y = wz })   -- the stick's right is -X
local turns = Harness.CallsNamed("slide")
check(#turns == 1 and near(turns[1][3].destinationDir.X, wx) and near(turns[1][3].destinationDir.Z, wz),
	"120 degrees: one turn, his own slide to face the stick's way")
-- after it he faces 30 degrees short of the way
local fx2, fz2 = math.sin(math.rad(90)), math.cos(math.rad(90))
Harness.tricoProps.currentInfo.ai2Ext.dirXZ = Vector(fx2, 0, fz2)
for _ = 1, 40 do CoopTrico.Update(0.1, { x = -wx, y = wz }) end
check(#Harness.CallsNamed("slide") == 1 and CoopTrico.goal ~= nil, "no more turns: he walks the rest")
-- and an overshoot (60 degrees off on the other side) isn't turned back either
Harness.tricoProps.currentInfo.ai2Ext.dirXZ = Vector(math.sin(math.rad(-180)), 0, math.cos(math.rad(-180)))
CoopTrico.turnIn = 0
for _ = 1, 40 do CoopTrico.Update(0.1, { x = -wx, y = wz }) end
check(#Harness.CallsNamed("slide") == 1, "60 degrees off: walked, not turned")
CoopTrico.Update(0.016, { x = 0, y = 0 })
Harness.tricoProps.currentInfo.ai2Ext.dirXZ = Vector(0, 0, 1)
-- while focused, Trico's idle and boy-affection behaviours and jump slides are banned; only the
-- bans that were off are set, and only those are cleared again
local bw = { IdlingBan = false, SitIdlingBan = true, BoyDaisukiProcBan = false }
Harness.tricoProps = Harness.NewTricoProps(0, 0, 0)
Harness.tricoProps.currentInfo.work = bw
Harness.tricoProps.moveMngr.prioSelect = false
CoopTrico.idleFor = 99
CoopTrico.Update(0.016, { x = 0, y = 1 })
check(bw.IdlingBan and bw.SitIdlingBan and bw.BoyDaisukiProcBan and Harness.tricoProps.moveMngr.prioSelect,
	"focused: idling, affection and jump slides banned")
CoopTrico.Update(0.016, { x = 0, y = 0 })
for _ = 1, 25 do CoopTrico.Update(0.1, { x = 0, y = 0 }) end
check(not CoopTrico.focused and bw.IdlingBan, "2 s later: the boy back in mind, idle fidgets still banned")
for _ = 1, 130 do CoopTrico.Update(0.1, { x = 0, y = 0 }) end
check(not bw.IdlingBan and bw.SitIdlingBan and not bw.BoyDaisukiProcBan and not Harness.tricoProps.moveMngr.prioSelect,
	"15 s later: ours lifted, the story's SitIdlingBan kept")
CoopTrico.Update(0.016, { x = 0, y = 1 })
CoopTrico.Release()
check(not bw.IdlingBan and not CoopTrico.banned, "released: lifted")
-- sitting: the stick first stands him up (sit_end), every 2 s at most
local savedAnim2 = Ai2.Utils.GetCurAnimState
Ai2.Utils.GetCurAnimState = function() return "ext_c_sit_loop" end
Harness.calls = {}
CoopTrico.standIn = 0
CoopTrico.Update(0.016, { x = 0, y = 1 })
local ends = Harness.CallsNamed("cmd")
check(#ends == 1 and ends[1][3] == "ControlCommand_C_sit_end" and CoopTrico.nav == "stand-up", "sitting: stood up first")
for _ = 1, 4 do CoopTrico.refreshIn = 0 CoopTrico.Update(0.016, { x = 0, y = 1 }) end
check(#Harness.CallsNamed("cmd") == 1, "not asked again at once")
-- lying: his stand command
Ai2.Utils.GetCurAnimState = function() return "ext_c_lie_down_loop" end
Harness.calls = {}
CoopTrico.standIn = 0
CoopTrico.refreshIn = 0
CoopTrico.Update(0.016, { x = 0, y = 1 })
check(Harness.CallsNamed("cmd")[1] and Harness.CallsNamed("cmd")[1][3] == "ControlCommand_C_stand", "lying: stood up with his stand command")
-- getting up (an end animation) is left to play
Ai2.Utils.GetCurAnimState = function() return "ext_c_sit_end" end
Harness.calls = {}
CoopTrico.standIn = 0
CoopTrico.refreshIn = 0
CoopTrico.Update(0.016, { x = 0, y = 1 })
check(#Harness.CallsNamed("cmd") == 0, "getting up: nothing sent")
Ai2.Utils.GetCurAnimState = savedAnim2
CoopTrico.Update(0.016, { x = 0, y = 0 })
-- which turn: the right of a facing (x, z) is (-z, x)
check(CoopTrico.TurnCommand(0, 1, -1, 0) == "turn90Right" and CoopTrico.TurnCommand(0, 1, 1, 0) == "turn90Left",
	"quarter turns")
check(CoopTrico.TurnCommand(0, 1, 0.3, -0.95) == "aboutLeft" and CoopTrico.TurnCommand(0, 1, -0.3, -0.95) == "aboutRight",
	"about-turns, on the stick's side")

-- the camera's direction is latched while the stick is held
CoopTrico.strategy = "goal"
Harness.objects._trico_camera = { worldPosition = Vector(0, 3, -8) }   -- forward +Z
CoopTrico.Update(0.016, { x = 0, y = 1 })
check(near(CoopTrico.goal.Z, 8), "forward is the camera's")
Harness.objects._trico_camera = { worldPosition = Vector(-8, 3, 0) }   -- the camera swings: forward +X
CoopTrico.refreshIn = 0
CoopTrico.Update(0.016, { x = 0.2, y = 1 })
check(near(CoopTrico.goal.X, 0, 2) and CoopTrico.goal.Z > 7, "same thumb, same way while held")
CoopTrico.refreshIn = 0
CoopTrico.Update(0.016, { x = 1, y = 0 })    -- the stick swings round 90 degrees: read again
check(CoopTrico.forward[1] > 0.9, "a big swing reads the camera again")
CoopTrico.Update(0.016, { x = 0, y = 0 })
check(CoopTrico.latch == nil, "let go: latch cleared")
CoopTrico.Update(0.016, { x = 0, y = 1 })
check(CoopTrico.goal.X > 7, "pressed again: the camera as it is now")
CoopTrico.Update(0.016, { x = 0, y = 0 })

-- Trico-relative: up is the way Trico faces, whatever the camera
CoopTrico.relative = "trico"
Harness.tricoProps.currentInfo.ai2Ext.dirXZ = Vector(0, 0, -1)
CoopTrico.Update(0.016, { x = 0, y = 1 })
check(near(CoopTrico.goal.Z, -8) and CoopTrico.forward[3] == "trico", "Trico-relative forward")
CoopTrico.Update(0.016, { x = 0, y = 0 })
CoopTrico.relative = "camera"
Harness.tricoProps.currentInfo.ai2Ext.dirXZ = Vector(0, 0, 1)
Harness.objects._trico_camera = { worldPosition = Vector(0, 3, -8) }

-- a queued move holds steering for its first 1.5 s only
CoopActions = { queueTime = 8, pending = { name = "stomp", timer = 8 } }
CoopTrico.Update(0.016, { x = 0, y = 1 })
check(not CoopTrico.steering, "a fresh queued move: steering waits")
CoopActions.pending.timer = 6.4
CoopTrico.Update(0.016, { x = 0, y = 1 })
check(CoopTrico.steering, "after 1.5 s the stick wins")
CoopTrico.Update(0.016, { x = 0, y = 0 })
CoopActions = nil

-- "ctrl": his planning off, a planned move of our own (none at all makes his action layer stop
-- him every frame), walk commands, and both put back when the stick is let go
CoopTrico.strategy = "ctrl"
Harness.tricoProps = Harness.NewTricoProps(0, 0, 0)
Harness.tricoProps.plannedMove = "MoveStop"
Harness.calls = {}
-- (no path functions in the harness yet: the walk commands, which at least start him)
CoopTrico.Update(0.016, { x = 0, y = 1 })
check(Harness.tricoProps.approach.skipCreate == true, "ctrl: his planning skipped")
check(Harness.tricoProps.plannedMove == "CoopManual", "ctrl: a planned move his actions leave alone")
local cmds = Harness.CallsNamed("cmd")
check(#cmds == 1 and cmds[1][3] == "ControlCommand_C_walk_fast" and CoopTrico.nav == "ctrl-cmd",
	"ctrl without a path: full stick walks fast")
-- with the free walk's path: the pose 25 m ahead (full stick), his own forward move in manual mode
local built = {}
Harness.tricoProps.currentInfo.detailPath = { Navmesh = 1, Interval = 1 }
Ai2.CondorUtils.GetPath = function() return built end
Ai2.CondorUtils.Path = { ReqDiscretePathMode = Harness.Record("discrete") }
Ai2.Actions = { Condor = { Path = { Pack = {} } } }
Ai2BuildPath = Harness.Record("build")
Harness.tricoProps.moveMngr.walk = { suspendStopDecision = false }
Harness.calls = {}
CoopTrico.Update(0.016, { x = 0, y = 1 })
check(Harness.tricoProps.moveMngr.walk.suspendStopDecision == true, "ctrl: his stop decision suspended while held")
local types = Harness.tricoProps.curMoveTypes
check(types and types[#types] == "MV_C_WALKFAST", "ctrl: full stick allows the fast walk")
check(#CoopTrico.CtrlTypes(0.4) == 2 and CoopTrico.CtrlTypes(0.4)[2] == "MV_C_WALKSLOW", "ctrl: a light push walks slowly")
check(Harness.tricoProps.plannedMove == "ForwardMove" and Harness.tricoProps.moveParam.manualMode
	and CoopTrico.nav == "ctrl-walk", "ctrl: his forward move, manual")
check(built.BasePos and near(built.BasePos.Z, 25) and near(built.BaseDir.Z, 1) and #Harness.CallsNamed("build") == 1,
	"ctrl: the pose 25 m ahead")
check(#Harness.CallsNamed("cmd") == 0, "ctrl: no walk commands while the path works")
-- still standing half a second into the walk (after a sit): his own forward request, once a second
local savedAnimK = Ai2.Utils.GetCurAnimState
Ai2.Utils.GetCurAnimState = function() return "c_stand" end
CoopTrico.ctrlStill, CoopTrico.kickIn = 0, 0
Harness.calls = {}
for _ = 1, 3 do CoopTrico.Update(0.1, { x = 0, y = 1 }) end
check(#Harness.CallsNamed("forward") == 0, "ctrl: no kick at once")
for _ = 1, 3 do CoopTrico.Update(0.1, { x = 0, y = 1 }) end
local fw = Harness.CallsNamed("forward")
check(#fw == 1 and fw[1][3] == CoopTrico.manualMove, "ctrl: kicked with his forward request")
for _ = 1, 5 do CoopTrico.Update(0.1, { x = 0, y = 1 }) end
check(#Harness.CallsNamed("forward") == 1, "ctrl: not again within a second")
Ai2.Utils.GetCurAnimState = function() return "c_walkC" end
for _ = 1, 20 do CoopTrico.Update(0.1, { x = 0, y = 1 }) end
check(#Harness.CallsNamed("forward") == 2 - 1, "ctrl: walking: no kicks")
-- a stale request of his (a slide taken by his sitting shuffle) holding a turn up: dropped after
-- 1.5 s standing, and the turn asked again
Ai2.Utils.GetCurAnimState = function() return "c_stand" end
local stale = { name = "slide" }
Harness.tricoProps.moveMngr.runningRequest = stale
Harness.tricoProps.moveMngr.CancelPlannedMove = function(self, props) props.plannedMove = nil props.moveParam = nil end
CoopTrico.ctrlStill, CoopTrico.unstickIn = 0, 0
Harness.calls = {}
for _ = 1, 10 do CoopTrico.Update(0.1, { x = 0, y = -1 }) end
check(Harness.tricoProps.moveMngr.runningRequest == stale, "ctrl: not dropped within 1.5 s")
for _ = 1, 7 do CoopTrico.Update(0.1, { x = 0, y = -1 }) end
check(Harness.tricoProps.moveMngr.runningRequest == nil and CoopTrico.unsticks >= 1, "ctrl: stale request dropped")
check(#Harness.CallsNamed("slide") >= 2, "ctrl: the turn asked again")
Ai2.Utils.GetCurAnimState = savedAnimK
-- the stick behind him: a turning slide, his destination moved ahead the stick's way (on it he
-- counts as arrived and his slide does nothing)
Harness.tricoProps.isOnGoalInVolume = true
Harness.calls = {}
CoopTrico.turnIn = 0
CoopTrico.Update(0.016, { x = 0, y = -1 })
local dest = Harness.tricoProps.currentInfo.apDestination
check(#Harness.CallsNamed("slide") == 1 and dest and near(dest.Z, -20) and not Harness.tricoProps.isOnGoalInVolume,
	"ctrl: turned, destination ahead the stick's way")
CoopTrico.Update(0.016, { x = 0, y = 1 })
Harness.tricoProps.plannedMove = "MoveTurn"
Harness.tricoProps.moveParam = nil
Ai2BuildPath = nil
CoopTrico.Update(0.016, { x = 0, y = 1 })
check(Harness.tricoProps.plannedMove == "MoveTurn", "ctrl: a move of his own is not overwritten")
Ai2BuildPath = Harness.Record("build")
Harness.tricoProps.plannedMove = "ForwardMove"
Harness.tricoProps.moveParam = CoopTrico.manualMove
CoopTrico.Update(0.016, { x = 0, y = 0 })
check(not Harness.tricoProps.approach.skipCreate, "ctrl: planning back on release")
check(Harness.tricoProps.plannedMove == nil and Harness.tricoProps.moveParam == nil, "ctrl: our forward move dropped")
check(Harness.tricoProps.moveMngr.walk.suspendStopDecision == false, "ctrl: his stop decision back on release")
Ai2BuildPath, Ai2.CondorUtils.GetPath, Ai2.CondorUtils.Path, Ai2.Actions = nil, nil, nil, nil
Harness.tricoProps.plannedMove = "CoopManual"
CoopTrico.Unskip(Harness.tricoProps)
check(Harness.tricoProps.plannedMove == "CoopManual", "ctrl: unskip only once")
Harness.tricoProps.plannedMove = nil
CoopTrico.strategy = "goal"
CoopTrico.Update(0.016, { x = 0, y = 1 })
CoopTrico.Update(0.016, { x = 0, y = 0 })

-- errors in the game's AI are contained and reported once (with the debug channel on)
CoopDebug.enabled = true
CoopTrico.strategy = "interest"
Harness.objects.PlayerAi2Instruction_InstProxyEntity = nil
CoopTrico.Update(0.016, pad)
CoopTrico.refreshIn = 0
CoopTrico.Update(0.016, pad)
CoopDebug.Flush()
check(#Harness.Find("^TR error") == 1, "error reported once")

-- reports twice a second: Trico's state, then stage, boy, camera and forward
CoopTrico.Release()
CoopTrico.strategy = "goal"
Harness.lightbar = {}
Harness.tricoProps.isInjuredMovementMode = true
CoopTrico.Update(0.5, nil)
CoopDebug.Flush()
local tr = Harness.Find("^TR ")
check(#tr == 1 and string.find(tr[1][2], "TR goal off p=0.0,0.0,0.0 g=- pm=nil rq=nil in=nil it=nil md=nil an=", 1, true) == 1,
	"report line: " .. tostring(tr[1] and tr[1][2]))
local ts = Harness.Find("^TS ")
check(#ts == 1, "second report line")
check(string.find(ts[1][2], "st=s05 inj=true boy=1.0,0.0,-2.0 cam=0.0,3.0,-8.0 f=0.00,1.00,cam", 1,
	true), "stage, injured, boy, camera and forward: " .. ts[1][2])
check(string.find(ts[1][2], "dir=0.00,1.00$"), "Trico's facing: " .. ts[1][2])

-- no Trico yet (title screen): nothing happens
Harness.tricoProps = nil
CoopTrico.Update(0.016, pad)
print("ok")
