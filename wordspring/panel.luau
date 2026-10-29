--!nonstrict
local lookup = require("./lib/lookup.luau")
local opened = false
local generation = 0
local inputRevision = 0
local dictionaryRevision = 0
local query = ""
local filter = ""
local view = "search"
local pendingAt = nil
local results = {}
local article = nil
local saved = {}
local dictionaries = {}
local importPath = ""
local status = "Type a word or phrase to search your offline dictionaries."
local problem = nil
local busy = false
local confirmation = nil
local render
local search
local openEntry
local loadSaved
local loadDictionaries

local function label(text, color)
  return ui.label({ text = text, color = color or "on_surface", maxLines = 0 })
end

local function notice(response)
  if response.warnings and #response.warnings > 0 then
    problem = response.warnings[1].message
  end
end

local function invalidate()
  generation += 1
  pendingAt = nil
  panel.setNeedsFrameTick(false)
  problem = nil
  confirmation = nil
  return generation
end

local function current(request)
  return opened and request == generation
end

local function performSearch()
  local request = generation
  local text = query
  pendingAt = nil
  panel.setNeedsFrameTick(false)
  lookup.lookup(text, function(response, error)
    if not current(request) then return end
    if error then
      results = {}
      article = nil
      problem = lookup.message(error)
      status = ""
      render()
      return
    end
    results = response.results
    status = tostring(#results) .. " matching entries"
    notice(response)
    if #results > 0 then openEntry(results[1], false) else render() end
  end)
end

search = function(text, immediate, replaceInput)
  invalidate()
  view = "search"
  query = noctalia.string.trim(text)
  if replaceInput then inputRevision += 1 end
  article = nil
  results = {}
  if query == "" then
    status = "Type a word or phrase to search your offline dictionaries."
  else
    status = "Looking up “" .. query .. "”…"
    if immediate then
      performSearch()
    else
      pendingAt = noctalia.nowMs() + noctalia.getConfig("panel_debounce_ms")
      panel.setNeedsFrameTick(true)
    end
  end
  render()
end

openEntry = function(item, replaceInput)
  local previousProblem = problem
  local request = invalidate()
  problem = previousProblem
  view = "search"
  if replaceInput then
    query = item.headword or query
    inputRevision += 1
  end
  article = nil
  status = "Opening dictionary entry…"
  render()
  lookup.entry(item, function(response, error)
    if not current(request) then return end
    if error then
      problem = lookup.message(error)
      status = ""
    else
      article = response.article
      status = article.dictionary
    end
    render()
  end)
end

local function mutate(args, success, timeout)
  if busy then return end
  busy = true
  problem = nil
  render()
  lookup.call(args, function(response, error)
    busy = false
    if error then
      if opened then problem = lookup.message(error); render()
      else noctalia.notifyError("Wordspring", lookup.message(error)) end
      return
    end
    if opened then success(response) end
  end, timeout)
end

loadSaved = function(kind)
  local request = invalidate()
  view = kind
  saved = {}
  status = "Loading saved words…"
  render()
  lookup.call({ kind, "--filter", filter }, function(response, error)
    if not current(request) then return end
    if error then problem = lookup.message(error); status = ""
    else saved = response.entries; status = tostring(#saved) .. " saved entries" end
    render()
  end)
end

loadDictionaries = function()
  local request = invalidate()
  view = "dictionaries"
  status = "Loading dictionary settings…"
  render()
  lookup.call({ "config" }, function(response, error)
    if not current(request) then return end
    if error then problem = lookup.message(error); status = ""
    else
      dictionaries = response.dictionaries
      dictionaryRevision += 1
      status = "Shared with KRunner · keyword: " .. response.trigger
    end
    render()
  end)
end

local function toggleFavorite(item)
  if busy then return end
  local target = not item.favorite
  local request = generation
  busy = true
  lookup.favorite(item, target, function(response, error)
    busy = false
    if error then
      if opened then problem = lookup.message(error); render()
      else noctalia.notifyError("Wordspring", lookup.message(error)) end
      return
    end
    if current(request) and article == item then article.favorite = response.favorite end
    if opened then render() end
  end)
  render()
end

local function moveDictionary(index, direction)
  local nextIndex = index + direction
  if nextIndex < 1 or nextIndex > #dictionaries then return end
  dictionaries[index], dictionaries[nextIndex] = dictionaries[nextIndex], dictionaries[index]
  dictionaryRevision += 1
  render()
end

local function importDictionary(path)
  if busy or noctalia.string.trim(path) == "" then return end
  busy = true
  problem = nil
  status = "Importing dictionary…"
  render()
  lookup.import(noctalia.expandPath(path), function(response, error)
    busy = false
    if error then
      if opened then problem = lookup.message(error); status = ""; render()
      else noctalia.notifyError("Wordspring", lookup.message(error)) end
      return
    end
    if opened then
      dictionaries = response.dictionaries
      dictionaryRevision += 1
      status = "Import finished."
      notice(response)
      render()
    end
  end)
end

local function searchBody()
  local body = {}
  for index, item in results do
    table.insert(body, ui.button({
      key = "result:" .. tostring(index), text = item.headword .. " · " .. item.label,
      glyph = "book-2", contentAlign = "start",
      selected = article ~= nil and article.source == item.source and article.id == item.id,
      onClick = function() openEntry(item, false) end,
    }))
  end
  if article then
    local item = article -- callbacks retain this entry, never a mutable global reference
    table.insert(body, ui.separator())
    table.insert(body, ui.row({ gap = 8 }, {
      ui.button({ text = if item.favorite then "Remove bookmark" else "Bookmark", glyph = "bookmark", enabled = not busy, onClick = function() toggleFavorite(item) end }),
      ui.button({ text = "Copy word", onClick = function() lookup.copy(item.headword) end }),
      ui.button({ text = "Copy entry", onClick = function() lookup.copy(item.text) end }),
    }))
    if #item.blocks == 0 or item.blocks[1].role ~= "heading" then
      table.insert(body, ui.label({ text = item.headword, fontSize = 26, fontWeight = "bold", maxLines = 0 }))
    end
    for index, block in item.blocks do
      local heading = block.role == "heading"
      local section = block.role == "section" or block.role == "part_of_speech"
      local muted = block.role == "example" or block.role == "pronunciation" or block.role == "origin"
      table.insert(body, ui.label({
        key = "block:" .. tostring(index), text = block.text, maxLines = 0,
        fontSize = if heading then 26 elseif section then 16 else 14,
        fontWeight = if heading then "bold" elseif section or block.role == "note" then "semibold" else "normal",
        color = if muted then "on_surface_variant" else "on_surface",
      }))
    end
    if #item.links > 0 then
      table.insert(body, ui.separator())
      table.insert(body, label("Related words", "on_surface_variant"))
      for _, link in item.links do
        table.insert(body, ui.button({ text = link.text, glyph = "search", contentAlign = "start", onClick = function() search(link.query, true, true) end }))
      end
    end
  end
  return body
end

local function savedBody()
  local body = {}
  for index, item in saved do
    table.insert(body, ui.row({ gap = 8 }, {
      ui.button({ key = "saved:" .. tostring(index), text = item.headword .. " · " .. item.dictionary, contentAlign = "start", flexGrow = 1,
        onClick = function() results = {}; openEntry(item, true) end }),
      if view == "favorites" then ui.button({ glyph = "bookmark-off", tooltip = "Remove bookmark", enabled = not busy, onClick = function()
        mutate({ "favorite", "--source", item.source, "--id", item.id, "--enabled", "false" }, function() loadSaved("favorites") end)
      end }) else ui.spacer({ width = 0, flexGrow = 0 }),
    }))
  end
  if #saved == 0 then table.insert(body, label("No saved entries match this filter.", "on_surface_variant")) end
  if view == "history" then
    table.insert(body, ui.button({ text = "Clear history", variant = "destructive", enabled = not busy,
      onClick = function() confirmation = { kind = "history" }; render() end }))
  end
  return body
end

local function dictionariesBody()
  local body = {
    label("Enable, label, and reorder dictionaries, then save. Removing a dictionary deletes its imported copy; original files are kept.", "on_surface_variant"),
    ui.button({ text = "Save dictionaries and shared settings", enabled = not busy, onClick = function()
      local encoded, error = noctalia.json.encode(dictionaries)
      if not encoded then problem = error; render(); return end
      local args = lookup.sharedSettings()
      table.insert(args, "--dictionaries-json"); table.insert(args, encoded)
      mutate(args, function() loadDictionaries() end)
    end }),
  }
  local override = noctalia.string.trim(noctalia.getConfig("dictionaries_json"))
  if override ~= "" then table.insert(body, label("A dictionary JSON override is active in plugin settings. It takes precedence for lookups.", "primary")) end
  for index, item in dictionaries do
    table.insert(body, ui.separator())
    table.insert(body, label(item.name .. (if item.available then "" else " · imported copy unavailable")))
    table.insert(body, label(item.source, "on_surface_variant"))
    table.insert(body, ui.row({ gap = 8 }, {
      ui.toggle({ checked = item.enabled, enabled = not busy, onChange = function(value) item.enabled = value == "true" end }),
      ui.input({ key = "label:" .. tostring(dictionaryRevision) .. ":" .. tostring(index), value = item.label, placeholder = "Short label", enabled = not busy,
        onChange = function(value) item.label = value end }),
      ui.button({ glyph = "arrow-up", enabled = not busy and index > 1, onClick = function() moveDictionary(index, -1) end }),
      ui.button({ glyph = "arrow-down", enabled = not busy and index < #dictionaries, onClick = function() moveDictionary(index, 1) end }),
      ui.button({ text = "Reimport", enabled = not busy, onClick = function() importDictionary(item.source) end }),
      ui.button({ glyph = "trash", tooltip = "Remove dictionary", enabled = not busy, onClick = function() confirmation = { kind = "remove", source = item.source }; render() end }),
    }))
  end
  table.insert(body, ui.separator())
  table.insert(body, label("Add an Apple .dictionary, StarDict .ifo, or a folder containing dictionaries."))
  table.insert(body, ui.input({ key = "import-path", value = importPath, placeholder = "Dictionary or folder path", enabled = not busy,
    onChange = function(value) importPath = value end, onSubmit = function(value) importDictionary(value) end }))
  table.insert(body, ui.button({ text = "Import", glyph = "folder-plus", enabled = not busy, onClick = function() importDictionary(importPath) end }))
  table.insert(body, label("Imports are local. Apple imports require your configured Python environment. Imports taking more than 55 seconds can be run with wordspring-lookup in a terminal.", "on_surface_variant"))
  return body
end

render = function()
  if not opened then return end
  local children = {
    ui.row({ gap = 8 }, {
      ui.label({ text = "Wordspring", fontSize = 20, fontWeight = "bold", flexGrow = 1 }),
      ui.button({ glyph = "settings", tooltip = "Plugin settings", onClick = function() noctalia.openSettings() end }),
      ui.button({ glyph = "x", tooltip = "Close", onClick = function() panel.close() end }),
    }),
    ui.row({ gap = 8 }, {
      ui.button({ text = "Search", selected = view == "search", onClick = function() search(query, true, true) end }),
      ui.button({ text = "Bookmarks", selected = view == "favorites", onClick = function() loadSaved("favorites") end }),
      ui.button({ text = "History", selected = view == "history", onClick = function() loadSaved("history") end }),
      ui.button({ text = "Dictionaries", selected = view == "dictionaries", onClick = loadDictionaries }),
    }),
  }
  if view == "search" then
    table.insert(children, ui.input({ key = "query:" .. tostring(inputRevision), value = query, placeholder = "Look up a word or phrase", focus = true,
      onChange = function(value) search(value, false, false) end, onSubmit = function(value) search(value, true, false) end }))
  elseif view == "favorites" or view == "history" then
    table.insert(children, ui.input({ key = "filter:" .. view, value = filter, placeholder = "Filter saved words; press Enter", focus = true,
      onChange = function(value) filter = value end, onSubmit = function(value) filter = value; loadSaved(view) end }))
  end
  if status ~= "" then table.insert(children, label(status, "on_surface_variant")) end
  if problem then table.insert(children, label(problem, "error")) end
  if confirmation then
    local action = confirmation
    table.insert(children, label(if action.kind == "history" then "Clear all lookup history? Bookmarks will be kept." else "Remove this dictionary's imported copy? Original files and bookmarks will be kept.", "primary"))
    table.insert(children, ui.row({ gap = 8 }, {
      ui.button({ text = "Confirm", variant = "destructive", enabled = not busy, onClick = function()
        confirmation = nil
        if action.kind == "history" then mutate({ "clear-history" }, function() loadSaved("history") end)
        else mutate({ "remove", "--source", action.source }, function() loadDictionaries() end) end
      end }),
      ui.button({ text = "Cancel", onClick = function() confirmation = nil; render() end }),
    }))
  end
  local body = if view == "search" then searchBody() elseif view == "dictionaries" then dictionariesBody() else savedBody()
  table.insert(children, ui.scroll({ key = "body:" .. view, flexGrow = 1, gap = 10, align = "stretch" }, body))
  panel.render(ui.column({ flexGrow = 1, gap = 12, align = "stretch" }, children))
end

local function acceptRequest(request)
  if type(request) ~= "table" then return end
  if request.source and request.id then
    query = request.query or ""
    results = {}
    openEntry({ source = request.source, id = request.id, headword = query }, true)
  else
    search(request.query or "", true, true)
  end
end

noctalia.state.watch("open_entry", function(request)
  if opened and type(request) == "table" then
    acceptRequest(request)
    noctalia.state.set("open_entry", false)
  end
end)

function onOpen(context)
  opened = true
  local request = noctalia.state.get("open_entry")
  if type(request) == "table" then
    noctalia.state.set("open_entry", false)
    acceptRequest(request)
  elseif context and context ~= "" then
    search(context, true, true)
  else
    if view == "search" then search(query, query ~= "", true)
    elseif view == "dictionaries" then loadDictionaries()
    else loadSaved(view) end
  end
end

function onClose()
  opened = false
  invalidate()
end

function onFrameTick(_deltaMs)
  if pendingAt and noctalia.nowMs() >= pendingAt then performSearch() end
end

function onIpc(event, payload)
  if (event == "open" or event == "lookup") and opened then
    search(payload or "", true, true)
  else lookup.ipc(event, payload) end
end
