#include "pch.h"
#include "MarkdownRenderer.h"
#include "HtmlUtil.h"
#include "TextEncoding.h"

#include <cctype>

#if !defined(MDPAD_FORCE_FALLBACK_RENDERER) && __has_include(<cmark-gfm.h>) && __has_include(<cmark-gfm-core-extensions.h>)
#define MDPAD_HAS_CMARK_GFM 1
extern "C"
{
#include <cmark-gfm.h>
#include <cmark-gfm-core-extensions.h>
}
#else
#define MDPAD_HAS_CMARK_GFM 0
#endif

namespace
{
    struct FrontMatter
    {
        std::string html;
        std::string body;
    };

    std::string NormalizeLineEndings(std::string_view text)
    {
        std::string normalized;
        normalized.reserve(text.size());

        for (size_t i = 0; i < text.size(); ++i)
        {
            if (text[i] == '\r')
            {
                if (i + 1 < text.size() && text[i + 1] == '\n')
                {
                    ++i;
                }
                normalized.push_back('\n');
            }
            else
            {
                normalized.push_back(text[i]);
            }
        }

        return normalized;
    }

    std::string_view StripUtf8Bom(std::string_view text)
    {
        constexpr std::string_view bom{ "\xEF\xBB\xBF", 3 };
        if (text.size() >= bom.size() && text.substr(0, bom.size()) == bom)
        {
            text.remove_prefix(bom.size());
        }
        return text;
    }

    std::string_view TrimLeft(std::string_view value)
    {
        while (!value.empty() && (value.front() == ' ' || value.front() == '\t'))
        {
            value.remove_prefix(1);
        }
        return value;
    }

    std::string_view TrimRight(std::string_view value)
    {
        while (!value.empty() && (value.back() == ' ' || value.back() == '\t'))
        {
            value.remove_suffix(1);
        }
        return value;
    }

    std::string_view Trim(std::string_view value)
    {
        return TrimRight(TrimLeft(value));
    }

    std::string ToString(std::string_view value)
    {
        return std::string(value.data(), value.size());
    }

    bool StartsWith(std::string_view value, std::string_view prefix)
    {
        return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
    }

    bool IsYamlFrontMatterFence(std::string_view line)
    {
        return Trim(line) == "---";
    }

    std::vector<std::string> SplitLines(std::string_view text)
    {
        std::vector<std::string> lines;
        std::istringstream input{ ToString(text) };
        std::string line;
        while (std::getline(input, line))
        {
            lines.push_back(line);
        }
        if (!text.empty() && text.back() == '\n')
        {
            lines.push_back({});
        }

        return lines;
    }

    std::string RenderFrontMatter(std::string_view metadata)
    {
        auto const lines = SplitLines(metadata);
        std::string output = "<details class=\"front-matter\"><summary>Metadata</summary>\n";

        bool hasItems = false;
        std::string items;
        for (auto const& lineValue : lines)
        {
            std::string_view line = Trim(lineValue);
            if (line.empty())
            {
                continue;
            }

            size_t const separator = line.find(':');
            if (separator == std::string_view::npos)
            {
                continue;
            }

            std::string_view key = Trim(line.substr(0, separator));
            std::string_view value = Trim(line.substr(separator + 1));
            if (key.empty())
            {
                continue;
            }

            hasItems = true;
            items += "<dt>";
            items += EscapeHtml(key);
            items += "</dt><dd>";
            items += EscapeHtml(value);
            items += "</dd>\n";
        }

        if (hasItems)
        {
            output += "<dl>\n";
            output += items;
            output += "</dl>\n";
        }
        else if (!Trim(metadata).empty())
        {
            output += "<pre>";
            output += EscapeHtml(metadata);
            output += "</pre>\n";
        }
        else
        {
            output += "<p>No metadata</p>\n";
        }

        output += "</details>\n";
        return output;
    }

