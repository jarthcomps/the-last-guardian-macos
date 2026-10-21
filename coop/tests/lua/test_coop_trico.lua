dofile(string.match(debug.getinfo(1, "S").source, "^@(.*/)") .. "harness.lua")

Load("game/scripts/coop/coop_debug")
Load("game/scripts/coop/coop_trico")

local function near(a, b) return math.abs(a - b) < 1e-3 end

-- stick: dead zone, LX/LY scaled by LMag, clamped to 1
check(CoopTrico.Stick(nil) == nil, "no pad")
check(CoopTrico.Stick({ LX = 1, LY = 0, LMag = 0.2 }) == nil, "dead zone")
local sx, sy, len = CoopTrico.Stick({ LX = 0, LY = 1, LMag = 0.5 })
check(near(sx, 0) and near(sy, 0.5) and near(len, 0.5), "half up")
sx, sy, len = CoopTrico.Stick({ LX = 1, LY = 1, LMag = 1 })
check(near(len, 1) and near(sx, sy), "clamped diagonal")

-- goals are camera-relative: forward along +Z, right is -X (looking along +Z)
local p = Vector(10, 2, 20)
local gx, gy, gz, dx, dz = CoopTrico.GoalFor(p, 1, 0, 0, 1, 1, 0, 1)
check(near(gx, 10) and near(gy, 2) and near(gz, 26) and near(dz, 1), "full forward is 6 m ahead")
gx, gy, gz, dx, dz = CoopTrico.GoalFor(p, 1, 0, 1, 0, 0.3, 0, 1)
check(near(gx, 10 - 3.9) and near(gz, 20) and near(dx, -1), "light right")
gx, gy, gz = CoopTrico.GoalFor(p, 0, 1, 0, -1, 1, nil, nil)
check(near(gz, 14), "no camera: Trico's facing is forward, back goes behind")

-- steering through the interest strategy
Harness.tricoProps = Harness.NewTricoProps(0, 0, 0)
Harness.objects.PlayerAi2Instruction_InstProxyEntity = { name = "proxy" }
Harness.objects.CameraManager = { camera = { worldPosition = Vector(0, 3, -8) } }
local pad = { LX = 0, LY = 1, LMag = 1 }
CoopTrico.Update(0.016, pad)
check(CoopTrico.steering, "steering")
check(#Harness.moved == 1 and Harness.moved[1][2] == "0,0,6", "proxy moved to the goal")
check(#Harness.CallsNamed("interest") == 1 and #Harness.CallsNamed("instruction") == 1, "interest")
check(#Harness.CallsNamed("overwrite") == 1, "approach overwritten")
local ow = Harness.CallsNamed("overwrite")[1][4]
check(ow.condition() == true and near(ow.destination.Z, 6), "overwrite holds while steering")

-- goal refreshes every 0.3 s, not every frame
CoopTrico.Update(0.1, pad)
check(#Harness.moved == 1, "no refresh yet")
CoopTrico.Update(0.25, pad)
check(#Harness.moved == 2, "refreshed")

-- release hands Trico back once
CoopTrico.Update(0.016, { LX = 0, LY = 0, LMag = 0 })
check(not CoopTrico.steering and not ow.condition(), "released")
check(#Harness.CallsNamed("reset") == 1 and #Harness.CallsNamed("stop") == 1, "reset and stop")
CoopTrico.Update(0.016, nil)
check(#Harness.CallsNamed("stop") == 1, "released only once")

-- forward and slide strategies
CoopTrico.strategy = "forward"
CoopTrico.Update(0.016, pad)
local fw = Harness.CallsNamed("forward")
check(#fw == 1 and fw[1][3].isFreeMove and near(fw[1][3].goalPos.Z, 6), "forward request")
check(near(Harness.tricoProps.currentInfo.apDestination.Z, 6), "approach destination set")
CoopTrico.Release()
CoopTrico.strategy = "slide"
CoopTrico.Update(0.016, pad)
check(#Harness.CallsNamed("slide") == 1, "slide request")
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

-- reports twice a second
CoopTrico.Release()
Harness.lightbar = {}
CoopTrico.Update(0.5, nil)
CoopDebug.Flush()
check(#Harness.Find("^TR interest off p=0.0,0.0,0.0") == 1, "report line")

-- no Trico yet (title screen): nothing happens
Harness.tricoProps = nil
CoopTrico.Update(0.016, pad)
print("ok")
