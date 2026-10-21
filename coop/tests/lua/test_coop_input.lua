dofile(string.match(debug.getinfo(1, "S").source, "^@(.*/)") .. "harness.lua")

Load("game/scripts/coop/coop_input")

local function setFlags(flags, seq)
	Input.touch2.num = 2
	Input.touch2.data[2] = { x = flags, y = seq, id = 192 }
end
local V1 = 4096 -- protocol version 1 in the top nibble

-- the hello goes out once a second on the second pad, before and after the emulator answers
CoopInput.Update(0.016)
check(#Harness.lightbar == 1 and Harness.lightbar[1][1] == 1 and Harness.lightbar[1][2] == 0xc00fee, "hello")
CoopInput.Update(0.5)
check(#Harness.lightbar == 1, "no second hello within a second")
CoopInput.Update(0.6)
check(#Harness.lightbar == 2, "hello repeats")
check(not CoopInput.active, "inactive without the emulator")

-- decode
local f = CoopInput.Decode(V1 + 1 + 2 + 8 + 32)
check(f.active and f.boyHuman and not f.tricoHuman and f.tricoCamera and f.twoPlayers and f.version == 1, "decode")

-- solo, boy
setFlags(V1 + 1 + 2, 0)
CoopInput.Update(0.016)
check(CoopInput.active and CoopInput.boyHuman and not CoopInput.tricoHuman, "solo boy")
check(#CoopInput.events == 1 and CoopInput.events[1].kind == "roles", "roles event on activation")
check(CoopInput.TricoPad() == nil, "no Trico pad")

-- solo swap to Trico, started by the boy's player
setFlags(V1 + 1 + 4, 1)
CoopInput.Update(0.016)
local kinds = {}
for _, e in ipairs(CoopInput.events) do kinds[e.kind] = e.value or true end
check(kinds.swap == "boy" and kinds.roles, "swap from boy")
check(CoopInput.TricoPad() == Input.pad2, "Trico pad")

-- swap back started by Trico's player, camera toggled in the same frame
setFlags(V1 + 1 + 2 + 8 + 16, 2)
CoopInput.Update(0.016)
kinds = {}
for _, e in ipairs(CoopInput.events) do kinds[e.kind] = e.value end
check(kinds.swap == "trico" and kinds.camera == true, "swap from Trico and camera")

-- no events when nothing changes
CoopInput.Update(0.016)
check(#CoopInput.events == 0, "quiet frame")

-- the emulator stops answering (another game state, co-op off)
Input.touch2.data[2] = { x = 0, y = 0, id = -1 }
CoopInput.Update(0.016)
check(not CoopInput.active and CoopInput.events[1].kind == "roles", "deactivates")
print("ok")
