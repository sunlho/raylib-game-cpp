
#include <algorithm>
#include <cctype>
#include <cstring>
#include <utility>

#include "raylib.h"
#include "rlImGui.h"

#include "Console.h"
#include "ConsoleInternal.h"

namespace GameConsole {
namespace Internal {
namespace {

bool imguiInitialized = false;

struct ParsedCommand {
  std::vector<std::string> tokens;
  std::string error;
};

ParsedCommand ParseCommand(std::string input) {
  input = Trim(std::move(input));
  if (!input.empty() && input.front() == '/') {
    input.erase(input.begin());
  }

  ParsedCommand result;
  std::string token;
  bool quoted = false;
  bool escaped = false;

  for (const char character : input) {
    if (escaped) {
      token.push_back(character);
      escaped = false;
      continue;
    }
    if (character == '\\' && quoted) {
      escaped = true;
      continue;
    }
    if (character == '"') {
      quoted = !quoted;
      continue;
    }
    if (std::isspace(static_cast<unsigned char>(character)) != 0 && !quoted) {
      if (!token.empty()) {
        result.tokens.push_back(std::move(token));
        token.clear();
      }
      continue;
    }
    token.push_back(character);
  }

  if (escaped) {
    token.push_back('\\');
  }
  if (quoted) {
    result.error = "Unclosed quote";
    return result;
  }
  if (!token.empty()) {
    result.tokens.push_back(std::move(token));
  }
  return result;
}

} // namespace

std::string Trim(std::string value) {
  const size_t first = value.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) {
    return {};
  }
  const size_t last = value.find_last_not_of(" \t\r\n");
  return value.substr(first, last - first + 1);
}

std::string NormalizeCommandName(std::string value) {
  value = Trim(std::move(value));
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
    return static_cast<char>(std::tolower(character));
  });
  return value;
}

const CommandDefinition *FindCommand(const ConsoleState &state, std::string_view name) {
  const std::string normalizedName = NormalizeCommandName(std::string(name));
  const auto command = std::find_if(state.commands.begin(), state.commands.end(), [&](const CommandDefinition &candidate) {
    return candidate.name == normalizedName;
  });
  return command == state.commands.end() ? nullptr : &*command;
}

std::vector<const CommandDefinition *> GetSuggestions(const ConsoleState &state) {
  std::string query = Trim(state.input.data());
  if (!query.empty() && query.front() == '/') {
    query.erase(query.begin());
  }
  const size_t whitespace = query.find_first_of(" \t");
  const bool hasArguments = whitespace != std::string::npos;
  if (hasArguments) {
    query.resize(whitespace);
  }
  query = NormalizeCommandName(std::move(query));

  std::vector<const CommandDefinition *> suggestions;
  for (const auto &command : state.commands) {
    if ((query.empty() || command.name.starts_with(query)) && (!hasArguments || command.name == query)) {
      suggestions.push_back(&command);
    }
  }
  return suggestions;
}

std::string FormatCommand(const CommandDefinition &command) {
  return command.usage.empty() ? command.name : command.name + " " + command.usage;
}

void SetInput(ConsoleState &state, std::string_view input) {
  const size_t length = std::min(input.size(), state.input.size() - 1);
  std::memcpy(state.input.data(), input.data(), length);
  state.input[length] = '\0';
}

void PushLine(ConsoleState &state, std::string text, LineKind kind) {
  size_t lineStart = 0;
  do {
    const size_t lineEnd = text.find('\n', lineStart);
    state.lines.push_back({text.substr(lineStart, lineEnd - lineStart), kind});
    if (state.lines.size() > MAX_LOG_LINES) {
      state.lines.pop_front();
    }
    if (lineEnd == std::string::npos) {
      break;
    }
    lineStart = lineEnd + 1;
  } while (lineStart <= text.size());
  state.scrollToBottom = true;
}

void ClearLines(ConsoleState &state) {
  state.lines.clear();
  state.scrollToBottom = true;
}

