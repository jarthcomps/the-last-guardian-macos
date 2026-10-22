dofile(string.match(debug.getinfo(1, "S").source, "^@(.*/)") .. "harness.lua")

Load("game/scripts/coop/coop_debug")
Load("game/scripts/coop/coop_swap_fx")

-- Trico's bones and the sound call
local bones = {}
local function trico(name)
	return { name = name }
end
GameCharacter = {
	FindBone = function(character, bone)
		bones[character] = bones[character] or {}
		bones[character][bone] = bones[character][bone] or { name = bone, children = {} }
		return bones[character][bone]
	end,
}
local sounds = {}
SoundSystem = { GetGlobalSubName = function() return "global" end }
function PostEvent(event, sub, arg) sounds[#sounds + 1] = event end

-- no Trico (title screen): nothing happens
CoopSwapFx.Update(0.016, { { kind = "swap", value = "boy" } })
check(CoopSwapFx.arcs == 0, "no Trico, no arc")

CharacterMngr.trico = trico("Trico1")
local function locs(bone)
	local list = {}
	for _, c in ipairs(bones[CharacterMngr.trico][bone].children) do list[c.locatorName] = c end
	return list
end

-- the boy's player starts the swap: tail to head
CoopSwapFx.Update(0.016, { { kind = "swap", value = "boy" } })
local tail, head = locs("jt_Tail_17"), locs("jt_Head")
check(tail.CoopSwapArc_TH.type == "Source" and head.CoopSwapArc_TH.type == "Destination", "tail-head pair")
check(head.CoopSwapArc_HT.type == "Source" and tail.CoopSwapArc_HT.type == "Destination", "head-tail pair")
check(tail.CoopSwapArc_TH.flags == "~Disabled" and head.CoopSwapArc_TH.flags == "~Disabled", "tail-head on")
check(head.CoopSwapArc_HT.flags == "Disabled", "the other pair stays off")
check(sounds[#sounds] == "CondorStartElectricity", "sound on")

-- 0.6 s later it goes off
CoopSwapFx.Update(0.3, {})
check(CoopSwapFx.live == "boy", "still on at 0.3 s")
CoopSwapFx.Update(0.35, {})
check(CoopSwapFx.live == nil and tail.CoopSwapArc_TH.flags == "|Disabled"
	and head.CoopSwapArc_TH.flags == "|Disabled", "off after 0.6 s")
check(sounds[#sounds] == "CondorStopElectricity", "sound off")

-- Trico's player starts the swap: head to tail; the locators are reused
CoopSwapFx.Update(0.016, { { kind = "roles" }, { kind = "swap", value = "trico" } })
check(head.CoopSwapArc_HT.flags == "~Disabled" and tail.CoopSwapArc_HT.flags == "~Disabled", "head-tail on")
check(#bones[CharacterMngr.trico].jt_Head.children == 2, "locators made once")
-- a second swap during an arc replaces it
CoopSwapFx.Update(0.1, { { kind = "swap", value = "boy" } })
check(CoopSwapFx.live == "boy" and head.CoopSwapArc_HT.flags == "|Disabled"
	and tail.CoopSwapArc_TH.flags == "~Disabled", "replaced")
CoopSwapFx.Release()
check(CoopSwapFx.live == nil and tail.CoopSwapArc_TH.flags == "|Disabled", "released")

-- a new Trico character (another world): new locators on its bones
CharacterMngr.trico = trico("Trico2")
CoopSwapFx.Update(0.016, { { kind = "swap", value = "trico" } })
check(#bones[CharacterMngr.trico].jt_Head.children == 2 and locs("jt_Head").CoopSwapArc_HT.flags == "~Disabled",
	"made again for the new Trico")
CoopSwapFx.Release()

-- the arc's length is a setting
CoopSwapFx.arcTime = 3
CoopSwapFx.Update(0.016, { { kind = "swap", value = "boy" } })
CoopSwapFx.Update(2.5, {})
check(CoopSwapFx.live == "boy", "still on at 2.5 s with arcTime 3")
CoopSwapFx.Update(0.6, {})
check(CoopSwapFx.live == nil, "off after 3 s")
CoopSwapFx.arcTime = 0.6

-- reports and contained errors
CoopDebug.enabled = true
CoopSwapFx.Update(0.016, { { kind = "swap", value = "boy" } })
CoopDebug.Flush()
check(#Harness.Find("^FX swap boy arc tail%-head") == 1, "report line")
CoopSwapFx.Release()
GameCharacter.FindBone = function() return nil end
CharacterMngr.trico = trico("Trico3")
CoopSwapFx.Update(0.016, { { kind = "swap", value = "boy" } })
CoopDebug.Flush()
check(#Harness.Find("^FX error") == 1, "missing bones reported")
print("ok")
