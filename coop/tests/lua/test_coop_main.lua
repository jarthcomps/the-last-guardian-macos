dofile(string.match(debug.getinfo(1, "S").source, "^@(.*/)") .. "harness.lua")

Load("game/scripts/coop/coop_main")
check(#Harness.ticks == 1, "one tick registered")
local loaded = table.concat(Harness.loaded, ",")
check(loaded == "coop_main,coop_debug,coop_input,coop_trico,coop_actions,coop_pulse,coop_camera,coop_config", "modules loaded: " .. loaded)
check(not CoopDebug.enabled, "debug channel off by default")

-- loading again (state restart) keeps a single tick
Load("game/scripts/coop/coop_main")
check(#Harness.ticks == 1, "still one tick after reload")

-- the hello goes out; nothing else without the debug channel
Harness.Tick(1 / 60, 3)
check(#Harness.lightbar == 1 and Harness.lightbar[1][2] == 0xc00fee, "only the hello")

-- with the debug channel: reports and events
CoopDebug.enabled = true
Input.touch2.data[2] = { x = 8192 + 1 + 4, y = 0, id = 192 } -- solo, playing Trico
Harness.Tick(1 / 60)
check(#Harness.Find("^COOP event roles") == 1, "roles event reported")
check(#Harness.Find("^COOP t=.* on=true boy=false trico=true") == 1, "status line")

-- the Trico player's stick steers Trico
Harness.tricoProps = Harness.NewTricoProps(0, 0, 0)
Harness.objects.PlayerAi2Instruction_InstProxyEntity = {}
Input.touch2.data[1] = { x = 128 + 256 * 0, y = 128 + 256 * 128, id = 193 } -- stick up
Harness.Tick(1 / 60)
check(CoopTrico.steering, "steering from the Trico player's stick")
Harness.Tick(1 / 60, 61) -- the status report comes once a second
check(#Harness.Find("^TP L=0.00,1.00 R=0.00,0.00 hold=%[%]") >= 1, "TP line")

-- giving up the Trico role releases Trico
Input.touch2.data[2] = { x = 8192 + 1 + 2, y = 1, id = 192 }
Harness.Tick(1 / 60)
check(not CoopTrico.steering, "released when nobody plays Trico")
check(#Harness.Find("^COOP event swap boy") == 1, "swap event")

-- a failing tick is reported once and does not raise
CoopInput.Update = function() error("boom") end
local ok = pcall(Harness.Tick, 1, 3)
check(ok, "tick errors are contained")
CoopDebug.Flush()
check(#Harness.Find("^COOP tick error") == 1, "tick error reported once")
print("ok")
