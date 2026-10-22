dofile(string.match(debug.getinfo(1, "S").source, "^@(.*/)") .. "harness.lua")

Load("game/scripts/coop/coop_debug")
Load("game/scripts/coop/coop_trico")
-- the default goal distances sit beyond Trico's 6.5 m "reached" range (as auto-Trico's do)
check(CoopTrico.distances.min > 6.5 and CoopTrico.distances.turn > 6.5 and CoopTrico.distances.goal >= 14,
	"goals beyond the arrival range")
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
CoopTrico.Update(0.016, pad)
check(Harness.moved[#Harness.moved][2] == "0,0,4.8" and CoopTrico.nav == "y0.60", "shortened goal")
Harness.reachable = function() return false end
CoopTrico.refreshIn = 0
CoopTrico.Update(0.016, pad)
check(Harness.moved[#Harness.moved][2] == "0,0,8" and CoopTrico.nav == "n1.00",
	"nothing reachable: full goal")
Harness.reachable = function() return false, Vector(0, 0, 3) end
CoopTrico.refreshIn = 0
CoopTrico.Update(0.016, pad)
check(Harness.moved[#Harness.moved][2] == "0,0,3" and CoopTrico.nav == "e1.00",
	"nothing reachable: where the navmesh path ends")
Harness.reachable = function() return false, Vector(0, 0, 1) end
CoopTrico.refreshIn = 0
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
check(CoopTrico.turning and CoopTrico.nav == "spot-aboutRight" and #Harness.CallsNamed("cmd") == 1, "standing: turns on the spot")
CoopTrico.Update(0.016, { x = 0, y = 0 })
Harness.calls = {}
-- walking: curves round through a side goal
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

-- steer kicks a standing Trico off with a forward move to the goal; not while he already moves
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

-- on a narrow beam the side goal is off the navmesh: Trico turns on the spot instead
Harness.reachable = function(from, to) return math.abs(to.X) < 1 end   -- 2 m wide, along Z
Harness.calls = {}
CoopTrico.turnIn = 0
CoopTrico.Update(0.016, { x = 0, y = -1 })
local spot = Harness.CallsNamed("cmd")
check(#spot == 1 and spot[1][3] == "ControlCommand_C_turn_narrow_right" and CoopTrico.nav == "spot-aboutRight"
	and #Harness.CallsNamed("level") == 0, "about-turn on the spot, no goal: " .. tostring(spot[1] and spot[1][3]))
CoopTrico.refreshIn = 0
CoopTrico.Update(0.016, { x = 0, y = -1 })
check(#Harness.CallsNamed("cmd") == 1, "not asked again within 1.5 s")
for _ = 1, 16 do CoopTrico.Update(0.1, { x = 0, y = -1 }) end
check(#Harness.CallsNamed("cmd") == 2, "asked again while Trico still faces away")
Harness.tricoProps.currentInfo.ai2Ext.dirXZ = Vector(0, 0, -1)
CoopTrico.refreshIn = 0
CoopTrico.Update(0.016, { x = 0, y = -1 })
check(not CoopTrico.turning and near(CoopTrico.goal.Z, -8) and #Harness.CallsNamed("cmd") == 2, "turned: walks along the beam")
CoopTrico.Update(0.016, { x = 0, y = 0 })
Harness.tricoProps.currentInfo.ai2Ext.dirXZ = Vector(0, 0, 1)
Harness.reachable = function(from, to) return true end
-- with the action queue there, the spot turn goes through it (stood up first, watched, resent)
local requested
CoopActions = { queueTime = 8, Request = function(name) requested = name end }
CoopTrico.turnIn = 0
Harness.calls = {}
Harness.reachable = function(from, to) return math.abs(to.X) < 1 end
CoopTrico.Update(0.016, { x = 0, y = -1 })
check(requested == "aboutRight" and #Harness.CallsNamed("cmd") == 0, "the spot turn is a queued move")
-- while the turn plays, no second one (it would turn him back)
requested = nil
local savedAnim = Ai2.Utils.GetCurAnimState
Ai2.Utils.GetCurAnimState = function() return "c_turn_narrow_right" end
for _ = 1, 30 do CoopTrico.Update(0.1, { x = 0, y = -1 }) end
check(requested == nil, "no second turn while one plays")
Ai2.Utils.GetCurAnimState = function() return "c_stand" end
for _ = 1, 6 do CoopTrico.Update(0.1, { x = 0, y = -1 }) end
check(requested == "aboutRight", "after it, still facing away: another")
Ai2.Utils.GetCurAnimState = savedAnim
CoopTrico.Update(0.016, { x = 0, y = 0 })
CoopActions = nil
Harness.reachable = function(from, to) return true end
-- during a turn on the spot the AI's position reads 0,0,0: the character's stands in for it
CharacterMngr = { GetTrico = function() return { worldPosition = Vector(5, 1, 6) } end }
local zero = { currentInfo = { aiPos = Vector(0, 0, 0) } }
local at = CoopTrico.Position(zero)
check(at and near(at.X, 5) and near(at.Z, 6), "zero AI position: the character's")
check(near(CoopTrico.Position({ currentInfo = { aiPos = Vector(2, 0, 3) } }).X, 2), "a real AI position is kept")
CharacterMngr = nil
check(CoopTrico.Position(zero).X == 0, "no character: the AI's as it is")
local mineProps = { currentInfo = { aiPos = Vector(0, 0, 0), work = { GetMySelfPos = Vector(7, 1, 8) } } }
check(near(CoopTrico.Position(mineProps).X, 7), "the entity position comes first")
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
