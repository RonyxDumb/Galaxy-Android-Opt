#pragma once

namespace mobile_system {
// Open the app-private report before installing the signal handlers.
void installCrashHandler(const char* privateDirectory);
}
