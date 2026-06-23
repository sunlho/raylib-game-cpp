#pragma once

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Assets {

inline std::filesystem::path Root() {
  return std::filesystem::path{ASSETS_PATH};
}

inline std::filesystem::path Path(std::string_view relativePath) {
  return (Root() / std::filesystem::path{relativePath}).lexically_normal();
}

inline bool Exists(std::string_view relativePath) {
  return std::filesystem::exists(Path(relativePath));
}

inline std::optional<std::string> ReadText(std::string_view relativePath) {
  std::ifstream input(Path(relativePath), std::ios::binary);
  if (!input.is_open()) {
    return std::nullopt;
  }

  return std::string((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
}

inline std::optional<std::vector<unsigned char>> ReadBinary(std::string_view relativePath) {
  std::ifstream input(Path(relativePath), std::ios::binary);
  if (!input.is_open()) {
    return std::nullopt;
  }

  input.seekg(0, std::ios::end);
  const auto size = static_cast<std::size_t>(input.tellg());
  input.seekg(0, std::ios::beg);

  std::vector<unsigned char> bytes(size);
  if (size > 0) {
    input.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(size));
  }

  return bytes;
}

inline std::vector<std::filesystem::path> ListFiles(std::string_view relativeDirectory = "", bool recursive = false) {
  std::vector<std::filesystem::path> files;
  const auto basePath = Path(relativeDirectory);

  if (!std::filesystem::exists(basePath) || !std::filesystem::is_directory(basePath)) {
    return files;
  }

  if (recursive) {
    for (const auto &entry : std::filesystem::recursive_directory_iterator(basePath)) {
      if (entry.is_regular_file()) {
        files.push_back(entry.path());
      }
    }
    return files;
  }

  for (const auto &entry : std::filesystem::directory_iterator(basePath)) {
    if (entry.is_regular_file()) {
      files.push_back(entry.path());
    }
  }

  return files;
}

} // namespace Assets
