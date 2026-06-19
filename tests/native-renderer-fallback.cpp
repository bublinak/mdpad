#include <iostream>
#include <string>

#include "MarkdownRenderer.h"

namespace
{
    void RequireContains(std::string const& html, std::string_view expected)
    {
        if (html.find(expected) == std::string::npos)
        {
            std::cerr << "Expected output to contain:\n" << expected << "\n\nActual output:\n" << html << '\n';
            std::exit(1);
        }
    }

    void RequireNotContains(std::string const& html, std::string_view unexpected)
    {
        if (html.find(unexpected) != std::string::npos)
        {
            std::cerr << "Expected output not to contain:\n" << unexpected << "\n\nActual output:\n" << html << '\n';
            std::exit(1);
        }
    }
}

int main()
{
    MarkdownRenderer renderer;

    std::string const crlfHtml = renderer.Render(
        L"# Title\r\n"
        L"\r\n"
        L"| Name | Value |\r\n"
        L"| --- | --- |\r\n"
        L"| Alpha | `one` |\r\n"
        L"\r\n"
        L"```js\r\n"
        L"const answer = 42;\r\n"
        L"```\r\n"
        L"\r\n"
        L"## After fence\r\n"
        L"Plain text");

    RequireContains(crlfHtml, "<h1>Title</h1>");
    RequireContains(crlfHtml, "<table>");
    RequireContains(crlfHtml, "<th>Name</th>");
    RequireContains(crlfHtml, "<td><code>one</code></td>");
    RequireContains(crlfHtml, "<pre><code class=\"language-js\">const answer = 42;\n</code></pre>");
    RequireContains(crlfHtml, "<h2>After fence</h2>");
    RequireContains(crlfHtml, "<p>Plain text</p>");
    RequireNotContains(crlfHtml, "<h1>Title | Name | Value |");

    std::string const bareCrHtml = renderer.Render(L"# First\r\r## Second\rBody");
    RequireContains(bareCrHtml, "<h1>First</h1>");
    RequireContains(bareCrHtml, "<h2>Second</h2>");
    RequireContains(bareCrHtml, "<p>Body</p>");

    std::string const frontMatterHtml = renderer.Render(
        L"---\r\n"
        L"title: Dokumentace k projektu Orbis-v1\r\n"
        L"subtitle: Centralni jednotka, senzoricka vetev a webova aplikace TagMan\r\n"
        L"version: Pracovni\r\n"
        L"date: 18. 6. 2026\r\n"
        L"owner: Teatech s.r.o.\r\n"
        L"---\r\n"
        L"\r\n"
        L"# Overview\r\n"
        L"\r\n"
        L"Text before a rule.\r\n"
        L"\r\n"
        L"---\r\n"
        L"\r\n"
        L"Text after a rule.");

    RequireContains(frontMatterHtml, "<details class=\"front-matter\"><summary>Metadata</summary>");
    RequireContains(frontMatterHtml, "<dt>title</dt><dd>Dokumentace k projektu Orbis-v1</dd>");
    RequireContains(frontMatterHtml, "<dt>owner</dt><dd>Teatech s.r.o.</dd>");
    RequireContains(frontMatterHtml, "<h1>Overview</h1>");
    RequireContains(frontMatterHtml, "<hr>");
    RequireContains(frontMatterHtml, "<p>Text after a rule.</p>");
    RequireNotContains(frontMatterHtml, "<p>---</p>");

    std::string const mediaHtml = renderer.Render(
        L"![NPN](NPN.jpeg)\r\n"
        L"\r\n"
        L"<p>\r\n"
        L"  <img src=\"Tranzistor.jpeg\" width=\"300\">\r\n"
        L"</p>\r\n"
        L"\r\n"
        L"<iframe\r\n"
        L"  src=\"https://example.com/embed?ctz=CQAgj+test\"\r\n"
        L"  width=\"100%\"\r\n"
        L"  height=\"380\"\r\n"
        L"  title=\"Falstad test\"\r\n"
        L"  loading=\"lazy\"\r\n"
        L"></iframe>\r\n");

    RequireContains(mediaHtml, "<img alt=\"NPN\" src=\"NPN.jpeg\">");
    RequireContains(mediaHtml, "<img src=\"Tranzistor.jpeg\" width=\"300\">");
    RequireContains(mediaHtml, "<iframe");
    RequireContains(mediaHtml, "src=\"https://example.com/embed?ctz=CQAgj+test\"");
    RequireContains(mediaHtml, "width=\"100%\"");
    RequireContains(mediaHtml, "height=\"380\"");
    RequireContains(mediaHtml, "title=\"Falstad test\"");
    RequireContains(mediaHtml, "loading=\"lazy\"");
    RequireContains(mediaHtml, "</iframe>");
    RequireNotContains(mediaHtml, "&lt;img src=&quot;Tranzistor.jpeg&quot;");
    RequireNotContains(mediaHtml, "&lt;iframe");

    std::cout << "Native fallback renderer tests passed.\n";
    return 0;
}
