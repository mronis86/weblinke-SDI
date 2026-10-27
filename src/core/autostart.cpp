#include "core/autostart.h"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <unistd.h>
#else
#include <unistd.h>
#endif

namespace weblinked::autostart {
namespace {

bool fail(std::string* error, const std::string& message) {
  if (error != nullptr) {
    *error = message;
  }
  return false;
}

std::string quote(const std::string& path) {
  return "\"" + path + "\"";
}

bool iequals(const std::string& left, const std::string& right) {
  if (left.size() != right.size()) {
    return false;
  }
  for (size_t i = 0; i < left.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(left[i])) !=
        std::tolower(static_cast<unsigned char>(right[i]))) {
      return false;
    }
  }
  return true;
}

std::string unquote(std::string text) {
  if (text.size() >= 2 && text.front() == '"' && text.back() == '"') {
    return text.substr(1, text.size() - 2);
  }
  return text;
}

#if defined(_WIN32)

std::wstring widen(const std::string& text) {
  if (text.empty()) {
    return std::wstring();
  }
  const int size = ::MultiByteToWideChar(CP_UTF8, 0, text.c_str(),
                                         static_cast<int>(text.size()),
                                         nullptr, 0);
  std::wstring out(static_cast<size_t>(size), L'\0');
  ::MultiByteToWideChar(CP_UTF8, 0, text.c_str(),
                        static_cast<int>(text.size()), out.data(), size);
  return out;
}

std::string narrow(const std::wstring& text) {
  if (text.empty()) {
    return std::string();
  }
  const int size = ::WideCharToMultiByte(CP_UTF8, 0, text.c_str(),
                                         static_cast<int>(text.size()),
                                         nullptr, 0, nullptr, nullptr);
  std::string out(static_cast<size_t>(size), '\0');
  ::WideCharToMultiByte(CP_UTF8, 0, text.c_str(),
                        static_cast<int>(text.size()), out.data(), size,
                        nullptr, nullptr);
  return out;
}

constexpr wchar_t kRunKey[] =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kValueName[] = L"WebLinked";

bool openRunKey(HKEY* key, REGSAM access, std::string* error) {
  const LSTATUS status =
      ::RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, access, key);
  if (status != ERROR_SUCCESS) {
    return fail(error, "cannot open the Windows Run key");
  }
  return true;
}

#else

std::filesystem::path loginFilePath() {
#if defined(__APPLE__)
  const char* home = std::getenv("HOME");
  std::filesystem::path root = home != nullptr
                                   ? std::filesystem::path(home)
                                   : std::filesystem::temp_directory_path();
  return root / "Library" / "LaunchAgents" / "com.weblinked.engine.plist";
#else
  std::filesystem::path root;
  if (const char* xdg = std::getenv("XDG_CONFIG_HOME")) {
    root = std::filesystem::path(xdg);
  } else if (const char* home = std::getenv("HOME")) {
    root = std::filesystem::path(home) / ".config";
  } else {
    root = std::filesystem::temp_directory_path();
  }
  return root / "autostart" / "weblinked.desktop";
#endif
}

#endif

}  // namespace

bool supported() { return true; }

const char* label() {
#if defined(_WIN32)
  return "Start with Windows";
#elif defined(__APPLE__)
  return "Open at login";
#else
  return "Start when I log in";
#endif
}