    std::optional<FrontMatter> ExtractFrontMatter(std::string_view markdown)
    {
        markdown = StripUtf8Bom(markdown);

        size_t const firstLineEnd = markdown.find('\n');
        std::string_view firstLine = firstLineEnd == std::string_view::npos
            ? markdown
            : markdown.substr(0, firstLineEnd);
        if (!IsYamlFrontMatterFence(firstLine))
        {
            return std::nullopt;
        }

        size_t const metadataStart = firstLineEnd == std::string_view::npos ? markdown.size() : firstLineEnd + 1;
        size_t lineStart = metadataStart;
        while (lineStart <= markdown.size())
        {
            size_t const lineEnd = markdown.find('\n', lineStart);
            std::string_view line = lineEnd == std::string_view::npos
                ? markdown.substr(lineStart)
                : markdown.substr(lineStart, lineEnd - lineStart);

            if (IsYamlFrontMatterFence(line))
            {
                size_t const bodyStart = lineEnd == std::string_view::npos ? markdown.size() : lineEnd + 1;
                FrontMatter frontMatter;
                frontMatter.html = RenderFrontMatter(markdown.substr(metadataStart, lineStart - metadataStart));
                frontMatter.body = ToString(markdown.substr(bodyStart));
                return frontMatter;
            }

            if (lineEnd == std::string_view::npos)
            {
                break;
            }
            lineStart = lineEnd + 1;
        }

        return std::nullopt;
    }

    size_t FindClosing(std::string_view value, std::string_view marker, size_t offset)
    {
        size_t pos = offset;
        while ((pos = value.find(marker, pos)) != std::string_view::npos)
        {
            if (pos == 0 || value[pos - 1] != '\\')
            {
                return pos;
            }
            pos += marker.size();
        }
        return std::string_view::npos;
    }

    std::string RenderInline(std::string_view text)
    {
        std::string output;
        output.reserve(text.size());

        for (size_t i = 0; i < text.size();)
        {
            if (StartsWith(text.substr(i), "!["))
            {
                size_t const altEnd = FindClosing(text, "]", i + 2);
                if (altEnd != std::string_view::npos && altEnd + 1 < text.size() && text[altEnd + 1] == '(')
                {
                    size_t const srcEnd = FindClosing(text, ")", altEnd + 2);
                    if (srcEnd != std::string_view::npos)
                    {
                        output += "<img alt=\"";
                        output += EscapeHtml(text.substr(i + 2, altEnd - i - 2));
                        output += "\" src=\"";
                        output += EscapeHtml(Trim(text.substr(altEnd + 2, srcEnd - altEnd - 2)));
                        output += "\">";
                        i = srcEnd + 1;
                        continue;
                    }
                }
            }

            if (text[i] == '[')
            {
                size_t const textEnd = FindClosing(text, "]", i + 1);
                if (textEnd != std::string_view::npos && textEnd + 1 < text.size() && text[textEnd + 1] == '(')
                {
                    size_t const hrefEnd = FindClosing(text, ")", textEnd + 2);
                    if (hrefEnd != std::string_view::npos)
                    {
                        output += "<a href=\"";
                        output += EscapeHtml(Trim(text.substr(textEnd + 2, hrefEnd - textEnd - 2)));
                        output += "\">";
                        output += RenderInline(text.substr(i + 1, textEnd - i - 1));
                        output += "</a>";
                        i = hrefEnd + 1;
                        continue;
                    }
                }
            }

            if (text[i] == '`')
            {
                size_t const end = FindClosing(text, "`", i + 1);
                if (end != std::string_view::npos)
                {
                    output += "<code>";
                    output += EscapeHtml(text.substr(i + 1, end - i - 1));
                    output += "</code>";
                    i = end + 1;
                    continue;
                }
            }

            if (StartsWith(text.substr(i), "**"))
            {
                size_t const end = FindClosing(text, "**", i + 2);
                if (end != std::string_view::npos)
                {
                    output += "<strong>";
                    output += RenderInline(text.substr(i + 2, end - i - 2));
                    output += "</strong>";
                    i = end + 2;
                    continue;
                }
            }

            if (text[i] == '*')
            {
                size_t const end = FindClosing(text, "*", i + 1);
                if (end != std::string_view::npos)
                {
                    output += "<em>";
                    output += RenderInline(text.substr(i + 1, end - i - 1));
                    output += "</em>";
                    i = end + 1;
                    continue;
                }
            }

            output += EscapeHtml(text.substr(i, 1));
            ++i;
        }

        return output;
    }

    size_t MarkdownIndent(std::string_view line)
    {
        size_t count = 0;
        while (count < line.size() && line[count] == ' ')
        {
            ++count;
        }
        return count;
    }

