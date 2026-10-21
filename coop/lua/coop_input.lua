-- tlg-coop: what the emulator tells the scripts, and the scripts' hello back.
--
-- The emulator (SHADPS4_TLG_COOP=1) routes the controllers: the boy's player drives the game's
-- first pad, Trico's player the second pad (Input.pad2). It also puts its state in the second
-- touch point of the second pad: id 192, x = flags, y = swap count. It only starts routing once
-- it has seen the scripts' hello, the light bar colour c00fee on the second pad.

CoopInput = CoopInput or {}

local MAGIC_ID = 192
local HELLO = "c00fee"
local HELLO_INTERVAL = 1.0

local FLAG_ACTIVE = 0
local FLAG_BOY_HUMAN = 1
local FLAG_TRICO_HUMAN = 2
local FLAG_TRICO_CAMERA = 3
local FLAG_TRICO_STARTED_SWAP = 4
local FLAG_TWO_PLAYERS = 5

local function bit(value, n)
	return math.floor(value / (2 ^ n)) % 2 == 1
end

function CoopInput.Reset()
	CoopInput.active = false       -- the emulator's co-op mode is on and talking to us
	CoopInput.flags = 0
	CoopInput.version = 0
	CoopInput.boyHuman = true
	CoopInput.tricoHuman = false
	CoopInput.twoPlayers = false
	CoopInput.tricoCamera = false
	CoopInput.tricoStartedSwap = false
	CoopInput.sequence = nil
	CoopInput.helloIn = 0
	CoopInput.events = {}
end

-- Decode the flags word; returns a table (also used by the tests).
function CoopInput.Decode(flags)
	return {
		active = bit(flags, FLAG_ACTIVE),
		boyHuman = bit(flags, FLAG_BOY_HUMAN),
		tricoHuman = bit(flags, FLAG_TRICO_HUMAN),
		tricoCamera = bit(flags, FLAG_TRICO_CAMERA),
		tricoStartedSwap = bit(flags, FLAG_TRICO_STARTED_SWAP),
		twoPlayers = bit(flags, FLAG_TWO_PLAYERS),
		version = math.floor(flags / 4096) % 16,
	}
end

local function push(kind, a)
	local e = CoopInput.events
	e[#e + 1] = { kind = kind, value = a }
end

-- Call once per frame. Fills CoopInput.events with "swap", "camera", "roles" events.
function CoopInput.Update(dt)
	CoopInput.events = {}
	CoopInput.helloIn = CoopInput.helloIn - (dt or 0)
	if CoopInput.helloIn <= 0 then
		CoopInput.helloIn = HELLO_INTERVAL
		pcall(SetLightBar, 1, HELLO)
	end

	local touch = Input.touch2
	local point = touch and touch.data and touch.data[2]
	if not point or point.id ~= MAGIC_ID then
		if CoopInput.active then
			CoopInput.active = false
			push("roles")
		end
		return
	end
	local f = CoopInput.Decode(point.x or 0)
	if not f.active then return end
	local rolesChanged = not CoopInput.active or f.boyHuman ~= CoopInput.boyHuman
		or f.tricoHuman ~= CoopInput.tricoHuman
	local cameraChanged = CoopInput.active and f.tricoCamera ~= CoopInput.tricoCamera
	CoopInput.active = true
	CoopInput.flags = point.x
	CoopInput.version = f.version
	CoopInput.boyHuman = f.boyHuman
	CoopInput.tricoHuman = f.tricoHuman
	CoopInput.twoPlayers = f.twoPlayers
	CoopInput.tricoCamera = f.tricoCamera
	CoopInput.tricoStartedSwap = f.tricoStartedSwap
	local seq = point.y or 0
	if CoopInput.sequence ~= nil and seq ~= CoopInput.sequence then
		push("swap", f.tricoStartedSwap and "trico" or "boy")
	end
	CoopInput.sequence = seq
	if rolesChanged then push("roles") end
	if cameraChanged then push("camera", f.tricoCamera) end
end

-- The Trico player's pad, or nil when nobody plays Trico.
function CoopInput.TricoPad()
	if CoopInput.active and CoopInput.tricoHuman then return Input.pad2 end
	return nil
end

CoopInput.Reset()
