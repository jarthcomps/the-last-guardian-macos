-- tlg-coop: a lightning arc along Trico when the players swap.
--
-- The game draws an arc from an ElectricityLocator of type "Source" to the "Destination" locators
-- with the same locatorName, while their flags are not "Disabled" (the s14 door-blast scene puts
-- such a source on Trico's tail bone jt_Tail_17). Two fixed pairs sit on Trico, one per
-- direction, so no locator changes type after it is attached:
--   the boy's player starts the swap   -> tail to head;
--   Trico's player starts the swap     -> head to tail.
-- Each arc lasts CoopSwapFx.arcTime seconds, with the game's electricity sound.

CoopSwapFx = CoopSwapFx or {}
CoopSwapFx.arcTime = CoopSwapFx.arcTime or 0.6

local TAIL_BONE = "jt_Tail_17"
local HEAD_BONE = "jt_Head"
local PAIRS = {
	boy = { name = "CoopSwapArc_TH", source = TAIL_BONE, destination = HEAD_BONE, label = "tail-head" },
	trico = { name = "CoopSwapArc_HT", source = HEAD_BONE, destination = TAIL_BONE, label = "head-tail" },
}

function CoopSwapFx.Reset()
	CoopSwapFx.trico = nil        -- the Trico character the locators were made for
	CoopSwapFx.locators = nil     -- { boy = { src, dst }, trico = { src, dst } }
	CoopSwapFx.live = nil         -- the pair now shown
	CoopSwapFx.timer = 0
	CoopSwapFx.lastError = nil
	CoopSwapFx.arcs = 0
end

local function Say(fmt, ...)
	if CoopDebug then CoopDebug.Print(fmt, ...) end
end

local function Sound(event)
	if not PostEvent or not SoundSystem or not SoundSystem.GetGlobalSubName then return end
	pcall(function() PostEvent(event, SoundSystem:GetGlobalSubName(), "") end)
end

local function Locator(bone, name, kind)
	local ex = NewObject("ElectricityLocator")
	ex.locatorName = name
	ex.type = kind
	ex.flags = "Disabled"
	Connect(bone, ex)
	return ex
end

-- The two pairs for the Trico character now in the world (made again if it changed).
local function Locators()
	local trico = CharacterMngr and CharacterMngr:GetTrico()
	if not trico then return nil end
	if CoopSwapFx.locators and CoopSwapFx.trico == trico then return CoopSwapFx.locators end
	local tail = GameCharacter.FindBone(trico, TAIL_BONE)
	local head = GameCharacter.FindBone(trico, HEAD_BONE)
	if not tail or not head then error("no " .. (tail and HEAD_BONE or TAIL_BONE)) end
	local bones = { [TAIL_BONE] = tail, [HEAD_BONE] = head }
	local made = {}
	for who, pair in pairs(PAIRS) do
		made[who] = {
			src = Locator(bones[pair.source], pair.name, "Source"),
			dst = Locator(bones[pair.destination], pair.name, "Destination"),
		}
	end
	CoopSwapFx.trico = trico
	CoopSwapFx.locators = made
	return made
end

local function Off()
	local who = CoopSwapFx.live
	if not who then return end
	CoopSwapFx.live = nil
	local l = CoopSwapFx.locators and CoopSwapFx.locators[who]
	if l then
		l.src.flags = "|Disabled"
		l.dst.flags = "|Disabled"
	end
	Sound("CondorStopElectricity")
	Say("FX arc off")
end

local function Arc(who)
	local pair = PAIRS[who]
	if not pair then return end
	local all = Locators()
	if not all then return end
	Off()
	local l = all[who]
	l.src.flags = "~Disabled"
	l.dst.flags = "~Disabled"
	CoopSwapFx.live = who
	CoopSwapFx.timer = CoopSwapFx.arcTime
	CoopSwapFx.arcs = CoopSwapFx.arcs + 1
	Sound("CondorStartElectricity")
	Say("FX swap %s arc %s", who, pair.label)
end

-- Call once per frame with CoopInput.events (a "swap" event's value is who started it).
function CoopSwapFx.Update(dt, events)
	dt = dt or 0
	local ok, err = pcall(function()
		for _, e in ipairs(events or {}) do
			if e.kind == "swap" then Arc(e.value) end
		end
		if CoopSwapFx.live then
			CoopSwapFx.timer = CoopSwapFx.timer - dt
			if CoopSwapFx.timer <= 0 then Off() end
		end
	end)
	if not ok and err ~= CoopSwapFx.lastError then
		CoopSwapFx.lastError = err
		Say("FX error %s", tostring(err))
	end
end

function CoopSwapFx.Release()
	pcall(Off)
end

CoopSwapFx.Reset()
