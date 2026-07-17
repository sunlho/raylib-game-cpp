#pragma once

#include <array>
#include <deque>
#include <string>
#include <string_view>
#include <vector>

#include "Console.h"

namespace GameConsole {
namespace Internal {

constexpr size_t MAX_INPUT_LENGTH = 256;
constexpr size_t MAX_LOG_LINES = 200;
constexpr size_t MAX_HISTORY = 50;

enum class LineKind {
  Output,
  Error,
  Command,
};

struct ConsoleLine {
  std::string text;
  LineKind kind = LineKind::Output;
};

struct ConsoleState {
  bool open = false;
  bool requestFocus = false;
  bool clearQueuedInput = false;
  bool scrollToBottom = false;
  std::array<char, MAX_INPUT_LENGTH> input{};
  std::string historyDraft;
  int historyIndex = -1;
  std::deque<ConsoleLine> lines;
  std::vector<std::string> history;
  std::vector<CommandDefinition> commands;
};

std::string Trim(std::string value);
std::string NormalizeCommandName(std::string value);
const CommandDefinition *FindCommand(const ConsoleState &state, std::string_view name);
std::vector<const CommandDefinition *> GetSuggestions(const ConsoleState &state);
std::string FormatCommand(const CommandDefinition &command);
void SetInput(ConsoleState &state, std::string_view input);
void PushLine(ConsoleState &state, std::string text, LineKind kind);
void ClearLines(ConsoleState &state);
void Execute(flecs::world &world);
bool IsImGuiInitialized();
void InitializeImGui();
void ShutdownImGui();

} // namespace Internal

} // namespace GameConsole
