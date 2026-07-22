#include <algorithm>
#include <string>

#include "imgui.h"
#include "rlImGui.h"

#include "Console.h"
#include "ConsoleInternal.h"

namespace GameConsole {
namespace Internal {

ImVec4 GetLineColor(LineKind kind) {
  switch (kind) {
  case LineKind::Error:
    return ImVec4(0.94f, 0.43f, 0.43f, 1.0f);
  case LineKind::Command:
    return ImVec4(0.42f, 0.83f, 0.70f, 1.0f);
  case LineKind::Output:
  default:
    return ImVec4(0.86f, 0.88f, 0.90f, 1.0f);
  }
}

void ReplaceInput(ImGuiInputTextCallbackData *data, const std::string &value) {
  data->DeleteChars(0, data->BufTextLen);
  data->InsertChars(0, value.c_str());
  data->CursorPos = data->SelectionStart = data->SelectionEnd = data->BufTextLen;
}

int InputCallback(ImGuiInputTextCallbackData *data) {
  auto &state = *static_cast<ConsoleState *>(data->UserData);

  if (data->EventFlag == ImGuiInputTextFlags_CallbackCompletion) {
    const auto suggestions = GetSuggestions(state);
    if (!suggestions.empty()) {
      const bool slashPrefixed = state.input.front() == '/';
      std::string completed = slashPrefixed ? "/" : "";
      completed += suggestions.front()->name;
      if (!suggestions.front()->usage.empty()) {
        completed.push_back(' ');
      }
      ReplaceInput(data, completed);
      state.historyIndex = -1;
    }
    return 0;
  }

  if (data->EventFlag != ImGuiInputTextFlags_CallbackHistory || state.history.empty()) {
    return 0;
  }

  std::string selected;
  if (data->EventKey == ImGuiKey_UpArrow) {
    if (state.historyIndex < 0) {
      state.historyDraft = data->Buf;
      state.historyIndex = static_cast<int>(state.history.size()) - 1;
    } else if (state.historyIndex > 0) {
      --state.historyIndex;
    }
  } else if (data->EventKey == ImGuiKey_DownArrow && state.historyIndex >= 0) {
    ++state.historyIndex;
    if (state.historyIndex >= static_cast<int>(state.history.size())) {
      state.historyIndex = -1;
    }
  }

  selected =
      state.historyIndex >= 0
          ? state.history[static_cast<size_t>(state.historyIndex)]
          : state.historyDraft;
  ReplaceInput(data, selected);
  return 0;
}

void DrawSuggestions(ConsoleState &state) {
  constexpr int MAX_SUGGESTIONS = 4;
  const auto suggestions = GetSuggestions(state);

  ImGui::BeginChild("Suggestions", ImVec2(0.0f, 76.0f), ImGuiChildFlags_Borders);
  if (suggestions.empty()) {
    ImGui::TextDisabled("No matching commands");
  } else {
    const int count = std::min(MAX_SUGGESTIONS, static_cast<int>(suggestions.size()));
    for (int index = 0; index < count; ++index) {
      const CommandDefinition &command = *suggestions[static_cast<size_t>(index)];
      const std::string label = FormatCommand(command);
      if (ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick)) {
        const bool slashPrefixed = state.input.front() == '/';
        std::string completed = slashPrefixed ? "/" : "";
        completed += command.name;
        if (!command.usage.empty()) {
          completed.push_back(' ');
        }
        SetInput(state, completed);
        state.requestFocus = true;
      }
      if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", command.description.c_str());
      }
    }
  }
  ImGui::EndChild();
}

void DrawWindow(flecs::world &world, ConsoleState &state) {
  const ImGuiIO &io = ImGui::GetIO();
  const float height = std::clamp(io.DisplaySize.y * 0.42f, 260.0f, 440.0f);
  ImGui::SetNextWindowPos(ImVec2(0.0f, io.DisplaySize.y - height), ImGuiCond_Always);
  ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, height), ImGuiCond_Always);
  ImGui::SetNextWindowBgAlpha(0.97f);

  bool visible = state.open;
  constexpr ImGuiWindowFlags flags =
      ImGuiWindowFlags_NoMove |
      ImGuiWindowFlags_NoResize |
      ImGuiWindowFlags_NoCollapse |
      ImGuiWindowFlags_NoSavedSettings;

  if (!ImGui::Begin("Console", &visible, flags)) {
    ImGui::End();
    state.open = visible;
    return;
  }

  if (ImGui::Button("Clear")) {
    ClearLines(state);
  }
  ImGui::SameLine();
  ImGui::TextDisabled("%zu commands", state.commands.size());
  ImGui::Separator();

  const float reservedHeight = 76.0f + ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y * 3.0f;
  ImGui::BeginChild(
      "Log",
      ImVec2(0.0f, std::max(80.0f, ImGui::GetContentRegionAvail().y - reservedHeight)),
      ImGuiChildFlags_Borders,
      ImGuiWindowFlags_HorizontalScrollbar);
  for (const auto &line : state.lines) {
    ImGui::PushStyleColor(ImGuiCol_Text, GetLineColor(line.kind));
    ImGui::TextUnformatted(line.text.c_str());
    ImGui::PopStyleColor();
  }
  if (state.scrollToBottom) {
    ImGui::SetScrollHereY(1.0f);
    state.scrollToBottom = false;
  }
  ImGui::EndChild();

  DrawSuggestions(state);

  if (state.requestFocus) {
    ImGui::SetKeyboardFocusHere();
    state.requestFocus = false;
  }

  constexpr ImGuiInputTextFlags inputFlags =
      ImGuiInputTextFlags_EnterReturnsTrue |
      ImGuiInputTextFlags_CallbackCompletion |
      ImGuiInputTextFlags_CallbackHistory;
  ImGui::SetNextItemWidth(-1.0f);
  if (ImGui::InputTextWithHint(
          "##CommandInput",
          "Enter command",
          state.input.data(),
          state.input.size(),
          inputFlags,
          InputCallback,
          &state)) {
    Execute(world);
    state.requestFocus = true;
  }

  ImGui::End();
  state.open = visible;
}

} // namespace Internal

void Draw(flecs::world &world) {
  if (!Internal::IsImGuiInitialized()) {
    return;
  }

  auto &state = world.get_mut<Internal::ConsoleState>();
  rlImGuiBegin();
  if (state.clearQueuedInput) {
    ImGui::GetIO().ClearInputKeys();
    state.clearQueuedInput = false;
  }
  if (state.open) {
    Internal::DrawWindow(world, state);
  }
  rlImGuiEnd();
}

} // namespace GameConsole
