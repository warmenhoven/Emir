#pragma once

namespace app::ui::widgets {

// Creates a "(?)" element with a simple text explanation.
void ExplanationTooltip(const char *explanation, bool sameLine = true);

// Creates a "/!\" element with a warning message.
void WarningTooltip(const char *message, bool sameLine = true);

} // namespace app::ui::widgets
