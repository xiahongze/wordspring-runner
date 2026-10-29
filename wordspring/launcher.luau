--!nonstrict
local lookup = require("./lib/lookup.luau")
local generation = 0
local actions = {}
local lastQuery = ""

local function hint(query, title, subtitle, glyph)
  launcher.setResults(query, {{ id = "hint", title = title, subtitle = subtitle, glyph = glyph or "book-2" }})
end

function onQuery(query)
  generation += 1
  local request = generation
  actions = {}
  local text = noctalia.string.trim(query)
  lastQuery = text
  if text == "" then
    hint(query, "Look up an offline dictionary entry", "Type a word or phrase. Open the panel to manage dictionaries.")
    return
  end
  hint(query, "Looking up “" .. text .. "”…", "Searching enabled offline dictionaries", "loader")
  lookup.lookup(text, function(response, error)
    -- Host query-keying also protects a changed query; the generation protects
    -- the A -> B -> A case and this VM's activation map.
    if generation ~= request then return end
    if error then
      hint(query, "Wordspring: " .. lookup.message(error), "Press Enter to open the panel", "alert-triangle")
      return
    end
    local rows = {}
    for index, item in response.results do
      local id = "entry:" .. tostring(index)
      actions[id] = item
      local meanings = {}
      for number, meaning in item.meanings do
        if number > 4 then break end
        table.insert(meanings, tostring(number) .. ". " .. meaning)
      end
      table.insert(rows, {
        id = id,
        title = item.headword .. " · " .. item.label,
        subtitle = if #meanings > 0 then table.concat(meanings, "\n") else item.preview,
        glyph = "book-2",
      })
    end
    if response.warnings and #response.warnings > 0 then
      table.insert(rows, { id = "hint", title = "Some dictionaries are unavailable", subtitle = response.warnings[1].message, glyph = "alert-triangle" })
    end
    launcher.setResults(query, rows)
  end)
end

function onActivate(id)
  local item = actions[id]
  lookup.openPanel(if item then { source = item.source, id = item.id, query = item.headword } else { query = lastQuery })
end

function onIpc(event, payload)
  lookup.ipc(event, payload)
end
