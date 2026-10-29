--!nonstrict
-- Every entry requires this module; each VM gets its own instance. Durable data
-- belongs to the CLI's shared SQLite store, and cross-entry context uses state.
local M = {}
M.panelId = "xiahongze/wordspring:panel"

local function failure(code, message)
  return { code = code, message = message }
end

function M.call(args, callback, timeout)
  local binary = noctalia.string.trim(noctalia.getConfig("cli_path"))
  if binary == "" then binary = "wordspring-lookup" end
  if binary:find("/", 1, true) then binary = noctalia.expandPath(binary) end
  local argv = { binary }
  local config = noctalia.string.trim(noctalia.getConfig("config_path"))
  if config ~= "" then
    table.insert(argv, "--config")
    table.insert(argv, noctalia.expandPath(config))
  end
  local override = noctalia.string.trim(noctalia.getConfig("dictionaries_json"))
  if override ~= "" and (args[1] == "lookup" or args[1] == "entry" or args[1] == "favorite") then
    table.insert(argv, "--dictionaries-json")
    table.insert(argv, override)
  end
  for _, argument in args do table.insert(argv, tostring(argument)) end
  local duration = timeout or noctalia.getConfig("timeout_ms")
  local accepted = noctalia.runAsync(argv, function(result)
    if result.timedOut then
      callback(nil, failure("timeout", "Wordspring timed out. Try again or increase the timeout in plugin settings."))
      return
    end
    if result.stdoutTruncated or result.stderrTruncated then
      callback(nil, failure("output_truncated", "The CLI response exceeded Noctalia's output limit."))
      return
    end
    local value = noctalia.json.decode(result.stdout)
    if type(value) ~= "table" then
      local detail = noctalia.string.trim(result.stderr or "")
      callback(nil, failure("cli_unavailable", "Could not read the Wordspring CLI response. Check the CLI path. " .. detail))
    elseif value.protocol ~= 1 then
      callback(nil, failure("protocol_mismatch", "This plugin needs Wordspring JSON protocol 1. Install the matching CLI release."))
    elseif result.exitCode ~= 0 or value.ok ~= true then
      callback(nil, value.error or failure("cli_error", "Wordspring exited unsuccessfully."))
    else
      callback(value, nil)
    end
  end, duration)
  if not accepted then
    callback(nil, failure("cli_unavailable", "Could not start Wordspring. Check the CLI path in plugin settings."))
  end
end

function M.lookup(query, callback)
  M.call({ "lookup", "--limit", tostring(noctalia.getConfig("limit_per_dictionary")), "--", query }, callback)
end

function M.entry(item, callback)
  M.call({ "entry", "--source", item.source, "--id", item.id, "--record-visit" }, callback)
end

function M.favorite(item, enabled, callback)
  M.call({ "favorite", "--source", item.source, "--id", item.id, "--enabled", tostring(enabled) }, callback)
end

function M.openPanel(request)
  -- publish before opening: a newly created panel reads it in onOpen; a live
  -- panel sees the watch. No shared Lua memory is assumed.
  noctalia.state.set("open_entry", request or { query = "" })
  local accepted = noctalia.runAsync({ "noctalia", "msg", "panel-open", M.panelId }, function(result)
    if result.exitCode ~= 0 then
      noctalia.notifyError("Wordspring", "Could not open the dictionary panel: " .. (result.stderr or ""))
    end
  end)
  if not accepted then noctalia.notifyError("Wordspring", "Could not open the dictionary panel.") end
end

function M.ipc(event, payload)
  if event == "open" or event == "lookup" then
    M.openPanel({ query = payload or "" })
  elseif event == "settings" then
    noctalia.openSettings()
  else
    noctalia.notifyError("Wordspring", "Unknown IPC event: " .. tostring(event))
  end
end

function M.copy(text)
  if not noctalia.copyToClipboard(text, "text/plain") then
    noctalia.notifyError("Wordspring", "Could not copy to the clipboard.")
  end
end

function M.sharedSettings()
  local args = { "configure" }
  local keyword = noctalia.string.trim(noctalia.getConfig("krunner_keyword"))
  local python = noctalia.string.trim(noctalia.getConfig("python_executable"))
  if keyword ~= "" then table.insert(args, "--trigger"); table.insert(args, keyword) end
  if python ~= "" then table.insert(args, "--python"); table.insert(args, python) end
  return args
end

function M.import(path, callback)
  local args = { "import" }
  local python = noctalia.string.trim(noctalia.getConfig("python_executable"))
  if python ~= "" then table.insert(args, "--python"); table.insert(args, noctalia.expandPath(python)) end
  -- End the importer before the host's maximum 60s process timeout. Longer
  -- imports can be run directly with the same CLI, outside the shell.
  table.insert(args, "--timeout-ms"); table.insert(args, "55000")
  table.insert(args, "--"); table.insert(args, path)
  M.call(args, callback, 60000)
end

function M.message(error)
  return if type(error) == "table" then tostring(error.message or error.code) else tostring(error)
end

return M