    bool IsThematicBreak(std::string_view line)
    {
        size_t const indent = MarkdownIndent(line);
        if (indent > 3)
        {
            return false;
        }

        line.remove_prefix(indent);
        line = Trim(line);

        char marker = '\0';
        size_t count = 0;
        for (char const ch : line)
        {
            if (ch == ' ' || ch == '\t')
            {
                continue;
            }

            if (marker == '\0')
            {
                if (ch != '-' && ch != '*' && ch != '_')
                {
                    return false;
                }
                marker = ch;
            }
            else if (ch != marker)
            {
                return false;
            }

            ++count;
        }

        return count >= 3;
    }


    bool TryHeading(std::string_view line, int& level, std::string_view& text)
    {
        line = TrimRight(line);
        size_t const indent = MarkdownIndent(line);
        if (indent > 3)
        {
            return false;
        }

        line.remove_prefix(indent);
        level = 0;
        while (level < 6 && static_cast<size_t>(level) < line.size() && line[static_cast<size_t>(level)] == '#')
        {
            ++level;
        }

        if (level == 0 || static_cast<size_t>(level) >= line.size() || line[static_cast<size_t>(level)] != ' ')
        {
            return false;
        }

        text = Trim(line.substr(static_cast<size_t>(level) + 1));
        while (!text.empty() && text.back() == '#')
        {
            text.remove_suffix(1);
            text = TrimRight(text);
        }
        return true;
    }

    bool TryFenceStart(std::string_view line, std::string& marker, std::string& info)
    {
        size_t const indent = MarkdownIndent(line);
        if (indent > 3)
        {
            return false;
        }

        line.remove_prefix(indent);
        if (!StartsWith(line, "```") && !StartsWith(line, "~~~"))
        {
            return false;
        }

        char const ch = line[0];
        size_t count = 0;
        while (count < line.size() && line[count] == ch)
        {
            ++count;
        }

        if (count < 3)
        {
            return false;
        }

        marker.assign(count, ch);
        info = ToString(Trim(line.substr(count)));
        return true;
    }

    bool IsFenceClose(std::string_view line, std::string_view marker)
    {
        size_t const indent = MarkdownIndent(line);
        if (indent > 3)
        {
            return false;
        }

        line.remove_prefix(indent);
        if (!StartsWith(line, marker))
        {
            return false;
        }

        for (char const ch : line.substr(marker.size()))
        {
            if (ch != ' ' && ch != '\t')
            {
                return false;
            }
        }
        return true;
    }

    std::vector<std::string> SplitTableRow(std::string_view row)
    {
        row = Trim(row);
        if (!row.empty() && row.front() == '|')
        {
            row.remove_prefix(1);
        }
        if (!row.empty() && row.back() == '|')
        {
            row.remove_suffix(1);
        }

        std::vector<std::string> cells;
        std::string current;
        bool escaped = false;
        for (char const ch : row)
        {
            if (escaped)
            {
                current.push_back(ch);
                escaped = false;
                continue;
            }

            if (ch == '\\')
            {
                escaped = true;
                continue;
            }

            if (ch == '|')
            {
                cells.push_back(ToString(Trim(current)));
                current.clear();
            }
            else
            {
                current.push_back(ch);
            }
        }

        cells.push_back(ToString(Trim(current)));
        return cells;
    }

    bool IsTableDelimiterCell(std::string_view cell)
    {
        cell = Trim(cell);
        if (!cell.empty() && cell.front() == ':')
        {
            cell.remove_prefix(1);
        }
        if (!cell.empty() && cell.back() == ':')
        {
            cell.remove_suffix(1);
        }
        cell = Trim(cell);

        size_t dashes = 0;
        for (char const ch : cell)
        {
            if (ch != '-')
            {
                return false;
            }
            ++dashes;
        }
        return dashes >= 3;
    }

    bool IsTableDelimiter(std::string_view row)
    {
        auto const cells = SplitTableRow(row);
        if (cells.empty())
        {
            return false;
        }

        for (auto const& cell : cells)
        {
            if (!IsTableDelimiterCell(cell))
            {
                return false;
            }
        }
        return true;
    }

    bool LooksLikeTable(std::string_view header, std::string_view delimiter)
    {
        return header.find('|') != std::string_view::npos
            && delimiter.find('|') != std::string_view::npos
            && IsTableDelimiter(delimiter);
    }

