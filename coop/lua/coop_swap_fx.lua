-- tlg-coop: a lightning arc between Trico's tail and the boy's head when the players swap.
--
-- The game draws an arc from an ElectricityLocator of type "Source" to the "Destination" locators
-- with the same locatorName, while their flags are not "Disabled" (the s14 door-blast scene puts
-- such a source on Trico's tail bone jt_Tail_17; the dark-dome scenes arc to the boy's limbs).
-- Two fixed pairs, one per direction, so no locator changes type after it is attached. The arc
-- starts at whoever started the swap:
--   Trico's player starts the swap   -> Trico's tail to the boy's head;
--   the boy's player starts the swap -> the boy's head to Trico's tail.
-- Each arc lasts CoopSwapFx.arcTime seconds, with the game's electricity sound.

CoopSwapFx = CoopSwapFx or {}
CoopSwapFx.arcTime = CoopSwapFx.arcTime or 0.6

local TAIL_BONE = "jt_Tail_17"     -- Trico's
local HEAD_BONE = "jt_Head"        -- the boy's
local PAIRS = {
	trico = { name = "CoopSwapArc_TB", source = "tail", destination = "head", label = "tail-boy" },
	boy = { name = "CoopSwapArc_BT", source = "head", destination = "tail", label = "boy-tail" },
}

function CoopSwapFx.Reset()
	CoopSwapFx.trico = nil        -- the characters the locators were made for
	CoopSwapFx.boy = nil
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

-- The boy's head bone: through his character, else through his AI (as condorplayerhandymirror.lua
-- finds his spine).
local function BoyHead(boy)
	local head = GameCharacter.FindBone(boy, HEAD_BONE)
	if head then return head end
	local ok, bone = pcall(function() return Ai2.Utils.FindBone(Ai2.Utils.GetPlayerAI(), HEAD_BONE) end)
	return ok and bone or nil
end

-- The two pairs for the Trico and boy characters now in the world (made again if one changed).
local function Locators()
	local trico = CharacterMngr and CharacterMngr:GetTrico()
	local boy = CharacterMngr and CharacterMngr:GetPlayer()
	if not trico or not boy then return nil end
	if CoopSwapFx.locators and CoopSwapFx.trico == trico and CoopSwapFx.boy == boy then
		return CoopSwapFx.locators
	end
	local tail = GameCharacter.FindBone(trico, TAIL_BONE)
	local head = BoyHead(boy)
	if not tail or not head then error("no " .. (tail and "boy " .. HEAD_BONE or TAIL_BONE)) end
	local bones = { tail = tail, head = head }
	local made = {}
	for who, pair in pairs(PAIRS) do
		made[who] = {
			src = Locator(bones[pair.source], pair.name, "Source"),
			dst = Locator(bones[pair.destination], pair.name, "Destination"),
		}
	end
	CoopSwapFx.trico = trico
	CoopSwapFx.boy = boy
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
