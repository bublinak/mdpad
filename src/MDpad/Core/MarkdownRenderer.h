#pragma once

class MarkdownRenderer
{
public:
    std::string Render(std::wstring_view markdown) const;

private:
    std::string RenderFallback(std::string_view markdown) const;
};
