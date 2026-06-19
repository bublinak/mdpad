#pragma once

std::string EscapeHtml(std::string_view value);
std::wstring JsonString(std::wstring_view value);
std::wstring JsonStringUtf8(std::string_view value);
bool HasAllowedExternalScheme(std::wstring_view href);
