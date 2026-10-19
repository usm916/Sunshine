/**
 * @file tests/unit/platform/windows/test_service_config.cpp
 * @brief Verify service option validation and real Windows argument parsing without changing services.
 */
#include "../../../tests_common.h"

#ifdef _WIN32
  #include <Windows.h>

  // Shell declarations require the Windows base types above.
  #include <shellapi.h>
  #include <tools/sunshine_service_config.h>

/**
 * @brief Decode the command line with the same native parser used by the service entry point.
 * @param command Complete Unicode command line.
 * @return Parsed arguments.
 */
static std::vector<std::wstring> decode_service_command(const std::wstring &command) {
  int count = 0;
  auto native = CommandLineToArgvW(command.c_str(), &count);
  if (!native) {
    return {};
  }
  const std::vector<std::wstring> result(native, native + count);
  LocalFree(native);
  return result;
}

TEST(ServiceConfig, DefaultPreservesOfficialServiceAndPackageLocalConfiguration) {
  const auto parsed = sunshine_service::parse_options({});
  ASSERT_TRUE(parsed);
  EXPECT_EQ(parsed->name, L"SunshineService");
  EXPECT_TRUE(parsed->config_directory.empty());
  EXPECT_EQ(decode_service_command(sunshine_service::host_command(L"D:\\package\\sunshine.exe", *parsed)), std::vector<std::wstring>({L"D:\\package\\sunshine.exe"}));
}

TEST(ServiceConfig, AcceptsUnicodeAbsoluteConfigurationAndEitherOptionOrder) {
  const std::vector<std::wstring> arguments {L"--config-dir", L"D:\\共有 settings\\", L"--service-name", L"SunshineCustom_Service-1"};
  const auto parsed = sunshine_service::parse_options(arguments);
  ASSERT_TRUE(parsed);
  EXPECT_EQ(parsed->name, L"SunshineCustom_Service-1");
  EXPECT_EQ(parsed->config_directory, std::filesystem::path(arguments[1]));
  const std::vector<std::wstring> reversed {arguments[2], arguments[3], arguments[0], arguments[1]};
  EXPECT_TRUE(sunshine_service::parse_options(reversed));
  EXPECT_TRUE(sunshine_service::parse_options(std::vector<std::wstring> {L"--service-name", std::wstring(128, L'A')}));
}

TEST(ServiceConfig, RejectsUnknownMissingDuplicateAndInvalidServiceOptions) {
  const std::vector<std::vector<std::wstring>> invalid {
    {L"--service-name"},
    {L"--service-name", L""},
    {L"--config-dir", L""},
    {L"--unknown", L"value"},
    {L"--service-name", L"Custom", L"--service-name", L"Other"},
    {L"--config-dir", L"D:\\config", L"--config-dir", L"D:\\other"},
    {L"--service-name", L"bad name"},
    {L"--service-name", L"bad/name"},
    {L"--service-name", std::wstring(129, L'A')},
    {L"--config-dir", L"relative\\config"},
    {L"--config-dir", L"D:relative"},
    {L"--config-dir", L"D:\\bad\"path"},
    {L"--config-dir", L"D:\\bad\rpath"},
    {L"--config-dir", L"D:\\bad\npath"},
  };
  for (const auto &arguments : invalid) {
    EXPECT_FALSE(sunshine_service::parse_options(arguments));
  }
}

TEST(ServiceConfig, NativeQuotingRoundTripsLiteralMetacharactersAndTrailingBackslashes) {
  const std::vector<std::wstring> values {L"", L"simple", L"with spaces", L"a\"b", L"a\\\"b", L"D:\\trailing\\", L"D:\\two\\\\", L"日本語 'single'", L"$(throw 'no'); & exit 9"};
  auto command = std::wstring(L"sunshine.exe");
  for (const auto &value : values) {
    command += L' ' + sunshine_service::quote_argument(value);
  }
  auto decoded = decode_service_command(command);
  ASSERT_EQ(decoded.size(), values.size() + 1);
  decoded.erase(decoded.begin());
  EXPECT_EQ(decoded, values);
}

TEST(ServiceConfig, SharedStorePinsEveryPersistentPathWithoutShellInterpretation) {
  sunshine_service::options_t options;
  options.name = L"SunshineCustomService";
  options.config_directory = L"D:\\共有 user's settings";
  const auto parsed = decode_service_command(sunshine_service::host_command(L"D:\\build's folder\\sunshine.exe", options));
  ASSERT_EQ(parsed.size(), 8);
  EXPECT_EQ(parsed[0], L"D:\\build's folder\\sunshine.exe");
  EXPECT_EQ(parsed[1], (options.config_directory / L"sunshine.conf").wstring());
  const std::vector<std::wstring> keys {L"file_state=", L"credentials_file=", L"pkey=", L"cert=", L"file_apps=", L"log_path="};
  for (std::size_t i = 0; i < keys.size(); ++i) {
    EXPECT_TRUE(parsed[i + 2].starts_with(keys[i]));
    EXPECT_TRUE(parsed[i + 2].substr(keys[i].size()).starts_with(options.config_directory.wstring()));
  }
  EXPECT_TRUE(parsed[2].ends_with(L"sunshine_state.json"));
  EXPECT_TRUE(parsed[3].ends_with(L"sunshine_state.json"));
  EXPECT_TRUE(parsed[4].ends_with(L"cakey.pem"));
  EXPECT_TRUE(parsed[5].ends_with(L"cacert.pem"));
  EXPECT_TRUE(parsed[6].ends_with(L"apps.json"));
  EXPECT_TRUE(parsed[7].ends_with(L"sunshine.log"));
}
#endif
