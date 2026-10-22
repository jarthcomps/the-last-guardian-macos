-- tlg-coop: entry point, loaded at the end of game/scripts/states/main.lua.
--
-- Every frame: read what the emulator says about the players (coop_input), let the Trico player
-- steer Trico (coop_trico), use its gesture actions (coop_actions), aim and fire its lightning
-- (coop_pulse), frame the shared camera (coop_camera, switched with Select) and show a lightning
-- arc along Trico on each swap (coop_swap_fx). coop_config.lua, when present, overrides settings (the test runs
-- use it to turn on the debug channel and pick the steering strategy).

Load("game/scripts/coop/coop_debug")
Load("game/scripts/coop/coop_input")
Load("game/scripts/coop/coop_trico")
Load("game/scripts/coop/coop_actions")
Load("game/scripts/coop/coop_pulse")
Load("game/scripts/coop/coop_camera")
Load("game/scripts/coop/coop_swap_fx")
pcall(Load, "game/scripts/coop/coop_config")

Coop = Coop or {}
Coop.version = "0.1.0"
Coop.clock = 0
Coop.frames = 0
Coop.nextReport = 0

local function padLine(label, pad)
	if type(pad) ~= "table" then return label .. " missing" end
	return string.format("%s L=%.2f,%.2f m=%.2f R=%.2f,%.2f", label, pad.LX or -9, pad.LY or -9,
		pad.LMag or -9, pad.RX or -9, pad.RY or -9)
end

local function report()
	CoopDebug.Print("COOP t=%.1f f=%d on=%s boy=%s trico=%s two=%s cam=%s seq=%s", Coop.clock,
		Coop.frames, tostring(CoopInput.active), tostring(CoopInput.boyHuman),
		tostring(CoopInput.tricoHuman), tostring(CoopInput.twoPlayers),
		tostring(CoopInput.tricoCamera), tostring(CoopInput.sequence))
	CoopDebug.Print("%s", padLine("P1", Input.pad1))
	local t = CoopInput.trico
	if t then
		local held = {}
		for _, name in ipairs(CoopInput.BUTTONS) do
			if t.hold[name] then held[#held + 1] = name end
		end
		CoopDebug.Print("TP L=%.2f,%.2f R=%.2f,%.2f hold=[%s]", t.x, t.y, t.rx, t.ry,
			table.concat(held, ","))
	end
end

local function tick(dt)
	Coop.frames = Coop.frames + 1
	Coop.clock = Coop.clock + (dt or 0)
	CoopInput.Update(dt)
	for _, e in ipairs(CoopInput.events) do
		CoopDebug.Print("COOP event %s %s", e.kind, tostring(e.value))
		if e.kind == "roles" and not CoopInput.tricoHuman then
			CoopTrico.Release()
			CoopActions.Release()
			CoopPulse.Release()
		end
	end
	CoopTrico.Update(dt, CoopInput.TricoPad())
	CoopActions.Update(dt, CoopInput.TricoPad())
	CoopPulse.Update(dt, CoopInput.TricoPad())
	CoopCamera.Update(dt, CoopInput.active and CoopInput.tricoCamera)
	CoopSwapFx.Update(dt, CoopInput.events)
	if CoopDebug.enabled and Coop.clock >= Coop.nextReport then
		Coop.nextReport = Coop.clock + 1
		report()
	end
	CoopDebug.Flush()
end

-- main.lua can run again when the game restarts its state: never register twice.
if Coop.tick then pcall(UnRegisterTick, Coop.tick) end
Coop.tick = function(dt)
	local ok, err = pcall(tick, dt)
	if not ok and not Coop.tickFailed then
		Coop.tickFailed = true
		pcall(CoopDebug.Print, "COOP tick error %s", tostring(err))
		pcall(CoopDebug.Flush)
	end
end
RegisterTick(Coop.tick)
CoopDebug.Print("COOP %s registered", Coop.version)
