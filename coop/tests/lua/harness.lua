-- Minimal stand-ins for the engine functions the co-op scripts call, so the scripts can run
-- under a stock Lua 5.1 interpreter. Usage: lua5.1 test_x.lua <lua dir>
LUA_DIR = arg[1] or "lua"

Harness = { lightbar = {}, ticks = {}, loaded = {}, resets = {} }

function Load(name)
	local base = string.match(name, "^game/scripts/coop/([%w_]+)$")
	assert(base, "unexpected Load " .. tostring(name))
	Harness.loaded[#Harness.loaded + 1] = base
	return dofile(LUA_DIR .. "/" .. string.lower(base) .. ".lua")
end

function SetLightBar(pad, color)
	assert(type(color) == "string" and string.len(color) == 6, "color must be rrggbb")
	Harness.lightbar[#Harness.lightbar + 1] = { pad, tonumber(color, 16) }
end
function ResetLightBar(pad) Harness.resets[#Harness.resets + 1] = pad end
function RegisterTick(f) Harness.ticks[#Harness.ticks + 1] = f end
function UnRegisterTick(f)
	for i, v in ipairs(Harness.ticks) do
		if v == f then table.remove(Harness.ticks, i) return end
	end
end
function GetObjPropsInfo(name) return { position = 1, name = 1 } end
Harness.objects = {}
function FindObject(name) return Harness.objects[name] end

local VectorMT = { __tostring = function(v) return string.format("%g,%g,%g", v.X, v.Y, v.Z) end }
VectorMT.__index = {
	-- rotate by a unit quaternion { X, Y, Z, W }
	Rotate = function(v, q)
		local tx = 2 * (q.Y * v.Z - q.Z * v.Y)
		local ty = 2 * (q.Z * v.X - q.X * v.Z)
		local tz = 2 * (q.X * v.Y - q.Y * v.X)
		return Vector(v.X + q.W * tx + (q.Y * tz - q.Z * ty),
			v.Y + q.W * ty + (q.Z * tx - q.X * tz),
			v.Z + q.W * tz + (q.X * ty - q.Y * tx))
	end,
}
function Vector(x, y, z)
	if type(x) == "table" then x, y, z = x.X, x.Y, x.Z end
	return setmetatable({ X = x, Y = y, Z = z }, VectorMT)
end
Harness.moved = {}
function SetWorldPosition(obj, text) Harness.moved[#Harness.moved + 1] = { obj, text } end

-- engine objects: NewObject / Connect / RenameObject keep Harness.objects by name
Harness.created = {}
local serial = 0
function NewObject(class)
	serial = serial + 1
	local obj = { class = class, _object_name = class .. serial, children = {} }
	Harness.created[#Harness.created + 1] = obj
	Harness.objects[obj._object_name] = obj
	return obj
end
function Connect(parent, child)
	if type(parent) == "string" then parent = Harness.objects[parent] or { children = {} } end
	parent.children = parent.children or {}
	parent.children[#parent.children + 1] = child
	child.parent = parent
end
function Clone(name)
	local obj = NewObject("Clone")
	obj.source = name
	return obj._object_name
end
function RenameObject(old, new)
	local obj = Harness.objects[old]
	Harness.objects[old] = nil
	obj._object_name = new
	Harness.objects[new] = obj
end

-- Trico's AI as far as coop_trico.lua touches it; Harness.calls records what it was asked.
Harness.calls = {}
local function record(name)
	return function(...) Harness.calls[#Harness.calls + 1] = { name, ... } end
end
function Harness.NewTricoProps(x, y, z)
	local mngr = {}
	mngr.RequestForward = record("forward")
	mngr.RequestSlide = record("slide")
	mngr.RequestStop = record("stop")
	return { currentInfo = { aiPos = Vector(x, y, z), ai2Ext = { dirXZ = Vector(0, 0, 1) } },
		moveMngr = mngr, approach = { overwrite = {} } }
end
Harness.reachable = function(from, to) return true end
Ai2 = {
	Utils = {
		GetCondorAIProps = function() return Harness.tricoProps end,
		RegisterInterest = record("interest"),
		InstructionAddition = record("instruction"),
		ResetAllInstructionLevel = record("reset"),
		InterestLevelAddition = record("level"),
		ResetInterestLevelAddition = record("levelReset"),
		InterestExclusionSwitching = record("exclude"),
		GetPlayerAI = function() return { pos = Vector(1, 0, -2) } end,
		Navmesh = {
			CheckSameIsland = function(props, from, to) return Harness.reachable(from, to) end,
		},
	},
	Approach = { Overwrite = { Request = record("overwrite") } },
	Condor = { StageScripts = { GetBoyStageID = function() return "s05" end } },
	CondorUtils = {
		EnableFreePulseMode = record("free"),
		DisableFreePulseMode = record("unfree"),
		OverwriteTailPulseDst = record("tail"),
		ResetOverwriteTailPulseDst = record("tailReset"),
		IsShootingPulse = function() return false end,
	},
}
Ai2.Utils.ReqCtrlCmd = record("cmd")
-- the navmesh under a point is 0.5 m up
function Ai2FindNearestPosition(ai, pos, ext) return { found = 1, pos = Vector(pos.X, 0.5, pos.Z) } end
GameFlag = { HaveTrigger = 0 }
function Harness.CallsNamed(name)
	local hits = {}
	for _, c in ipairs(Harness.calls) do
		if c[1] == name then hits[#hits + 1] = c end
	end
	return hits
end

local function newPad()
	return { LX = 0, LY = 0, RX = 0, RY = 0, LMag = 0, RMag = 0, LTMag = 0, RTMag = 0,
		hold = {}, down = {}, up = {}, rept = {} }
end
local function newTouch()
	return { num = 0, data = { { x = 0, y = 0, id = -1 }, { x = 0, y = 0, id = -1 } } }
end
Input = { pad1 = newPad(), pad2 = newPad(), touch1 = newTouch(), touch2 = newTouch() }

CharacterMngr = { trico = nil, player = nil }
function CharacterMngr.GetTrico(self) return self.trico end
function CharacterMngr.GetPlayer(self) return self.player end

function Harness.Tick(dt, n)
	for _ = 1, n or 1 do
		for _, f in ipairs(Harness.ticks) do f(dt) end
	end
end

-- Decode light bar calls back to messages: { {pad, text}, ... }
function Harness.Messages()
	local out, partial = {}, {}
	for _, call in ipairs(Harness.lightbar) do
		local pad, v = call[1], call[2]
		if v == 0xC00FEE then v = nil end -- the scripts' hello, not text
		for _, code in ipairs(v and { math.floor(v / 65536), math.floor(v / 256) % 256, v % 256 } or {}) do
			if code == 10 then
				out[#out + 1] = { pad, string.gsub(partial[pad] or "", " +$", "") }
				partial[pad] = ""
			else
				partial[pad] = (partial[pad] or "") .. string.char(code)
			end
		end
	end
	return out
end

function Harness.Find(pattern)
	local hits = {}
	for _, m in ipairs(Harness.Messages()) do
		if string.find(m[2], pattern) then hits[#hits + 1] = m end
	end
	return hits
end

function check(cond, msg)
	if not cond then error("FAILED: " .. msg, 2) end
end