std::string executablePath() {
#if defined(_WIN32)
  std::vector<wchar_t> buffer(MAX_PATH);
  for (int attempt = 0; attempt < 4; ++attempt) {
    const DWORD written = ::GetModuleFileNameW(
        nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (written == 0) {
      return {};
    }
    if (written < buffer.size() - 1) {
      return narrow(std::wstring(buffer.data(), written));
    }
    buffer.resize(buffer.size() * 2);
  }
  return {};
#elif defined(__APPLE__)
  uint32_t size = 0;
  _NSGetExecutablePath(nullptr, &size);
  std::string path(size, '\0');
  if (_NSGetExecutablePath(path.data(), &size) != 0) {
    return {};
  }
  path.resize(size > 0 && path[size - 1] == '\0' ? size - 1 : size);
  std::error_code ec;
  const auto canonical = std::filesystem::weakly_canonical(path, ec);
  return ec ? path : canonical.string();
#else
  std::error_code ec;
  const auto path = std::filesystem::read_symlink("/proc/self/exe", ec);
  return ec ? std::string{} : path.string();
#endif
}

std::string commandLineFor(const std::string& executable) {
  return quote(executable);
}

std::string commandLine() { return commandLineFor(executablePath()); }

#if !defined(_WIN32)
std::string loginFileFor(const std::string& executable) {
#if defined(__APPLE__)
  std::ostringstream out;
  out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
         "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
         "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
         "<plist version=\"1.0\">\n"
         "<dict>\n"
         "  <key>Label</key>\n"
         "  <string>com.weblinked.engine</string>\n"
         "  <key>ProgramArguments</key>\n"
         "  <array>\n"
         "    <string>"
      << executable
      << "</string>\n"
         "  </array>\n"
         "  <key>RunAtLoad</key>\n"
         "  <true/>\n"
         "</dict>\n"
         "</plist>\n";
  return out.str();
#else
  std::ostringstream out;
  out << "[Desktop Entry]\n"
         "Type=Application\n"
         "Name=WebLinked\n"
         "Exec="
      << quote(executable)
      << "\n"
         "X-GNOME-Autostart-enabled=true\n";
  return out.str();
#endif
}
#endif

bool enabled() {
  const std::string exe = executablePath();
  if (exe.empty()) {
    return false;
  }
#if defined(_WIN32)
  HKEY key = nullptr;
  if (!openRunKey(&key, KEY_QUERY_VALUE, nullptr)) {
    return false;
  }
  wchar_t data[1024];
  DWORD size = sizeof(data);
  DWORD type = 0;
  const LSTATUS status = ::RegQueryValueExW(key, kValueName, nullptr, &type,
                                            reinterpret_cast<LPBYTE>(data),
                                            &size);
  ::RegCloseKey(key);
  if (status != ERROR_SUCCESS || type != REG_SZ || size < sizeof(wchar_t)) {
    return false;
  }
  std::wstring raw(data, size / sizeof(wchar_t));
  if (!raw.empty() && raw.back() == L'\0') {
    raw.pop_back();
  }
  const std::string registered = unquote(narrow(raw));
  return iequals(registered, exe);
#else
  std::ifstream file(loginFilePath(), std::ios::binary);
  if (!file) {
    return false;
  }
  std::ostringstream text;
  text << file.rdbuf();
  return text.str().find(exe) != std::string::npos;
#endif
}

bool setEnabled(bool enable, std::string* error) {
  const std::string exe = executablePath();
  if (exe.empty()) {
    return fail(error, "cannot find this executable");
  }

#if defined(_WIN32)
  HKEY key = nullptr;
  if (!openRunKey(&key, KEY_SET_VALUE, error)) {
    return false;
  }
  if (!enable) {
    const LSTATUS status = ::RegDeleteValueW(key, kValueName);
    ::RegCloseKey(key);
    if (status != ERROR_SUCCESS && status != ERROR_FILE_NOT_FOUND) {
      return fail(error, "cannot remove the Windows login item");
    }
    return true;
  }
  const std::wstring command = widen(commandLineFor(exe));
  const LSTATUS status = ::RegSetValueExW(
      key, kValueName, 0, REG_SZ,
      reinterpret_cast<const BYTE*>(command.c_str()),
      static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
  ::RegCloseKey(key);
  if (status != ERROR_SUCCESS) {
    return fail(error, "cannot write the Windows login item");
  }
  return true;
#else
  const std::filesystem::path path = loginFilePath();
  if (!enable) {
    std::error_code ec;
    std::filesystem::remove(path, ec);
    return true;
  }
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  if (ec) {
    return fail(error, "cannot create " + path.parent_path().string());
  }
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  if (!file) {
    return fail(error, "cannot write " + path.string());
  }
  file << loginFileFor(exe);
  return static_cast<bool>(file);
#endif
}

bool bindWorkingDirectory(std::string* error) {
  const std::string exe = executablePath();
  if (exe.empty()) {
    return fail(error, "cannot find this executable");
  }
  const auto directory = std::filesystem::path(exe).parent_path();
  std::error_code ec;
  std::filesystem::current_path(directory, ec);
  if (ec) {
    return fail(error, "cannot use " + directory.string() + " as the working directory");
  }
  return true;
}

}  // namespace weblinked::autostart
