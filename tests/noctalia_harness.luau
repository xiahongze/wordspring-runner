--!nonstrict
-- SOURCES, SETTINGS, and PROPS are supplied by test_noctalia.py.
local state = {}
local watchers = {}
local calls = {}
local notifications = {}
local copies = {}
local jsonValues = {}
local now = 1000
local accepted = true

local function clone(value)
  if type(value) ~= "table" then return value end
  local out = {}
  for key, item in value do out[key] = clone(item) end
  return out
end

local function encode(value)
  local key = "json:" .. tostring(#jsonValues + 1)
  table.insert(jsonValues, value)
  jsonValues[key] = clone(value)
  return key
end

local function loadEntry(path)
  local context = { renders = 0, vertical = false }
  local host = {
    getConfig = function(key)
      assert(SETTINGS[key] ~= nil, "Undeclared setting " .. tostring(key))
      return SETTINGS[key]
    end,
    string = { trim = function(value) return value:match("^%s*(.-)%s*$") end },
    json = { encode = encode, decode = function(value) return clone(jsonValues[value]) end },
    nowMs = function() return now end,
    expandPath = function(value) return value:gsub("^~/", "/test-home/") end,
    state = {
      get = function(key) return clone(state[key]) end,
      set = function(key, value)
        state[key] = clone(value)
        for _, callback in watchers[key] or {} do callback(clone(value)) end
      end,
      watch = function(key, callback)
        watchers[key] = watchers[key] or {}
        table.insert(watchers[key], callback)
      end,
    },
    runAsync = function(argv, callback, timeout)
      assert(type(argv) == "table", "Dynamic values must use argv execution")
      if not accepted then return false end
      table.insert(calls, { argv = clone(argv), callback = callback, timeout = timeout })
      return true
    end,
    copyToClipboard = function(text, mime) assert(mime == "text/plain"); table.insert(copies, text); return true end,
    notifyError = function(title, text) table.insert(notifications, {title, text}) end,
    openSettings = function() context.settingsOpened = true end,
    setUpdateInterval = function(ms) context.interval = ms end,
  }
  local ui = {}
  for kind, allowed in PROPS do
    ui[kind] = function(props, children)
      for key in props or {} do assert(allowed[key], "Unknown " .. kind .. " prop: " .. key) end
      return { type = kind, props = props or {}, children = children or {} }
    end
  end
  local globals = {
    noctalia = host, ui = ui,
    launcher = { setResults = function(query, rows) context.query = query; context.rows = rows end },
    panel = {
      render = function(tree) context.tree = tree; context.renders += 1 end,
      close = function() context.closed = true end,
      setNeedsFrameTick = function(enabled) context.ticking = enabled end,
    },
    barWidget = {
      setGlyph = function(value) context.glyph = value end,
      setText = function(value) context.text = value end,
      setTooltip = function(value) context.tooltip = value end,
      isVertical = function() return context.vertical end,
    },
  }
  setmetatable(globals, { __index = getfenv(0) })
  local cache = {}
  globals.require = function(modulePath)
    local canonical = modulePath:gsub("^%./", "")
    if cache[canonical] then return cache[canonical] end
    local chunk = assert(loadstring(SOURCES[canonical], canonical))
    -- Modules get their own globals with access to the same host.
    local moduleGlobals = setmetatable({}, { __index = globals })
    setfenv(chunk, moduleGlobals)
    cache[canonical] = chunk()
    return cache[canonical]
  end
  local chunk = assert(loadstring(SOURCES[path], path))
  setfenv(chunk, globals)
  chunk()
  return globals, context
end

local function complete(index, value, options)
  local result = { exitCode = 0, stdout = encode(value), stderr = "", timedOut = false, stdoutTruncated = false, stderrTruncated = false }
  for key, item in options or {} do result[key] = item end
  assert(calls[index].callback, "Expected a captured process callback")
  calls[index].callback(result)
end

local function response(word)
  return { protocol = 1, ok = true, results = {{ source = "/synthetic/" .. word .. ".ifo", id = "1", headword = word, label = "SYN", dictionary = "Synthetic", meanings = {"first meaning", "second meaning"}, preview = "preview" }}, warnings = {} }
end

local function article(word)
  local item = response(word).results[1]
  item.blocks = {{ role = "heading", text = word }, { role = "body", text = "Complete article text" }}
  item.links = {{ text = "related", query = "related" }}
  item.favorite = false
  item.text = "Complete article text"
  return { protocol = 1, ok = true, article = item }
end

local function find(tree, predicate)
  if predicate(tree) then return tree end
  for _, child in tree.children do
    local result = find(child, predicate)
    if result then return result end
  end
  return nil
end

local function button(context, text)
  return assert(find(context.tree, function(node) return node.type == "button" and node.props.text == text end), "Missing button " .. text)
end

local function contains(context, text)
  return find(context.tree, function(node) return type(node.props.text) == "string" and node.props.text:find(text, 1, true) ~= nil end) ~= nil
end

local launcher, lc = loadEntry("launcher.luau")
launcher.onQuery("")
assert(#calls == 0 and lc.rows[1].id == "hint", "Empty query must show a hint without spawning")
launcher.onQuery("older")
assert(lc.rows[1].glyph == "loader", "Show loading synchronously")
local old = #calls
launcher.onQuery("newer")
local new = #calls
complete(new, response("newer"))
complete(old, response("older"))
assert(lc.query == "newer" and lc.rows[1].title:find("newer", 1, true), "Stale query replaced results")
launcher.onActivate("entry:1")
assert(state.open_entry.query == "newer", "Stale query replaced the activation map")
assert(calls[#calls].argv[3] == "panel-open", "Open the full panel without toggling it shut")

launcher.onQuery("same"); local first = #calls
launcher.onQuery("between")
launcher.onQuery("same"); local last = #calls
complete(last, response("latest")); complete(first, response("stale"))
assert(lc.rows[1].title:find("latest", 1, true), "A -> B -> A race")
launcher.onQuery("missing")
complete(#calls, { protocol = 1, ok = false, error = { code = "not_found", message = "No entry found" } }, { exitCode = 1 })
assert(lc.rows[1].title:find("No entry found", 1, true))
launcher.onQuery("timeout"); complete(#calls, {}, { timedOut = true })
assert(lc.rows[1].title:find("timed out", 1, true))
launcher.onQuery("truncated"); complete(#calls, {}, { stdoutTruncated = true })
assert(lc.rows[1].title:find("output limit", 1, true))
accepted = false; launcher.onQuery("no binary"); accepted = true
assert(lc.rows[1].title:find("Could not start", 1, true))
launcher.onQuery("wrong protocol"); complete(#calls, { protocol = 99, ok = true })
assert(lc.rows[1].title:find("protocol 1", 1, true))

state.open_entry = nil
local panel, pc = loadEntry("panel.luau")
panel.onOpen("")
local count = #calls
local input = assert(find(pc.tree, function(node) return node.type == "input" end))
input.props.onChange("s")
input.props.onChange("second")
assert(#calls == count and pc.ticking, "Panel must debounce before spawning")
now += SETTINGS.panel_debounce_ms + 1
panel.onFrameTick("16.6")
assert(#calls == count + 1 and not pc.ticking)
assert(calls[#calls].argv[#calls[#calls].argv] == "second")
complete(#calls, response("second"))
complete(#calls, article("second"))
assert(contains(pc, "Complete article text"))
button(pc, "Copy word").props.onClick()
assert(copies[#copies] == "second")
button(pc, "Copy entry").props.onClick()
assert(copies[#copies] == "Complete article text")
button(pc, "Bookmark").props.onClick()
complete(#calls, { protocol = 1, ok = true, favorite = true })
assert(button(pc, "Remove bookmark"))
button(pc, "related").props.onClick()
assert(calls[#calls].argv[#calls[#calls].argv] == "related", "Internal links must use the same lookup path")
local late = #calls
panel.onClose()
local renders = pc.renders
complete(late, response("related"))
assert(pc.renders == renders, "Closed panels must ignore late results")

panel.onOpen("fresh")
assert(calls[#calls].argv[#calls[#calls].argv] == "fresh")
local a = #calls
panel.onIpc("lookup", "new query")
complete(a, response("fresh"))
complete(#calls, response("new query"))
complete(#calls, article("new query"))
assert(contains(pc, "new query"))
button(pc, "Dictionaries").props.onClick()
complete(#calls, { protocol = 1, ok = true, trigger = "define", dictionaries = {{ source = "/test.ifo", name = "Test", label = "T", enabled = true, available = true }} })
assert(contains(pc, "Shared with KRunner"))
button(pc, "Save dictionaries and shared settings").props.onClick()
assert(table.find(calls[#calls].argv, "--dictionaries-json"), "Dictionary management must use the CLI")
complete(#calls, { protocol = 1, ok = false, error = { message = "Settings are read-only" } }, { exitCode = 1 })
assert(contains(pc, "Settings are read-only"), "Configuration failures must be visible")
button(pc, "Bookmarks").props.onClick()
complete(#calls, { protocol = 1, ok = true, entries = {{ source = "/test.ifo", id = "2", headword = "saved word", dictionary = "Test" }} })
assert(contains(pc, "saved word"))

local service = loadEntry("service.luau")
service.onIpc("lookup", "a word with 'quotes' and $(characters)")
assert(state.open_entry == false, "A live panel should consume IPC context")
assert(calls[#calls].argv[3] == "panel-open")

SETTINGS.show_label = true
local widget, wc = loadEntry("widget.luau")
assert(wc.text == "Wordspring" and wc.glyph == "book-2")
wc.vertical = true; widget.update()
assert(wc.text == "", "Vertical bars must keep a compact glyph")
print("Luau callback tests passed: hints, loading, debounce, stale responses, errors, article/actions, IPC, settings, and orientation")
