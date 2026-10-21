-- tlg-coop: text channel from the scripts to the emulator log.
--
-- The retail game compiles GaPrint to nothing, so messages travel through the light bar:
-- SetLightBar(pad, "rrggbb") reaches scePadSetLightBar, which the emulator logs at Debug level
-- (filter "Lib.Pad:Debug"). Each call carries three characters; a message ends with "\n".
-- tools: tlg_coop.debuglog decodes the log.

CoopDebug = CoopDebug or {
	enabled = false,  -- coop_config.lua turns it on for test runs
	pad = 1,          -- 0 = first pad (always open), 1 = second pad (open with the co-op patch)
	queue = {},
	perFrame = 120,   -- light bar calls per frame at most
}

-- Character codes without string.byte (the game itself never uses it).
local CODE = {}
for c = 32, 126 do CODE[string.char(c)] = c end
CODE["\n"] = 10

local function hex3(text, i)
	return string.format("%02x%02x%02x", CODE[string.sub(text, i, i)] or 63,
		CODE[string.sub(text, i + 1, i + 1)] or 63, CODE[string.sub(text, i + 2, i + 2)] or 63)
end

function CoopDebug.Print(fmt, ...)
	if not CoopDebug.enabled then return end
	local ok, text = pcall(string.format, tostring(fmt), unpack(arg))
	if not ok then text = tostring(fmt) end
	CoopDebug.Raw(CoopDebug.pad, text)
end

-- Queue one message for a pad without formatting.
function CoopDebug.Raw(pad, text)
	text = string.gsub(text, "[^\032-\126]", "?")
	-- pad before the newline so the next message starts on a fresh call
	while (string.len(text) + 1) % 3 ~= 0 do text = text .. " " end
	text = text .. "\n"
	local q = CoopDebug.queue
	for i = 1, string.len(text), 3 do
		q[#q + 1] = { pad, hex3(text, i) }
	end
end

function CoopDebug.Flush()
	local q = CoopDebug.queue
	local n = #q
	if n == 0 then return end
	local count = math.min(n, CoopDebug.perFrame)
	for i = 1, count do
		SetLightBar(q[i][1], q[i][2])
	end
	local rest = {}
	for i = count + 1, n do rest[#rest + 1] = q[i] end
	CoopDebug.queue = rest
end

-- Describe a value in one short line.
function CoopDebug.Describe(v)
	local t = type(v)
	if t == "number" then return string.format("%.3f", v) end
	if t == "string" then return '"' .. string.sub(v, 1, 40) .. '"' end
	if t == "userdata" then return "obj:" .. string.sub(tostring(v), 1, 40) end
	return t
end

-- Print the keys of a table (sorted), several per line.
function CoopDebug.DumpTable(label, tbl, limit)
	if type(tbl) ~= "table" then
		CoopDebug.Print("%s = %s", label, CoopDebug.Describe(tbl))
		return
	end
	local keys = {}
	for k, v in pairs(tbl) do
		keys[#keys + 1] = tostring(k) .. "=" .. CoopDebug.Describe(v)
	end
	table.sort(keys)
	limit = limit or 200
	local line = label .. ":"
	for i = 1, math.min(#keys, limit) do
		if string.len(line) + string.len(keys[i]) > 160 then
			CoopDebug.Print("%s", line)
			line = "  "
		end
		line = line .. " " .. keys[i]
	end
	CoopDebug.Print("%s (%d keys)", line, #keys)
end

-- Print the engine properties of an engine object.
function CoopDebug.DumpObject(label, obj, limit)
	if obj == nil then
		CoopDebug.Print("%s = nil", label)
		return
	end
	local ok, props = pcall(GetObjPropsInfo, tostring(obj))
	if not ok or type(props) ~= "table" then
		CoopDebug.Print("%s: no props (%s)", label, tostring(props))
		return
	end
	local values = {}
	for k, _ in pairs(props) do
		local okv, v = pcall(function() return obj[k] end)
		values[k] = okv and v or "?"
	end
	CoopDebug.DumpTable(label .. " <" .. tostring(obj) .. ">", values, limit)
end
