dofile(string.match(debug.getinfo(1, "S").source, "^@(.*/)") .. "harness.lua")

Load("game/scripts/coop/coop_debug")
Load("game/scripts/coop/coop_trico")

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
local gx, gy, gz, dx, dz = CoopTrico.GoalFor(p, 1, 0, 0, 1, 1, 0, 1)
check(near(gx, 10) and near(gy, 2) and near(gz, 28) and near(dz, 1), "full forward is 8 m ahead")
gx, gy, gz, dx, dz = CoopTrico.GoalFor(p, 1, 0, 1, 0, 0.3, 0, 1)
check(near(gx, 10 - 5.2) and near(gz, 20) and near(dx, -1), "light right")
gx, gy, gz = CoopTrico.GoalFor(p, 0, 1, 0, -1, 1, nil, nil)
check(near(gz, 12), "no camera: Trico's facing is forward, back goes behind")
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
check(CoopTrico.forward[3] == "cam", "forward source kept for the report")

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
check(#tr == 1 and tr[1][2] == "TR goal off p=0.0,0.0,0.0 g=- pm=nil rq=nil in=nil it=nil md=nil",
	"report line: " .. tostring(tr[1] and tr[1][2]))
local ts = Harness.Find("^TS ")
check(#ts == 1, "second report line")
check(string.find(ts[1][2], "st=s05 inj=true boy=1.0,0.0,-2.0 cam=0.0,3.0,-8.0 f=0.00,1.00,cam", 1,
	true), "stage, injured, boy, camera and forward: " .. ts[1][2])

-- no Trico yet (title screen): nothing happens
Harness.tricoProps = nil
CoopTrico.Update(0.016, pad)
print("ok")
