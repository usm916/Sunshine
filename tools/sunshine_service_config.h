/**
 * @file tools/sunshine_service_config.h
 * @brief Fixed service identity and Unicode host arguments for the Windows service wrapper.
 */
#pragma once

#include <algorithm>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sunshine_service {
  /** @brief Configuration supplied only by the registered service command line. */
  struct options_t {
    std::wstring name = L"SunshineService";  ///< Default official service identity.
    std::filesystem::path config_directory;  ///< Empty preserves the official package-local configuration.
  };

  /**
   * @brief Quote one argument for the native Windows command-line parser, without a shell.
   * @param value Literal argument, including any spaces, quotes or trailing backslashes.
   * @return Quoted and escaped argument.
   */
  inline std::wstring quote_argument(std::wstring_view value) {
    std::wstring result = L"\"";
    std::size_t slashes = 0;
    for (const auto ch : value) {
      if (ch == L'\\') {
        ++slashes;
      } else {
        result.append(slashes * (ch == L'"' ? 2 : 1), L'\\');
        if (ch == L'"') {
          result += L'\\';
        }
        result += ch;
        slashes = 0;
      }
    }
    result.append(slashes * 2, L'\\');
    result += L'"';
    return result;
  }

  /**
   * @brief Validate service-only options; reject unknown, duplicate and relative path arguments.
   * @param arguments Native arguments excluding the executable name.
   * @return Valid options, or no value for malformed arguments.
   */
  inline std::optional<options_t> parse_options(std::span<const std::wstring> arguments) {
    options_t result;
    bool has_name = false;
    bool has_config = false;
    if (arguments.size() % 2 != 0) {
      return std::nullopt;
    }
    for (std::size_t i = 0; i < arguments.size(); i += 2) {
      const auto &key = arguments[i];
      const auto &value = arguments[i + 1];
      if (value.empty()) {
        return std::nullopt;
      }
      if (key == L"--service-name" && !has_name) {
        if (value.size() > 128 || !std::all_of(value.begin(), value.end(), [](wchar_t ch) {
              return (ch >= L'A' && ch <= L'Z') || (ch >= L'a' && ch <= L'z') || (ch >= L'0' && ch <= L'9') || ch == L'-' || ch == L'_';
            })) {
          return std::nullopt;
        }
        result.name = value;
        has_name = true;
      } else if (key == L"--config-dir" && !has_config) {
        result.config_directory = value;
        if (!result.config_directory.is_absolute() || value.find_first_of(L"\"\r\n") != std::wstring::npos) {
          return std::nullopt;
        }
        has_config = true;
      } else {
        return std::nullopt;
      }
    }
    return result;
  }

  /**
   * @brief Pin credentials, pairing, applications and log paths to the original user's store.
   * @param executable Fixed absolute host executable beside the tools directory.
   * @param options Validated service options.
   * @return Host command line consumed directly by CreateProcessAsUserW.
   */
  inline std::wstring host_command(const std::filesystem::path &executable, const options_t &options) {
    auto result = quote_argument(executable.wstring());
    if (!options.config_directory.empty()) {
      const auto &root = options.config_directory;
      const std::vector<std::wstring> arguments {
        (root / L"sunshine.conf").wstring(),
        L"file_state=" + (root / L"sunshine_state.json").wstring(),
        L"credentials_file=" + (root / L"sunshine_state.json").wstring(),
        L"pkey=" + (root / L"credentials/cakey.pem").wstring(),
        L"cert=" + (root / L"credentials/cacert.pem").wstring(),
        L"file_apps=" + (root / L"apps.json").wstring(),
        L"log_path=" + (root / L"sunshine.log").wstring(),
      };
      for (const auto &argument : arguments) {
        result += L' ';
        result += quote_argument(argument);
      }
    }
    return result;
  }
}  // namespace sunshine_service
