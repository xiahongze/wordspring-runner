--!nonstrict
local lookup = require("./lib/lookup.luau")

function update()
  noctalia.setUpdateInterval(1000)
  barWidget.setGlyph(noctalia.getConfig("glyph"))
  barWidget.setText(if noctalia.getConfig("show_label") and not barWidget.isVertical() then "Wordspring" else "")
  barWidget.setTooltip("Wordspring · offline dictionary")
end

-- The manifest's panel-toggle action is configurable by the user. The host
-- dispatches it with this widget's bar/output context.
function onIpc(event, payload)
  lookup.ipc(event, payload)
end

update()
