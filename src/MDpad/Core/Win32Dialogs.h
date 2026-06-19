#pragma once

std::optional<std::filesystem::path> ShowOpenMarkdownDialog(HWND owner);
std::optional<std::filesystem::path> ShowSaveMarkdownDialog(HWND owner, std::filesystem::path const& currentPath);
std::optional<std::filesystem::path> ShowSaveHtmlDialog(HWND owner, std::filesystem::path const& currentPath);
int ShowDirtyPrompt(HWND owner, std::wstring const& displayName);
void ShowError(HWND owner, std::wstring const& message);
