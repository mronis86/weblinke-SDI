#pragma once

#include <string>

namespace weblinked {

/// Login-item registration so the engine starts with the user session.
///
/// This is not a Windows service and must not become one. Chromium, GPU and
/// DeckLink all need an interactive desktop; Session 0 has none of those.
/// A login item starts the same binary the operator would double-click, in
/// the tray, restoring whatever settings.json last saved.
///
/// Pure OS bookkeeping, no CEF, so the command-line shape is unit-tested.
namespace autostart {

/// Always true on the three desktop platforms this builds for.
bool supported();

/// Short label for the settings checkbox ("Start with Windows", …).
const char* label();

/// Absolute path of the running executable.
std::string executablePath();

/// Quoted command written into the login item, for this process.
std::string commandLine();

/// Same as commandLine(), for a path the tests can invent.
std::string commandLineFor(const std::string& executable);

#if !defined(_WIN32)
/// Linux .desktop / macOS LaunchAgent body. Exposed so a test can pin the
/// shape without writing into the caller's real login items.
std::string loginFileFor(const std::string& executable);
#endif

/// Whether *this* executable is registered to start at login.
bool enabled();

/// Register or withdraw the login item. `error` is set on failure.
bool setEnabled(bool enable, std::string* error = nullptr);

/// Makes the process working directory the folder that holds the executable.
///
/// A Startup-folder or `Run` key launch defaults to `C:\Windows\System32`.
/// Chromium has already been bitten by that once (see CHANGELOG v1.0.6).
/// Safe to call after CEF's subprocess check and before CefInitialize.
bool bindWorkingDirectory(std::string* error = nullptr);

}  // namespace autostart
}  // namespace weblinked
