// Login-item text, not the OS write. Writing the real Run key from a unit
// test would change the machine that is running the suite.

#include "core/autostart.h"
#include "test_support.h"

using namespace weblinked;

WEBLINKED_TEST(autostart_command_quotes_the_executable) {
  const std::string command =
      autostart::commandLineFor("C:\\Program Files\\WebLinked\\weblinked.exe");
  CHECK_STR(command, "\"C:\\Program Files\\WebLinked\\weblinked.exe\"");
}

WEBLINKED_TEST(autostart_reports_a_platform_label) {
  CHECK(autostart::supported());
  CHECK(autostart::label() != nullptr);
  CHECK(autostart::label()[0] != '\0');
}

#if !defined(_WIN32)
WEBLINKED_TEST(autostart_login_file_names_the_executable) {
  const std::string body =
      autostart::loginFileFor("/opt/WebLinked/weblinked");
  CHECK(body.find("/opt/WebLinked/weblinked") != std::string::npos);
}
#endif