void Execute(flecs::world &world) {
  auto &state = world.get_mut<ConsoleState>();
  std::string submitted = Trim(state.input.data());
  SetInput(state, {});
  state.historyIndex = -1;
  state.historyDraft.clear();

  if (submitted.empty() || submitted == "/") {
    return;
  }

  PushLine(state, "> " + submitted, LineKind::Command);
  if (state.history.empty() || state.history.back() != submitted) {
    state.history.push_back(submitted);
    if (state.history.size() > MAX_HISTORY) {
      state.history.erase(state.history.begin());
    }
  }

  ParsedCommand parsed = ParseCommand(std::move(submitted));
  if (!parsed.error.empty()) {
    PushLine(state, std::move(parsed.error), LineKind::Error);
    return;
  }
  if (parsed.tokens.empty()) {
    return;
  }

  const CommandDefinition *definition = FindCommand(state, parsed.tokens.front());
  if (definition == nullptr) {
    PushLine(state, "Unknown command: " + parsed.tokens.front(), LineKind::Error);
    return;
  }

  CommandHandler handler = definition->handler;
  std::vector<std::string> arguments(parsed.tokens.begin() + 1, parsed.tokens.end());
  CommandResult result = handler ? handler(world, arguments) : CommandResult{false, "Command has no handler"};
  if (!result.message.empty()) {
    PushLine(
        world.get_mut<ConsoleState>(),
        std::move(result.message),
        result.success ? LineKind::Output : LineKind::Error);
  }
}

bool IsImGuiInitialized() {
  return imguiInitialized;
}

void InitializeImGui() {
  if (!imguiInitialized) {
    rlImGuiSetup(true);
    imguiInitialized = true;
  }
}

void ShutdownImGui() {
  if (imguiInitialized) {
    rlImGuiShutdown();
    imguiInitialized = false;
  }
}

} // namespace Internal

bool RegisterCommand(flecs::world &world, CommandDefinition definition) {
  auto &state = world.get_mut<Internal::ConsoleState>();
  definition.name = Internal::NormalizeCommandName(std::move(definition.name));
  if (definition.name.empty() || !definition.handler || Internal::FindCommand(state, definition.name) != nullptr) {
    return false;
  }

  state.commands.push_back(std::move(definition));
  std::ranges::sort(state.commands, {}, &CommandDefinition::name);
  return true;
}

void Print(flecs::world &world, std::string message) {
  Internal::PushLine(world.get_mut<Internal::ConsoleState>(), std::move(message), Internal::LineKind::Output);
}

void PrintError(flecs::world &world, std::string message) {
  Internal::PushLine(world.get_mut<Internal::ConsoleState>(), std::move(message), Internal::LineKind::Error);
}

bool IsOpen(const flecs::world &world) {
  return world.get<Internal::ConsoleState>().open;
}

void SetOpen(flecs::world &world, bool open, std::string initialInput) {
  auto &state = world.get_mut<Internal::ConsoleState>();
  state.open = open;
  state.requestFocus = open;
  state.clearQueuedInput = open;
  state.historyIndex = -1;
  state.historyDraft.clear();
  if (open) {
    Internal::SetInput(state, initialInput);
  }
}

void Update(flecs::world &world) {
  const bool open = IsOpen(world);
  if (!open) {
    if (IsKeyPressed(KEY_T)) {
      SetOpen(world, true);
    } else if (IsKeyPressed(KEY_SLASH)) {
      SetOpen(world, true, "/");
    }
  } else if (IsKeyPressed(KEY_ESCAPE)) {
    SetOpen(world, false);
  }
}

void Shutdown() {
  Internal::ShutdownImGui();
}

module::module(flecs::world &world) {
  world.component<Internal::ConsoleState>()
      .add(flecs::Singleton);
  world.set<Internal::ConsoleState>({});
  Internal::InitializeImGui();
}

} // namespace GameConsole