    bool LooksLikeHtmlBlock(std::string_view line)
    {
        line = TrimLeft(line);
        if (line.size() < 3 || line.front() != '<')
        {
            return false;
        }

        if (StartsWith(line, "<!--") || StartsWith(line, "<!"))
        {
            return true;
        }

        size_t index = 1;
        if (line[index] == '/')
        {
            ++index;
        }

        if (index >= line.size() || !std::isalpha(static_cast<unsigned char>(line[index])))
        {
            return false;
        }

        while (index < line.size() && (std::isalnum(static_cast<unsigned char>(line[index])) || line[index] == '-'))
        {
            ++index;
        }

        return index == line.size()
            || (index < line.size() && (line[index] == '>' || line[index] == ' ' || line[index] == '\t' || line[index] == '/'));
    }

    bool TryUnorderedListItem(std::string_view line, std::string_view& text)
    {
        line = TrimLeft(line);
        if (line.size() < 2 || (line[0] != '-' && line[0] != '*' && line[0] != '+') || line[1] != ' ')
        {
            return false;
        }

        text = Trim(line.substr(2));
        return true;
    }

    bool TryOrderedListItem(std::string_view line, std::string_view& text)
    {
        line = TrimLeft(line);
        size_t i = 0;
        while (i < line.size() && std::isdigit(static_cast<unsigned char>(line[i])))
        {
            ++i;
        }

        if (i == 0 || i + 1 >= line.size() || line[i] != '.' || line[i + 1] != ' ')
        {
            return false;
        }

        text = Trim(line.substr(i + 2));
        return true;
    }

    std::string RenderListItem(std::string_view text)
    {
        if (StartsWith(text, "[ ] ") || StartsWith(text, "[x] ") || StartsWith(text, "[X] "))
        {
            bool const checked = text[1] == 'x' || text[1] == 'X';
            std::string html = "<input type=\"checkbox\" disabled";
            if (checked)
            {
                html += " checked";
            }
            html += "> ";
            html += RenderInline(Trim(text.substr(4)));
            return html;
        }

        return RenderInline(text);
    }
}

std::string MarkdownRenderer::Render(std::wstring_view markdown) const
{
    std::string const normalized = NormalizeLineEndings(ToUtf8(markdown));
    auto const frontMatter = ExtractFrontMatter(normalized);
    std::string const frontMatterHtml = frontMatter ? frontMatter->html : "";
    std::string const body = frontMatter ? frontMatter->body : ToString(StripUtf8Bom(normalized));

#if MDPAD_HAS_CMARK_GFM
    cmark_gfm_core_extensions_ensure_registered();

    int const options = CMARK_OPT_DEFAULT
        | CMARK_OPT_UNSAFE
        | CMARK_OPT_VALIDATE_UTF8
        | CMARK_OPT_GITHUB_PRE_LANG
        | CMARK_OPT_STRIKETHROUGH_DOUBLE_TILDE;

    cmark_mem* allocator = cmark_get_default_mem_allocator();
    cmark_parser* parser = cmark_parser_new(options);
    cmark_llist* extensions = nullptr;

    for (char const* name : { "table", "strikethrough", "autolink", "tasklist" })
    {
        cmark_syntax_extension* extension = cmark_find_syntax_extension(name);
        if (extension != nullptr)
        {
            cmark_parser_attach_syntax_extension(parser, extension);
            extensions = cmark_llist_append(allocator, extensions, extension);
        }
    }

    cmark_parser_feed(parser, body.data(), body.size());
    cmark_node* document = cmark_parser_finish(parser);
    char* html = cmark_render_html(document, options, extensions);

    std::string result = frontMatterHtml;
    result += html != nullptr ? html : "";

    allocator->free(html);
    cmark_llist_free(allocator, extensions);
    cmark_node_free(document);
    cmark_parser_free(parser);

    return result;
#else
    return frontMatterHtml + RenderFallback(body);
#endif
}

