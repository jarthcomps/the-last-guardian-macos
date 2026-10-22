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
CoopSwapFx.Update(0.016, { { kind = "swap", value = "boy" } })
check(CoopSwapFx.arcs == 0, "no boy yet, no arc")
CharacterMngr.player = trico("Boy1")
local function locs(character, bone)
	local list = {}
	for _, c in ipairs(bones[character][bone].children) do list[c.locatorName] = c end
	return list
end

-- the boy's player starts the swap: from the boy's head to Trico's tail
CoopSwapFx.Update(0.016, { { kind = "swap", value = "boy" } })
local tail, head = locs(CharacterMngr.trico, "jt_Tail_17"), locs(CharacterMngr.player, "jt_Head")
check(head.CoopSwapArc_BT.type == "Source" and tail.CoopSwapArc_BT.type == "Destination", "boy-tail pair")
check(tail.CoopSwapArc_TB.type == "Source" and head.CoopSwapArc_TB.type == "Destination", "tail-boy pair")
check(bones[CharacterMngr.trico].jt_Head == nil, "nothing on Trico's head")
check(head.CoopSwapArc_BT.flags == "~Disabled" and tail.CoopSwapArc_BT.flags == "~Disabled", "boy-tail on")
check(tail.CoopSwapArc_TB.flags == "Disabled", "the other pair stays off")
check(sounds[#sounds] == "CondorStartElectricity", "sound on")

-- 0.6 s later it goes off
CoopSwapFx.Update(0.3, {})
check(CoopSwapFx.live == "boy", "still on at 0.3 s")
CoopSwapFx.Update(0.35, {})
check(CoopSwapFx.live == nil and head.CoopSwapArc_BT.flags == "|Disabled"
	and tail.CoopSwapArc_BT.flags == "|Disabled", "off after 0.6 s")
check(sounds[#sounds] == "CondorStopElectricity", "sound off")

-- Trico's player starts the swap: from Trico's tail to the boy's head; the locators are reused
CoopSwapFx.Update(0.016, { { kind = "roles" }, { kind = "swap", value = "trico" } })
check(tail.CoopSwapArc_TB.flags == "~Disabled" and head.CoopSwapArc_TB.flags == "~Disabled", "tail-boy on")
check(#bones[CharacterMngr.player].jt_Head.children == 2 and #bones[CharacterMngr.trico].jt_Tail_17.children == 2,
	"locators made once")
-- a second swap during an arc replaces it
CoopSwapFx.Update(0.1, { { kind = "swap", value = "boy" } })
check(CoopSwapFx.live == "boy" and tail.CoopSwapArc_TB.flags == "|Disabled"
	and head.CoopSwapArc_BT.flags == "~Disabled", "replaced")
CoopSwapFx.Release()
check(CoopSwapFx.live == nil and head.CoopSwapArc_BT.flags == "|Disabled", "released")

-- a new Trico or boy character (another world): new locators on their bones
CharacterMngr.trico = trico("Trico2")
CoopSwapFx.Update(0.016, { { kind = "swap", value = "trico" } })
check(#bones[CharacterMngr.trico].jt_Tail_17.children == 2
	and locs(CharacterMngr.trico, "jt_Tail_17").CoopSwapArc_TB.flags == "~Disabled", "made again for the new Trico")
CoopSwapFx.Release()
-- the boy's head through his AI when his character has no such bone
local aiHead = { name = "ai head", children = {} }
GameCharacter.FindBone = function(character, bone)
	if character == CharacterMngr.player then return nil end
	bones[character] = bones[character] or {}
	bones[character][bone] = bones[character][bone] or { name = bone, children = {} }
	return bones[character][bone]
end
Ai2.Utils.FindBone = function(ai, bone) return bone == "jt_Head" and aiHead or nil end
CharacterMngr.player = trico("Boy2")
CoopSwapFx.Update(0.016, { { kind = "swap", value = "boy" } })
check(#aiHead.children == 2 and aiHead.children[1].locatorName ~= nil, "boy's head through his AI")
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
check(#Harness.Find("^FX swap boy arc boy%-tail") == 1, "report line")
CoopSwapFx.Release()
GameCharacter.FindBone = function() return nil end
CharacterMngr.trico = trico("Trico3")
CoopSwapFx.Update(0.016, { { kind = "swap", value = "boy" } })
CoopDebug.Flush()
check(#Harness.Find("^FX error") == 1, "missing bones reported")
print("ok")
