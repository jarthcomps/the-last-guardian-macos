dofile(string.match(debug.getinfo(1, "S").source, "^@(.*/)") .. "harness.lua")

Load("game/scripts/coop/coop_input")

local function setFlags(flags, seq, buttons)
	Input.touch2.num = 2
	Input.touch2.data[2] = { x = flags, y = seq + 256 * (buttons or 0), id = 192 }
end
-- the Trico player's sticks: 0-255 per axis, 128 = centre, 0 = left / up
local function setSticks(lx, ly, rx, ry)
	Input.touch2.data[1] = { x = lx + 256 * ly, y = rx + 256 * ry, id = 193 }
end
local V2 = 8192 -- protocol version 2 in the top nibble

-- the hello goes out once a second on the second pad, before and after the emulator answers
CoopInput.Update(0.016)
check(#Harness.lightbar == 1 and Harness.lightbar[1][1] == 1 and Harness.lightbar[1][2] == 0xc00fee, "hello")
CoopInput.Update(0.5)
check(#Harness.lightbar == 1, "no second hello within a second")
CoopInput.Update(0.6)
check(#Harness.lightbar == 2, "hello repeats")
check(not CoopInput.active, "inactive without the emulator")

-- decode
local f = CoopInput.Decode(V2 + 1 + 2 + 8 + 32)
check(f.active and f.boyHuman and not f.tricoHuman and f.tricoCamera and f.twoPlayers and f.version == 2, "decode")
local t = CoopInput.DecodeTrico(255 + 256 * 0, 0 + 256 * 128, 1 + 32)
check(t.x == 1 and t.y == 1 and t.rx == -1 and t.ry == 0, "sticks: right/up, right stick left")
check(t.hold.Cross and t.down.Cross and t.hold.R1 and not t.hold.Circle, "buttons")
local t2 = CoopInput.DecodeTrico(128 + 256 * 128, 128 + 256 * 128, 1, t)
check(t2.x == 0 and t2.y == 0 and t2.hold.Cross and not t2.down.Cross, "held, not pressed again")

-- solo, boy
setFlags(V2 + 1 + 2, 0)
CoopInput.Update(0.016)
check(CoopInput.active and CoopInput.boyHuman and not CoopInput.tricoHuman, "solo boy")
check(#CoopInput.events == 1 and CoopInput.events[1].kind == "roles", "roles event on activation")
check(CoopInput.TricoPad() == nil, "no Trico pad")

-- solo swap to Trico, started by the boy's player
setFlags(V2 + 1 + 4, 1)
setSticks(128, 0, 128, 128)
CoopInput.Update(0.016)
local kinds = {}
for _, e in ipairs(CoopInput.events) do kinds[e.kind] = e.value or true end
check(kinds.swap == "boy" and kinds.roles, "swap from boy")
local tp = CoopInput.TricoPad()
check(tp and tp.x == 0 and tp.y == 1, "Trico stick from the touch point")
Input.touch2.data[1] = { x = 0, y = 0, id = -1 }
CoopInput.Update(0.016)
check(CoopInput.TricoPad() == nil, "no stick point, no Trico input")
setSticks(128, 0, 128, 128)

-- swap back started by Trico's player, camera toggled in the same frame
setFlags(V2 + 1 + 2 + 8 + 16, 2)
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
