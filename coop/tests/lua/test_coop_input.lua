dofile(string.match(debug.getinfo(1, "S").source, "^@(.*/)") .. "harness.lua")

Load("game/scripts/coop/coop_input")

local function setFlags(flags, seq, buttons)
	Input.touch2.num = 2
	Input.touch2.data[2] = { x = flags, y = seq % 16 + 16 * (buttons or 0), id = 192 }
end
-- the Trico player's sticks: 0-255 per axis, 128 = centre, 0 = left / up
local function setSticks(lx, ly, rx, ry)
	Input.touch2.data[1] = { x = lx + 256 * ly, y = rx + 256 * ry, id = 193 }
end
local V3 = 3 * 4096 -- protocol version 3 in the top nibble

-- the hello goes out once a second on the second pad, before and after the emulator answers
CoopInput.Update(0.016)
check(#Harness.lightbar == 1 and Harness.lightbar[1][1] == 1 and Harness.lightbar[1][2] == 0xc00fee, "hello")
CoopInput.Update(0.5)
check(#Harness.lightbar == 1, "no second hello within a second")
CoopInput.Update(0.6)
check(#Harness.lightbar == 2, "hello repeats")
check(not CoopInput.active, "inactive without the emulator")

-- decode
local f = CoopInput.Decode(V3 + 1 + 2 + 8 + 32)
check(f.active and f.boyHuman and not f.tricoHuman and f.tricoCamera and f.twoPlayers and f.version == 3, "decode")
local t = CoopInput.DecodeTrico(255 + 256 * 0, 0 + 256 * 128, 1 + 32)
check(t.x == 1 and t.y == 1 and t.rx == -1 and t.ry == 0, "sticks: right/up, right stick left")
check(t.hold.Cross and t.down.Cross and t.hold.R1 and not t.hold.Circle, "buttons")
local t2 = CoopInput.DecodeTrico(128 + 256 * 128, 128 + 256 * 128, 1, t)
check(t2.x == 0 and t2.y == 0 and t2.hold.Cross and not t2.down.Cross, "held, not pressed again")
local t3 = CoopInput.DecodeTrico(32896, 32896, 256 + 512 + 1024 + 2048)
check(t3.down.Up and t3.down.Down and t3.down.Left and t3.down.Right and not t3.hold.Cross, "the D-pad")

-- solo, boy
setFlags(V3 + 1 + 2, 0)
CoopInput.Update(0.016)
check(CoopInput.active and CoopInput.boyHuman and not CoopInput.tricoHuman, "solo boy")
check(#CoopInput.events == 1 and CoopInput.events[1].kind == "roles", "roles event on activation")
check(CoopInput.TricoPad() == nil, "no Trico pad")

-- solo swap to Trico, started by the boy's player
setFlags(V3 + 1 + 4, 1)
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
setFlags(V3 + 1 + 2 + 8 + 16, 2)
CoopInput.Update(0.016)
kinds = {}
for _, e in ipairs(CoopInput.events) do kinds[e.kind] = e.value end
check(kinds.swap == "trico" and kinds.camera == true, "swap from Trico and camera")

-- the D-pad comes through with the swap count in the low 4 bits
setFlags(V3 + 1 + 2 + 8 + 16, 2, 2048 + 1)    -- Right and Cross
CoopInput.Update(0.016)
check(#CoopInput.events == 0, "same count, no swap")
check(CoopInput.trico == nil, "boy role: no Trico input")
setFlags(V3 + 1 + 4, 3, 2048 + 1)
setSticks(128, 128, 128, 128)
CoopInput.Update(0.016)
check(CoopInput.TricoPad() and CoopInput.TricoPad().hold.Right and CoopInput.TricoPad().hold.Cross, "Right and Cross")
-- the count wraps at 16
setFlags(V3 + 1 + 4, 15, 0)
CoopInput.Update(0.016)
setFlags(V3 + 1 + 4, 16, 0)
CoopInput.Update(0.016)
check(CoopInput.sequence == 0 and CoopInput.events[1] and CoopInput.events[1].kind == "swap", "wraps")
setFlags(V3 + 1 + 2 + 8 + 16, 0, 0)
CoopInput.Update(0.016)

-- no events when nothing changes
CoopInput.Update(0.016)
check(#CoopInput.events == 0, "quiet frame")

-- protocol 4: the Trico player's lone L3 taps, a 2-bit count in flags bits 6-7
local V4 = 4 * 4096
check(CoopInput.Decode(V4 + 1 + 3 * 64).turns == 3 and CoopInput.Decode(V4 + 1).turns == 0, "turn count")
setFlags(V4 + 1 + 4 + 0 * 64, 0, 0)
CoopInput.Update(0.016)
local function turned()
	for _, e in ipairs(CoopInput.events) do if e.kind == "turn" then return true end end
	return false
end
check(not turned(), "the first count seen is not a tap")
setFlags(V4 + 1 + 4 + 1 * 64, 0, 0)
CoopInput.Update(0.016)
check(turned(), "a tap")
CoopInput.Update(0.016)
check(not turned(), "once")
setFlags(V4 + 1 + 4 + 0 * 64, 0, 0)   -- wrapped round (3 more taps between frames count as one)
CoopInput.Update(0.016)
check(turned(), "after wrapping")
-- protocol 3 has no turn bits: never a turn
setFlags(V3 + 1 + 4 + 64, 0, 0)
CoopInput.Update(0.016)
check(not turned(), "protocol 3")
setFlags(V3 + 1 + 2 + 8 + 16, 0, 0)
CoopInput.Update(0.016)
CoopInput.Update(0.016)

-- the emulator stops answering (another game state, co-op off)
Input.touch2.data[2] = { x = 0, y = 0, id = -1 }
CoopInput.Update(0.016)
check(not CoopInput.active and CoopInput.events[1].kind == "roles", "deactivates")
print("ok")
