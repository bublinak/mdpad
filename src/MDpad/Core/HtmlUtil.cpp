#include "pch.h"
#include "HtmlUtil.h"
#include "TextEncoding.h"

std::string EscapeHtml(std::string_view value)
{
    std::string escaped;
    escaped.reserve(value.size());

    for (char const ch : value)
    {
        switch (ch)
        {
        case '&': escaped += "&amp;"; break;
        case '<': escaped += "&lt;"; break;
        case '>': escaped += "&gt;"; break;
        case '"': escaped += "&quot;"; break;
        case '\'': escaped += "&#39;"; break;
        default: escaped += ch; break;
        }
    }

    return escaped;
}

std::wstring JsonString(std::wstring_view value)
{
    std::wstring escaped;
    escaped.reserve(value.size() + 2);
    escaped.push_back(L'"');

    for (wchar_t const ch : value)
    {
        switch (ch)
        {
        case L'\\': escaped += L"\\\\"; break;
        case L'"': escaped += L"\\\""; break;
        case L'\b': escaped += L"\\b"; break;
        case L'\f': escaped += L"\\f"; break;
        case L'\n': escaped += L"\\n"; break;
        case L'\r': escaped += L"\\r"; break;
        case L'\t': escaped += L"\\t"; break;
        default:
            if (ch < 0x20)
            {
                wchar_t buffer[7]{};
                swprintf_s(buffer, L"\\u%04x", static_cast<unsigned int>(ch));
                escaped += buffer;
            }
            else
            {
                escaped.push_back(ch);
            }
            break;
        }
    }

    escaped.push_back(L'"');
    return escaped;
}

std::wstring JsonStringUtf8(std::string_view value)
{
    return JsonString(FromUtf8(value));
}

bool HasAllowedExternalScheme(std::wstring_view href)
{
    std::wstring lowered(href);
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(towlower(ch));
    });

    return lowered.starts_with(L"https://")
        || lowered.starts_with(L"http://")
        || lowered.starts_with(L"mailto:");
}