std::string MarkdownRenderer::RenderFallback(std::string_view markdown) const
{
    std::string normalized = NormalizeLineEndings(markdown);
    std::vector<std::string> lines = SplitLines(normalized);

    std::stringstream output;

    for (size_t i = 0; i < lines.size();)
    {
        std::string_view current = lines[i];
        if (Trim(current).empty())
        {
            ++i;
            continue;
        }

        std::string fence;
        std::string info;
        if (TryFenceStart(current, fence, info))
        {
            std::string lang = info;
            if (auto const space = lang.find(' '); space != std::string::npos)
            {
                lang.erase(space);
            }

            output << "<pre><code";
            if (!lang.empty())
            {
                output << " class=\"language-" << EscapeHtml(lang) << "\"";
            }
            output << ">";

            ++i;
            while (i < lines.size() && !IsFenceClose(lines[i], fence))
            {
                output << EscapeHtml(lines[i]) << '\n';
                ++i;
            }
            if (i < lines.size())
            {
                ++i;
            }

            output << "</code></pre>\n";
            continue;
        }

        int headingLevel = 0;
        std::string_view headingText;
        if (TryHeading(current, headingLevel, headingText))
        {
            output << "<h" << headingLevel << ">" << RenderInline(headingText) << "</h" << headingLevel << ">\n";
            ++i;
            continue;
        }

        if (IsThematicBreak(current))
        {
            output << "<hr>\n";
            ++i;
            continue;
        }

        if (LooksLikeHtmlBlock(current))
        {
            while (i < lines.size() && !Trim(lines[i]).empty())
            {
                output << lines[i] << '\n';
                ++i;
            }
            continue;
        }

        if (i + 1 < lines.size() && LooksLikeTable(lines[i], lines[i + 1]))
        {
            auto headers = SplitTableRow(lines[i]);
            output << "<table>\n<thead><tr>";
            for (auto const& header : headers)
            {
                output << "<th>" << RenderInline(header) << "</th>";
            }
            output << "</tr></thead>\n<tbody>\n";

            i += 2;
            while (i < lines.size() && lines[i].find('|') != std::string::npos && !Trim(lines[i]).empty())
            {
                auto cells = SplitTableRow(lines[i]);
                output << "<tr>";
                for (size_t cell = 0; cell < headers.size(); ++cell)
                {
                    output << "<td>";
                    if (cell < cells.size())
                    {
                        output << RenderInline(cells[cell]);
                    }
                    output << "</td>";
                }
                output << "</tr>\n";
                ++i;
            }

            output << "</tbody>\n</table>\n";
            continue;
        }

        std::string_view itemText;
        if (TryUnorderedListItem(current, itemText))
        {
            output << "<ul>\n";
            while (i < lines.size() && TryUnorderedListItem(lines[i], itemText))
            {
                output << "<li>" << RenderListItem(itemText) << "</li>\n";
                ++i;
            }
            output << "</ul>\n";
            continue;
        }

        if (TryOrderedListItem(current, itemText))
        {
            output << "<ol>\n";
            while (i < lines.size() && TryOrderedListItem(lines[i], itemText))
            {
                output << "<li>" << RenderInline(itemText) << "</li>\n";
                ++i;
            }
            output << "</ol>\n";
            continue;
        }

        if (StartsWith(TrimLeft(current), "> "))
        {
            output << "<blockquote>";
            bool first = true;
            while (i < lines.size() && StartsWith(TrimLeft(lines[i]), "> "))
            {
                if (!first)
                {
                    output << "<br>";
                }
                std::string_view quote = TrimLeft(lines[i]);
                output << RenderInline(Trim(quote.substr(2)));
                first = false;
                ++i;
            }
            output << "</blockquote>\n";
            continue;
        }

        std::string paragraph = ToString(Trim(current));
        ++i;
        while (i < lines.size()
            && !Trim(lines[i]).empty()
            && !TryFenceStart(lines[i], fence, info)
            && !TryHeading(lines[i], headingLevel, headingText)
            && !IsThematicBreak(lines[i])
            && !(i + 1 < lines.size() && LooksLikeTable(lines[i], lines[i + 1]))
            && !TryUnorderedListItem(lines[i], itemText)
            && !TryOrderedListItem(lines[i], itemText)
            && !StartsWith(TrimLeft(lines[i]), "> "))
        {
            paragraph += ' ';
            paragraph += ToString(Trim(lines[i]));
            ++i;
        }

        output << "<p>" << RenderInline(paragraph) << "</p>\n";
    }

    return output.str();
}
