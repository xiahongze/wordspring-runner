--!nonstrict
-- No widget or open panel is needed for this singleton IPC endpoint.
local lookup = require("./lib/lookup.luau")

function onIpc(event, payload)
  lookup.ipc(event, payload)
end
