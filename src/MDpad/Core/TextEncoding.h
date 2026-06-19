#pragma once

std::string ToUtf8(std::wstring_view text);
std::wstring FromUtf8(std::string_view text);
