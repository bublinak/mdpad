#include "pch.h"
#include "MainWindow.xaml.h"
#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif

#include "Core/HtmlUtil.h"
#include "Core/TextEncoding.h"
#include "Core/Win32Dialogs.h"

#include <microsoft.ui.xaml.window.h>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Controls::Primitives;
using namespace Microsoft::UI::Xaml::Input;
using namespace Microsoft::UI::Xaml::Media;
using namespace Microsoft::UI::Composition;
using namespace Microsoft::UI::Composition::SystemBackdrops;
using namespace Microsoft::UI::Windowing;
using namespace Microsoft::Web::WebView2::Core;
using namespace Windows::ApplicationModel;
using namespace Windows::Foundation;
using namespace Windows::System;

namespace
{
    std::vector<winrt::MDpad::MainWindow> g_secondaryWindows;

    void UntrackSecondaryWindow(void* windowAbi)
    {
        auto const match = std::remove_if(g_secondaryWindows.begin(), g_secondaryWindows.end(), [windowAbi](winrt::MDpad::MainWindow const& window) {
            return get_abi(window) == windowAbi;
        });
        g_secondaryWindows.erase(match, g_secondaryWindows.end());
    }

    void TrackSecondaryWindow(winrt::MDpad::MainWindow const& window)
    {
        void* const windowAbi = get_abi(window);
        g_secondaryWindows.push_back(window);
        window.Closed([windowAbi](IInspectable const&, WindowEventArgs const&) {
            UntrackSecondaryWindow(windowAbi);
        });
    }

    std::wstring ErrorText(char const* prefix, std::exception const& error)
    {
        std::wstring message = winrt::to_hstring(prefix).c_str();
        message += L"\n\n";
        message += winrt::to_hstring(error.what()).c_str();
        return message;
    }

    std::wstring InstalledRendererPath()
    {
        UINT32 length = 0;
        if (GetCurrentPackageFullName(&length, nullptr) != APPMODEL_ERROR_NO_PACKAGE)
        {
            auto installedPath = Package::Current().InstalledLocation().Path();
            return std::filesystem::path(installedPath.c_str()).append(L"Assets").append(L"renderer").wstring();
        }

        std::wstring modulePath(MAX_PATH, L'\0');
        DWORD copied = GetModuleFileNameW(nullptr, modulePath.data(), static_cast<DWORD>(modulePath.size()));
        while (copied == modulePath.size())
        {
            modulePath.resize(modulePath.size() * 2);
            copied = GetModuleFileNameW(nullptr, modulePath.data(), static_cast<DWORD>(modulePath.size()));
        }

        modulePath.resize(copied);
        return std::filesystem::path(modulePath).parent_path().append(L"Assets").append(L"renderer").wstring();
    }

    std::wstring ToVirtualFolderUri(std::filesystem::path const& path)
    {
        if (path.empty())
        {
            return L"";
        }

        return L"https://doc.mdpad.local/";
    }

    wchar_t const* ThemeName(AppTheme theme)
    {
        switch (theme)
        {
        case AppTheme::Light:
            return L"light";
        case AppTheme::Dark:
            return L"dark";
        default:
            return L"system";
        }
    }

    Windows::UI::Color Rgb(uint8_t red, uint8_t green, uint8_t blue)
    {
        return Windows::UI::Color{ 255, red, green, blue };
    }

    std::wstring ToLower(std::wstring_view value)
    {
        std::wstring lowered(value);
        std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](wchar_t ch) {
            return static_cast<wchar_t>(towlower(ch));
        });
        return lowered;
    }

    bool StartsWithInsensitive(std::wstring_view value, std::wstring_view prefix)
    {
        if (value.size() < prefix.size())
        {
            return false;
        }

        for (size_t i = 0; i < prefix.size(); ++i)
        {
            if (towlower(value[i]) != towlower(prefix[i]))
            {
                return false;
            }
        }
        return true;
    }

    bool StartsWith(std::wstring_view value, std::wstring_view prefix)
    {
        return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
    }

    bool EndsWith(std::wstring_view value, std::wstring_view suffix)
    {
        return value.size() >= suffix.size() && value.substr(value.size() - suffix.size()) == suffix;
    }

    bool IsIsolatedSingleAsterisk(std::wstring const& text, size_t markerStart)
    {
        return (markerStart == 0 || text[markerStart - 1] != L'*') &&
            (markerStart + 1 >= text.size() || text[markerStart + 1] != L'*');
    }

    bool IsExactInlineMarker(std::wstring const& text, size_t markerStart, std::wstring_view marker)
    {
        if (markerStart > text.size() || markerStart + marker.size() > text.size())
        {
            return false;
        }

        if (std::wstring_view(text).substr(markerStart, marker.size()) != marker)
        {
            return false;
        }

        if (marker == L"*")
        {
            return IsIsolatedSingleAsterisk(text, markerStart);
        }

        return true;
    }

    bool HasOrderedListPrefix(std::wstring_view line, size_t& prefixLength)
    {
        size_t offset = 0;
        while (offset < line.size() && line[offset] >= L'0' && line[offset] <= L'9')
        {
            ++offset;
        }

        if (offset == 0 || offset + 1 >= line.size() || line[offset] != L'.' || line[offset + 1] != L' ')
        {
            prefixLength = 0;
            return false;
        }

        prefixLength = offset + 2;
        return true;
    }

    bool IsDocumentVirtualUri(std::wstring_view href)
    {
        return StartsWithInsensitive(href, L"https://doc.mdpad.local/")
            || StartsWithInsensitive(href, L"https://doc.mdpad.local?");
    }

    std::wstring StripQueryAndFragment(std::wstring_view value)
    {
        size_t const end = value.find_first_of(L"?#");
        return std::wstring(value.substr(0, end));
    }

    int HexValue(wchar_t ch)
    {
        if (ch >= L'0' && ch <= L'9')
        {
            return ch - L'0';
        }
        if (ch >= L'a' && ch <= L'f')
        {
            return 10 + ch - L'a';
        }
        if (ch >= L'A' && ch <= L'F')
        {
            return 10 + ch - L'A';
        }
        return -1;
    }

    std::wstring UrlDecode(std::wstring_view value)
    {
        std::string bytes;
        bytes.reserve(value.size());

        for (size_t i = 0; i < value.size(); ++i)
        {
            wchar_t const ch = value[i];
            if (ch == L'%' && i + 2 < value.size())
            {
                int const high = HexValue(value[i + 1]);
                int const low = HexValue(value[i + 2]);
                if (high >= 0 && low >= 0)
                {
                    bytes.push_back(static_cast<char>((high << 4) | low));
                    i += 2;
                    continue;
                }
            }

            bytes += ToUtf8(std::wstring(1, ch));
        }

        return FromUtf8(bytes);
    }

    bool IsMarkdownPath(std::filesystem::path const& path)
    {
        std::wstring const extension = ToLower(path.extension().wstring());
        return extension == L".md" || extension == L".markdown";
    }

    std::wstring BuildPreviewJson(std::string_view html, std::wstring_view baseUri, double zoom, AppTheme theme)
    {
        std::wstringstream payload;
        payload << L"{"
            << L"\"kind\":\"render\","
            << L"\"html\":" << JsonStringUtf8(html) << L","
            << L"\"baseUri\":" << JsonString(baseUri) << L","
            << L"\"zoom\":" << zoom << L","
            << L"\"theme\":" << JsonString(ThemeName(theme))
            << L"}";
        return payload.str();
    }

    std::wstring DecodeJsonString(std::wstring_view value)
    {
        if (value.size() < 2 || value.front() != L'"' || value.back() != L'"')
        {
            return std::wstring(value);
        }

        std::wstring decoded;
        decoded.reserve(value.size());
        for (size_t i = 1; i + 1 < value.size(); ++i)
        {
            wchar_t const ch = value[i];
            if (ch != L'\\' || i + 1 >= value.size())
            {
                decoded.push_back(ch);
                continue;
            }

            wchar_t const escaped = value[++i];
            switch (escaped)
            {
            case L'"': decoded.push_back(L'"'); break;
            case L'\\': decoded.push_back(L'\\'); break;
            case L'/': decoded.push_back(L'/'); break;
            case L'b': decoded.push_back(L'\b'); break;
            case L'f': decoded.push_back(L'\f'); break;
            case L'n': decoded.push_back(L'\n'); break;
            case L'r': decoded.push_back(L'\r'); break;
            case L't': decoded.push_back(L'\t'); break;
            case L'u':
            {
                if (i + 4 >= value.size())
                {
                    decoded.push_back(L'?');
                    break;
                }

                int code = 0;
                bool valid = true;
                for (size_t digit = 0; digit < 4; ++digit)
                {
                    int const hex = HexValue(value[i + 1 + digit]);
                    if (hex < 0)
                    {
                        valid = false;
                        break;
                    }
                    code = (code << 4) | hex;
                }

                if (valid)
                {
                    decoded.push_back(static_cast<wchar_t>(code));
                    i += 4;
                }
                break;
            }
            default:
                decoded.push_back(escaped);
                break;
            }
        }
        return decoded;
    }

    std::wstring StripMessagePrefix(std::wstring const& value, std::wstring const& prefix)
    {
        if (value.rfind(prefix, 0) != 0)
        {
            return {};
        }

        return value.substr(prefix.size());
    }
}

namespace winrt::MDpad::implementation
{
    MainWindow::MainWindow()
    {
        SetEnvironmentVariableW(L"WEBVIEW2_DEFAULT_BACKGROUND_COLOR", L"00000000");
        InitializeComponent();
        RegisterKeyboardAccelerators();
        AppWindow().Closing({ this, &MainWindow::OnAppWindowClosing });
        Closed({ this, &MainWindow::OnClosed });
        LoadSettings();
        ApplyWindowSize();
        ApplyAppTheme();
        ApplyAcrylicEffect();

        m_viewMode = m_settings.openFormattedByDefault ? ViewMode::Formatted : ViewMode::Syntax;
        m_suppressTextChanged = true;
        SourceTextBox().Text(m_document.Text());
        m_suppressTextChanged = false;

        ApplyViewMode();
        ApplyZoom();
        ApplyTitle();
        ApplyNavigationState();

        InitializePreviewAsync();
    }

    fire_and_forget MainWindow::InitializePreviewAsync()
    {
        auto lifetime = get_strong();
        ApplyPreviewBackgroundColor();
        co_await PreviewWebView().EnsureCoreWebView2Async();
        ApplyPreviewBackgroundColor();

        auto core = PreviewWebView().CoreWebView2();
        core.Settings().AreDefaultContextMenusEnabled(true);
        core.Settings().AreDevToolsEnabled(true);
        core.SetVirtualHostNameToFolderMapping(
            L"app.mdpad.local",
            InstalledRendererPath(),
            CoreWebView2HostResourceAccessKind::DenyCors);

        core.WebMessageReceived({ this, &MainWindow::OnPreviewMessage });
        core.NavigationCompleted({ this, &MainWindow::OnPreviewNavigationCompleted });
        PreviewWebView().Source(Uri(L"https://app.mdpad.local/index.html"));
    }

    void MainWindow::RegisterKeyboardAccelerators()
    {
        RootGrid().AddHandler(
            UIElement::PreviewKeyDownEvent(),
            box_value(KeyEventHandler([this](IInspectable const&, KeyRoutedEventArgs const& args) {
                bool const control = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
                bool const shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                bool const alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
                if (HandleNativeShortcut(args.Key(), control, shift, alt))
                {
                    args.Handled(true);
                }
            })),
            true);
    }

    bool MainWindow::HandleNativeShortcut(VirtualKey key, bool control, bool shift, bool alt)
    {
        auto invoke = [this](void (MainWindow::*handler)(IInspectable const&, RoutedEventArgs const&)) {
            IInspectable sender{ nullptr };
            RoutedEventArgs args{ nullptr };
            (this->*handler)(sender, args);
        };

        if (alt && !control && !shift)
        {
            if (key == VirtualKey::Left)
            {
                NavigateHistory(-1);
                return true;
            }
            if (key == VirtualKey::Right)
            {
                NavigateHistory(1);
                return true;
            }
            return false;
        }

        if (key == VirtualKey::Delete && !control && !shift && !alt)
        {
            if (m_viewMode != ViewMode::Syntax)
            {
                return false;
            }

            SourceTextBox().SelectedText(L"");
            return true;
        }

        if (!control || alt)
        {
            return false;
        }

        if (key == VirtualKey::Add || key == static_cast<VirtualKey>(VK_OEM_PLUS))
        {
            invoke(&MainWindow::ZoomIn_Click);
            return true;
        }
        if (key == VirtualKey::Subtract || key == static_cast<VirtualKey>(VK_OEM_MINUS))
        {
            invoke(&MainWindow::ZoomOut_Click);
            return true;
        }

        switch (key)
        {
        case VirtualKey::N:
            if (!shift)
            {
                New_Click(nullptr, nullptr);
                return true;
            }
            break;
        case VirtualKey::O:
            if (!shift)
            {
                Open_Click(nullptr, nullptr);
                return true;
            }
            break;
        case VirtualKey::S:
            return shift ? SaveAs() : Save();
        case VirtualKey::F:
            if (!shift)
            {
                ShowFindPanel(false);
                return true;
            }
            break;
        case VirtualKey::H:
            if (!shift && m_viewMode == ViewMode::Syntax)
            {
                ShowFindPanel(true);
                return true;
            }
            break;
        case VirtualKey::Z:
            if (!shift && m_viewMode == ViewMode::Syntax)
            {
                SourceTextBox().Undo();
                return true;
            }
            break;
        case VirtualKey::Y:
            if (!shift && m_viewMode == ViewMode::Syntax)
            {
                SourceTextBox().Redo();
                return true;
            }
            break;
        case VirtualKey::X:
            if (shift)
            {
                return ApplyInlineMarkdown(L"~~", L"~~", L"deleted text");
            }
            if (m_viewMode == ViewMode::Syntax)
            {
                SourceTextBox().CutSelectionToClipboard();
                return true;
            }
            break;
        case VirtualKey::C:
            if (shift)
            {
                return ApplyInlineMarkdown(L"`", L"`", L"code");
            }
            invoke(&MainWindow::Copy_Click);
            return true;
        case VirtualKey::V:
            if (!shift && m_viewMode == ViewMode::Syntax)
            {
                SourceTextBox().PasteFromClipboard();
                return true;
            }
            break;
        case VirtualKey::B:
            if (!shift)
            {
                return ApplyInlineMarkdown(L"**", L"**", L"bold text");
            }
            break;
        case VirtualKey::I:
            if (!shift)
            {
                return ApplyInlineMarkdown(L"*", L"*", L"italic text");
            }
            break;
        case VirtualKey::K:
            if (!shift)
            {
                return ApplyMarkdownLink();
            }
            break;
        case VirtualKey::A:
            if (!shift)
            {
                invoke(&MainWindow::SelectAll_Click);
                return true;
            }
            break;
        case VirtualKey::Number0:
            if (!shift)
            {
                invoke(&MainWindow::ResetZoom_Click);
                return true;
            }
            break;
        default:
            break;
        }

        return false;
    }

    bool MainWindow::HandlePreviewShortcut(std::wstring const& command)
    {
        if (command == L"new")
        {
            New_Click(nullptr, nullptr);
            return true;
        }
        if (command == L"open")
        {
            Open_Click(nullptr, nullptr);
            return true;
        }
        if (command == L"save")
        {
            return Save();
        }
        if (command == L"save-as")
        {
            return SaveAs();
        }
        if (command == L"find")
        {
            ShowFindPanel(false);
            return true;
        }
        if (command == L"select-all")
        {
            SelectAll_Click(nullptr, nullptr);
            return true;
        }
        if (command == L"zoom-in")
        {
            ZoomIn_Click(nullptr, nullptr);
            return true;
        }
        if (command == L"zoom-out")
        {
            ZoomOut_Click(nullptr, nullptr);
            return true;
        }
        if (command == L"reset-zoom")
        {
            ResetZoom_Click(nullptr, nullptr);
            return true;
        }

        return false;
    }

    void MainWindow::RequestClose()
    {
        RequestCloseAsync();
    }

    fire_and_forget MainWindow::RequestCloseAsync()
    {
        auto lifetime = get_strong();

        if (!co_await ConfirmDiscardIfDirtyAsync())
        {
            co_return;
        }

        m_closeConfirmed = true;
        Close();
    }

    IAsyncOperation<bool> MainWindow::ConfirmDiscardIfDirtyAsync()
    {
        if (!m_document.IsDirty())
        {
            co_return true;
        }

        if (m_dirtyPromptActive)
        {
            co_return false;
        }

        m_dirtyPromptActive = true;
        try
        {
            ContentDialog dialog;
            dialog.XamlRoot(RootGrid().XamlRoot());
            dialog.Title(box_value(L"Save changes?"));
            std::wstring message = L"Do you want to save changes to ";
            message += m_document.DisplayName();
            message += L"?";
            dialog.Content(box_value(message));
            dialog.PrimaryButtonText(L"Save");
            dialog.SecondaryButtonText(L"Don't Save");
            dialog.CloseButtonText(L"Cancel");
            dialog.DefaultButton(ContentDialogButton::Primary);

            ContentDialogResult const result = co_await dialog.ShowAsync();
            m_dirtyPromptActive = false;

            if (result == ContentDialogResult::Primary)
            {
                co_return Save();
            }

            co_return result == ContentDialogResult::Secondary;
        }
        catch (std::exception const& error)
        {
            m_dirtyPromptActive = false;
            ShowError(WindowHandle(), ErrorText("Could not show the save prompt.", error));
            co_return false;
        }
    }

    fire_and_forget MainWindow::SaveGeneratedHtmlAsync()
    {
        auto lifetime = get_strong();

        try
        {
            if (!m_previewReady || !PreviewWebView().CoreWebView2())
            {
                ShowError(WindowHandle(), L"The preview renderer is still loading. Try again in a moment.");
                co_return;
            }

            RenderPreview();
            std::filesystem::path const suggested = SuggestedHtmlPath();
            auto path = ShowSaveHtmlDialog(WindowHandle(), suggested);
            if (!path)
            {
                co_return;
            }

            hstring const script = LR"(
                new Promise((resolve) => {
                    requestAnimationFrame(() => {
                        setTimeout(() => resolve(window.__mdpadRenderer.exportHtml()), 80);
                    });
                });
            )";
            hstring const encoded = co_await PreviewWebView().CoreWebView2().ExecuteScriptAsync(script);
            std::wstring const html = DecodeJsonString(encoded.c_str());
            std::ofstream stream(*path, std::ios::binary | std::ios::trunc);
            if (!stream)
            {
                throw std::runtime_error("Unable to open file for writing.");
            }

            std::string const bytes = ToUtf8(html);
            stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
            StatusTextBlock().Text(L"Saved HTML preview: " + path->wstring());
        }
        catch (std::exception const& error)
        {
            ShowError(WindowHandle(), ErrorText("Could not save the generated HTML.", error));
        }
    }

    fire_and_forget MainWindow::ShowSettingsAsync()
    {
        auto lifetime = get_strong();

        ContentDialog dialog;
        dialog.XamlRoot(RootGrid().XamlRoot());
        dialog.Title(box_value(L"Settings"));
        dialog.PrimaryButtonText(L"Done");
        dialog.DefaultButton(ContentDialogButton::Primary);

        StackPanel content;
        content.Spacing(14);
        content.Width(360);

        TextBlock acrylicLabel;
        acrylicLabel.Text(L"Source editor acrylic transparency: " + to_hstring(m_settings.acrylicOpacityPercent) + L"%");

        Slider acrylicSlider;
        acrylicSlider.Minimum(0);
        acrylicSlider.Maximum(100);
        acrylicSlider.StepFrequency(1);
        acrylicSlider.Value(static_cast<double>(m_settings.acrylicOpacityPercent));
        acrylicSlider.ValueChanged([this, acrylicLabel](IInspectable const&, RangeBaseValueChangedEventArgs const& args) {
            m_settings.acrylicOpacityPercent = std::clamp(static_cast<int>(std::round(args.NewValue())), 0, 100);
            acrylicLabel.Text(L"Source editor acrylic transparency: " + to_hstring(m_settings.acrylicOpacityPercent) + L"%");
            ApplyAcrylicEffect();
            SaveSettings();
        });

        TextBlock themeLabel;
        themeLabel.Text(L"App theme");

        ComboBox themeBox;
        themeBox.Width(220);
        for (wchar_t const* label : { L"Use system", L"Light", L"Dark" })
        {
            ComboBoxItem item;
            item.Content(box_value(label));
            themeBox.Items().Append(item);
        }
        themeBox.SelectedIndex(static_cast<int>(m_settings.appTheme));
        themeBox.SelectionChanged([this](IInspectable const& sender, SelectionChangedEventArgs const&) {
            int const selected = sender.as<ComboBox>().SelectedIndex();
            if (selected >= static_cast<int>(AppTheme::System) && selected <= static_cast<int>(AppTheme::Dark))
            {
                m_settings.appTheme = static_cast<AppTheme>(selected);
                ApplyAppTheme();
                SaveSettings();
                PostPreviewPayload();
            }
        });

        TextBlock defaultModeLabel;
        defaultModeLabel.Text(L"Default app behavior");

        ComboBox defaultModeBox;
        defaultModeBox.Width(220);
        for (wchar_t const* label : { L"Start formatted", L"Start in syntax" })
        {
            ComboBoxItem item;
            item.Content(box_value(label));
            defaultModeBox.Items().Append(item);
        }
        defaultModeBox.SelectedIndex(m_settings.openFormattedByDefault ? 0 : 1);
        defaultModeBox.SelectionChanged([this](IInspectable const& sender, SelectionChangedEventArgs const&) {
            m_settings.openFormattedByDefault = sender.as<ComboBox>().SelectedIndex() == 0;
            OpenFormattedByDefaultItem().IsChecked(m_settings.openFormattedByDefault);
            SaveSettings();
        });

        TextBlock markdownLinkLabel;
        markdownLinkLabel.Text(L"Markdown file links");

        ComboBox markdownLinkBox;
        markdownLinkBox.Width(220);
        for (wchar_t const* label : { L"Open in new window", L"Open in current window" })
        {
            ComboBoxItem item;
            item.Content(box_value(label));
            markdownLinkBox.Items().Append(item);
        }
        markdownLinkBox.SelectedIndex(static_cast<int>(m_settings.markdownFileLinkOpenMode));
        markdownLinkBox.SelectionChanged([this](IInspectable const& sender, SelectionChangedEventArgs const&) {
            int const selected = sender.as<ComboBox>().SelectedIndex();
            if (selected >= static_cast<int>(MarkdownFileLinkOpenMode::NewWindow) && selected <= static_cast<int>(MarkdownFileLinkOpenMode::CurrentWindow))
            {
                m_settings.markdownFileLinkOpenMode = static_cast<MarkdownFileLinkOpenMode>(selected);
                SaveSettings();
            }
        });

        StackPanel links;
        links.Orientation(Orientation::Horizontal);
        links.Spacing(12);
        links.Margin(ThicknessHelper::FromLengths(0, 8, 0, 0));

        HyperlinkButton githubLink;
        githubLink.Content(box_value(L"GitHub"));
        githubLink.Click([this](IInspectable const&, RoutedEventArgs const&) {
            OpenExternalLink(L"https://github.com/bublinak/mdpad");
        });

        HyperlinkButton licenseLink;
        licenseLink.Content(box_value(L"MIT License"));
        licenseLink.Click([this](IInspectable const&, RoutedEventArgs const&) {
            OpenExternalLink(L"https://github.com/bublinak/mdpad/blob/main/LICENSE");
        });

        links.Children().Append(githubLink);
        links.Children().Append(licenseLink);

        content.Children().Append(acrylicLabel);
        content.Children().Append(acrylicSlider);
        content.Children().Append(themeLabel);
        content.Children().Append(themeBox);
        content.Children().Append(defaultModeLabel);
        content.Children().Append(defaultModeBox);
        content.Children().Append(markdownLinkLabel);
        content.Children().Append(markdownLinkBox);
        content.Children().Append(links);

        dialog.Content(content);
        co_await dialog.ShowAsync();
    }

    void MainWindow::OpenPath(std::wstring const& path)
    {
        OpenDocumentPath(path, true);
    }

    bool MainWindow::OpenDocumentPath(std::filesystem::path const& path, bool addToHistory)
    {
        try
        {
            m_document.LoadFromFile(path);
            m_suppressTextChanged = true;
            SourceTextBox().Text(m_document.Text());
            m_suppressTextChanged = false;
            ApplyTitle();
            RenderPreview();
            if (addToHistory)
            {
                AddHistoryEntry(m_document.Path());
            }
            else
            {
                ApplyNavigationState();
            }
            return true;
        }
        catch (std::exception const& error)
        {
            ShowError(WindowHandle(), ErrorText("Could not open the selected file.", error));
            return false;
        }
    }

    void MainWindow::New_Click(IInspectable const&, RoutedEventArgs const&)
    {
        NewWithPromptAsync();
    }

    fire_and_forget MainWindow::NewWithPromptAsync()
    {
        auto lifetime = get_strong();

        if (!co_await ConfirmDiscardIfDirtyAsync())
        {
            co_return;
        }

        m_document.New();
        m_suppressTextChanged = true;
        SourceTextBox().Text(L"");
        m_suppressTextChanged = false;
        m_fileHistory.clear();
        m_historyIndex = -1;
        ApplyTitle();
        ApplyNavigationState();
        RenderPreview();
    }

    void MainWindow::Open_Click(IInspectable const&, RoutedEventArgs const&)
    {
        OpenWithPromptAsync();
    }

    fire_and_forget MainWindow::OpenWithPromptAsync()
    {
        auto lifetime = get_strong();

        if (!co_await ConfirmDiscardIfDirtyAsync())
        {
            co_return;
        }

        try
        {
            auto path = ShowOpenMarkdownDialog(WindowHandle());
            if (path)
            {
                OpenPath(path->wstring());
            }
        }
        catch (std::exception const& error)
        {
            ShowError(WindowHandle(), ErrorText("Could not open the selected file.", error));
        }
    }

    void MainWindow::Save_Click(IInspectable const&, RoutedEventArgs const&)
    {
        Save();
    }

    void MainWindow::SaveAs_Click(IInspectable const&, RoutedEventArgs const&)
    {
        SaveAs();
    }

    void MainWindow::SaveHtml_Click(IInspectable const&, RoutedEventArgs const&)
    {
        SaveGeneratedHtmlAsync();
    }

    void MainWindow::Exit_Click(IInspectable const&, RoutedEventArgs const&)
    {
        RequestClose();
    }

    void MainWindow::Settings_Click(IInspectable const&, RoutedEventArgs const&)
    {
        ShowSettingsAsync();
    }

    void MainWindow::Back_Click(IInspectable const&, RoutedEventArgs const&)
    {
        NavigateHistory(-1);
    }

    void MainWindow::Forward_Click(IInspectable const&, RoutedEventArgs const&)
    {
        NavigateHistory(1);
    }

    void MainWindow::ToggleMode_Click(IInspectable const&, RoutedEventArgs const&)
    {
        m_viewMode = m_viewMode == ViewMode::Formatted ? ViewMode::Syntax : ViewMode::Formatted;
        ApplyViewMode();
    }

    void MainWindow::OpenFormattedByDefault_Click(IInspectable const&, RoutedEventArgs const&)
    {
        m_settings.openFormattedByDefault = OpenFormattedByDefaultItem().IsChecked();
        SaveSettings();
    }

    void MainWindow::WordWrap_Click(IInspectable const&, RoutedEventArgs const&)
    {
        m_settings.wordWrap = WordWrapItem().IsChecked();
        SourceTextBox().TextWrapping(m_settings.wordWrap ? TextWrapping::Wrap : TextWrapping::NoWrap);
        SaveSettings();
    }

    void MainWindow::ZoomIn_Click(IInspectable const&, RoutedEventArgs const&)
    {
        m_settings.zoom = std::min(2.0, m_settings.zoom + 0.1);
        ApplyZoom();
        SaveSettings();
        PostPreviewPayload();
    }

    void MainWindow::ZoomOut_Click(IInspectable const&, RoutedEventArgs const&)
    {
        m_settings.zoom = std::max(0.5, m_settings.zoom - 0.1);
        ApplyZoom();
        SaveSettings();
        PostPreviewPayload();
    }

    void MainWindow::ResetZoom_Click(IInspectable const&, RoutedEventArgs const&)
    {
        m_settings.zoom = 1.0;
        ApplyZoom();
        SaveSettings();
        PostPreviewPayload();
    }

    void MainWindow::Undo_Click(IInspectable const&, RoutedEventArgs const&)
    {
        SourceTextBox().Undo();
    }

    void MainWindow::Redo_Click(IInspectable const&, RoutedEventArgs const&)
    {
        SourceTextBox().Redo();
    }

    void MainWindow::Cut_Click(IInspectable const&, RoutedEventArgs const&)
    {
        SourceTextBox().CutSelectionToClipboard();
    }

    void MainWindow::Copy_Click(IInspectable const&, RoutedEventArgs const&)
    {
        if (m_viewMode == ViewMode::Syntax)
        {
            SourceTextBox().CopySelectionToClipboard();
        }
        else if (m_previewReady)
        {
            PreviewWebView().CoreWebView2().ExecuteScriptAsync(L"document.execCommand('copy');");
        }
    }

    void MainWindow::Paste_Click(IInspectable const&, RoutedEventArgs const&)
    {
        SourceTextBox().PasteFromClipboard();
    }

    void MainWindow::Delete_Click(IInspectable const&, RoutedEventArgs const&)
    {
        SourceTextBox().SelectedText(L"");
    }

    void MainWindow::Find_Click(IInspectable const&, RoutedEventArgs const&)
    {
        ShowFindPanel(false);
    }

    void MainWindow::Replace_Click(IInspectable const&, RoutedEventArgs const&)
    {
        ShowFindPanel(true);
    }

    void MainWindow::FindNext_Click(IInspectable const&, RoutedEventArgs const&)
    {
        FindNext();
    }

    void MainWindow::ReplaceNext_Click(IInspectable const&, RoutedEventArgs const&)
    {
        if (m_viewMode != ViewMode::Syntax)
        {
            return;
        }

        if (SourceTextBox().SelectedText() == FindTextBox().Text())
        {
            SourceTextBox().SelectedText(ReplaceTextBox().Text());
        }
        FindNext();
    }

    void MainWindow::CloseFind_Click(IInspectable const&, RoutedEventArgs const&)
    {
        FindPanel().Visibility(Visibility::Collapsed);
    }

    void MainWindow::SelectAll_Click(IInspectable const&, RoutedEventArgs const&)
    {
        if (m_viewMode == ViewMode::Syntax)
        {
            SourceTextBox().SelectAll();
        }
        else if (m_previewReady)
        {
            PreviewWebView().CoreWebView2().ExecuteScriptAsync(L"window.getSelection().selectAllChildren(document.querySelector('#content'));");
        }
    }

    void MainWindow::Bold_Click(IInspectable const&, RoutedEventArgs const&)
    {
        ApplyInlineMarkdown(L"**", L"**", L"bold text");
    }

    void MainWindow::Italic_Click(IInspectable const&, RoutedEventArgs const&)
    {
        ApplyInlineMarkdown(L"*", L"*", L"italic text");
    }

    void MainWindow::InlineCode_Click(IInspectable const&, RoutedEventArgs const&)
    {
        ApplyInlineMarkdown(L"`", L"`", L"code");
    }

    void MainWindow::Strikethrough_Click(IInspectable const&, RoutedEventArgs const&)
    {
        ApplyInlineMarkdown(L"~~", L"~~", L"deleted text");
    }

    void MainWindow::Link_Click(IInspectable const&, RoutedEventArgs const&)
    {
        ApplyMarkdownLink();
    }

    void MainWindow::Quote_Click(IInspectable const&, RoutedEventArgs const&)
    {
        ApplyLinePrefix(L"> ");
    }

    void MainWindow::BulletList_Click(IInspectable const&, RoutedEventArgs const&)
    {
        ApplyLinePrefix(L"- ");
    }

    void MainWindow::NumberedList_Click(IInspectable const&, RoutedEventArgs const&)
    {
        ApplyOrderedList();
    }

    bool MainWindow::ApplyInlineMarkdown(std::wstring_view prefix, std::wstring_view suffix, std::wstring_view placeholder)
    {
        if (m_viewMode != ViewMode::Syntax)
        {
            return false;
        }

        std::wstring text = SourceTextBox().Text().c_str();
        int32_t const start = SourceTextBox().SelectionStart();
        int32_t const length = SourceTextBox().SelectionLength();
        size_t const rangeStart = static_cast<size_t>(std::clamp(start, 0, static_cast<int32_t>(text.size())));
        size_t const rangeLength = static_cast<size_t>(std::clamp(length, 0, static_cast<int32_t>(text.size() - rangeStart)));
        std::wstring selected = SourceTextBox().SelectedText().c_str();
        bool const hadSelection = length > 0;

        auto replaceRange = [this](size_t replaceStart, size_t replaceLength, std::wstring const& replacement, size_t selectStart, size_t selectLength) {
            SourceTextBox().Select(static_cast<int32_t>(replaceStart), static_cast<int32_t>(replaceLength));
            SourceTextBox().SelectedText(replacement);
            SourceTextBox().Select(static_cast<int32_t>(selectStart), static_cast<int32_t>(selectLength));
            SourceTextBox().Focus(FocusState::Programmatic);
        };

        if (hadSelection && selected.size() >= prefix.size() + suffix.size() &&
            StartsWith(selected, prefix) && EndsWith(selected, suffix) &&
            IsExactInlineMarker(selected, 0, prefix) &&
            IsExactInlineMarker(selected, selected.size() - suffix.size(), suffix))
        {
            std::wstring inner = selected.substr(prefix.size(), selected.size() - prefix.size() - suffix.size());
            replaceRange(rangeStart, rangeLength, inner, rangeStart, inner.size());
            return true;
        }

        if (rangeStart >= prefix.size() && rangeStart + rangeLength + suffix.size() <= text.size() &&
            IsExactInlineMarker(text, rangeStart - prefix.size(), prefix) &&
            IsExactInlineMarker(text, rangeStart + rangeLength, suffix))
        {
            replaceRange(rangeStart - prefix.size(), rangeLength + prefix.size() + suffix.size(), selected, rangeStart - prefix.size(), selected.size());
            return true;
        }

        if (!hadSelection)
        {
            size_t const markerSearchStart = std::min(rangeStart, text.size());
            size_t const open = text.rfind(prefix, markerSearchStart);
            size_t const close = text.find(suffix, markerSearchStart);
            if (open != std::wstring::npos && close != std::wstring::npos &&
                open + prefix.size() <= rangeStart && close >= rangeStart &&
                close >= open + prefix.size() &&
                IsExactInlineMarker(text, open, prefix) &&
                IsExactInlineMarker(text, close, suffix))
            {
                std::wstring inner = text.substr(open + prefix.size(), close - open - prefix.size());
                size_t const newCaret = rangeStart >= open + prefix.size()
                    ? std::max(open, rangeStart - prefix.size())
                    : open;
                replaceRange(open, inner.size() + prefix.size() + suffix.size(), inner, newCaret, 0);
                return true;
            }
        }

        if (!hadSelection)
        {
            selected = placeholder;
        }

        std::wstring replacement(prefix);
        replacement += selected;
        replacement += suffix;
        SourceTextBox().SelectedText(replacement);
        if (hadSelection)
        {
            SourceTextBox().Select(start, static_cast<int32_t>(replacement.size()));
        }
        else
        {
            SourceTextBox().Select(start + static_cast<int32_t>(prefix.size()), static_cast<int32_t>(selected.size()));
        }
        SourceTextBox().Focus(FocusState::Programmatic);
        return true;
    }

    bool MainWindow::ApplyMarkdownLink()
    {
        if (m_viewMode != ViewMode::Syntax)
        {
            return false;
        }

        std::wstring text = SourceTextBox().Text().c_str();
        int32_t const start = SourceTextBox().SelectionStart();
        int32_t const length = SourceTextBox().SelectionLength();
        size_t const rangeStart = static_cast<size_t>(std::clamp(start, 0, static_cast<int32_t>(text.size())));
        size_t const rangeLength = static_cast<size_t>(std::clamp(length, 0, static_cast<int32_t>(text.size() - rangeStart)));
        std::wstring selected = SourceTextBox().SelectedText().c_str();

        auto replaceRange = [this](size_t replaceStart, size_t replaceLength, std::wstring const& replacement, size_t selectStart, size_t selectLength) {
            SourceTextBox().Select(static_cast<int32_t>(replaceStart), static_cast<int32_t>(replaceLength));
            SourceTextBox().SelectedText(replacement);
            SourceTextBox().Select(static_cast<int32_t>(selectStart), static_cast<int32_t>(selectLength));
            SourceTextBox().Focus(FocusState::Programmatic);
        };

        if (rangeLength > 0 && StartsWith(selected, L"[") && EndsWith(selected, L")"))
        {
            size_t const closeBracket = selected.find(L"](");
            if (closeBracket != std::wstring::npos && closeBracket > 0)
            {
                std::wstring inner = selected.substr(1, closeBracket - 1);
                replaceRange(rangeStart, rangeLength, inner, rangeStart, inner.size());
                return true;
            }
        }

        if (rangeStart > 0 && text[rangeStart - 1] == L'[')
        {
            size_t const suffixStart = rangeStart + rangeLength;
            if (suffixStart + 1 < text.size() && text[suffixStart] == L']' && text[suffixStart + 1] == L'(')
            {
                size_t const suffixEnd = text.find(L')', suffixStart + 2);
                if (suffixEnd != std::wstring::npos)
                {
                    replaceRange(rangeStart - 1, suffixEnd - rangeStart + 2, selected, rangeStart - 1, selected.size());
                    return true;
                }
            }
        }

        if (rangeLength == 0)
        {
            size_t const open = text.rfind(L'[', rangeStart);
            size_t const closeBracket = text.find(L"](", rangeStart);
            if (open != std::wstring::npos && closeBracket != std::wstring::npos && open < rangeStart && rangeStart <= closeBracket)
            {
                size_t const suffixEnd = text.find(L')', closeBracket + 2);
                if (suffixEnd != std::wstring::npos)
                {
                    std::wstring inner = text.substr(open + 1, closeBracket - open - 1);
                    size_t const newCaret = rangeStart > open ? rangeStart - 1 : open;
                    replaceRange(open, suffixEnd - open + 1, inner, newCaret, 0);
                    return true;
                }
            }
        }

        if (selected.empty())
        {
            selected = L"link text";
        }

        std::wstring replacement = L"[";
        replacement += selected;
        replacement += L"](url)";
        SourceTextBox().SelectedText(replacement);
        if (length > 0)
        {
            SourceTextBox().Select(start + static_cast<int32_t>(selected.size()) + 3, 3);
        }
        else
        {
            SourceTextBox().Select(start + 1, static_cast<int32_t>(selected.size()));
        }
        SourceTextBox().Focus(FocusState::Programmatic);
        return true;
    }

    bool MainWindow::ApplyLinePrefix(std::wstring_view prefix)
    {
        if (m_viewMode != ViewMode::Syntax)
        {
            return false;
        }

        std::wstring text = SourceTextBox().Text().c_str();
        int32_t const selectionStart = SourceTextBox().SelectionStart();
        int32_t const selectionLength = SourceTextBox().SelectionLength();
        size_t const start = static_cast<size_t>(std::clamp(selectionStart, 0, static_cast<int32_t>(text.size())));
        size_t const selectionEnd = static_cast<size_t>(std::clamp(selectionStart + selectionLength, 0, static_cast<int32_t>(text.size())));
        size_t lineStart = text.rfind(L'\n', start == 0 ? 0 : start - 1);
        lineStart = lineStart == std::wstring::npos ? 0 : lineStart + 1;
        size_t lineEnd = selectionEnd;
        while (lineEnd < text.size() && text[lineEnd] != L'\n')
        {
            ++lineEnd;
        }

        std::wstring block = text.substr(lineStart, lineEnd - lineStart);
        std::vector<std::wstring> lines;
        bool hasContentLine = false;
        bool allContentLinesPrefixed = true;
        size_t inspectOffset = 0;
        while (inspectOffset <= block.size())
        {
            size_t next = block.find(L'\n', inspectOffset);
            std::wstring line = block.substr(inspectOffset, next == std::wstring::npos ? std::wstring::npos : next - inspectOffset);
            if (!line.empty() && line.back() == L'\r')
            {
                line.pop_back();
            }

            if (!line.empty())
            {
                hasContentLine = true;
                if (!StartsWith(line, prefix))
                {
                    allContentLinesPrefixed = false;
                }
            }

            lines.push_back(std::move(line));
            if (next == std::wstring::npos)
            {
                break;
            }
            inspectOffset = next + 1;
        }

        bool const removePrefix = hasContentLine && allContentLinesPrefixed;
        std::wstring replacement;
        for (size_t i = 0; i < lines.size(); ++i)
        {
            std::wstring const& line = lines[i];
            if (removePrefix)
            {
                if (StartsWith(line, prefix))
                {
                    replacement += line.substr(prefix.size());
                }
                else
                {
                    replacement += line;
                }
            }
            else
            {
                replacement += prefix;
                replacement += line;
            }

            if (i + 1 < lines.size())
            {
                replacement += L"\r\n";
            }
        }

        SourceTextBox().Select(static_cast<int32_t>(lineStart), static_cast<int32_t>(lineEnd - lineStart));
        SourceTextBox().SelectedText(replacement);
        SourceTextBox().Select(static_cast<int32_t>(lineStart), static_cast<int32_t>(replacement.size()));
        SourceTextBox().Focus(FocusState::Programmatic);
        return true;
    }

    bool MainWindow::ApplyOrderedList()
    {
        if (m_viewMode != ViewMode::Syntax)
        {
            return false;
        }

        std::wstring text = SourceTextBox().Text().c_str();
        int32_t const selectionStart = SourceTextBox().SelectionStart();
        int32_t const selectionLength = SourceTextBox().SelectionLength();
        size_t const start = static_cast<size_t>(std::clamp(selectionStart, 0, static_cast<int32_t>(text.size())));
        size_t const selectionEnd = static_cast<size_t>(std::clamp(selectionStart + selectionLength, 0, static_cast<int32_t>(text.size())));
        size_t lineStart = text.rfind(L'\n', start == 0 ? 0 : start - 1);
        lineStart = lineStart == std::wstring::npos ? 0 : lineStart + 1;
        size_t lineEnd = selectionEnd;
        while (lineEnd < text.size() && text[lineEnd] != L'\n')
        {
            ++lineEnd;
        }

        std::wstring block = text.substr(lineStart, lineEnd - lineStart);
        std::vector<std::wstring> lines;
        bool hasContentLine = false;
        bool allContentLinesOrdered = true;
        size_t inspectOffset = 0;
        while (inspectOffset <= block.size())
        {
            size_t next = block.find(L'\n', inspectOffset);
            std::wstring line = block.substr(inspectOffset, next == std::wstring::npos ? std::wstring::npos : next - inspectOffset);
            if (!line.empty() && line.back() == L'\r')
            {
                line.pop_back();
            }

            if (!line.empty())
            {
                size_t prefixLength = 0;
                hasContentLine = true;
                if (!HasOrderedListPrefix(line, prefixLength))
                {
                    allContentLinesOrdered = false;
                }
            }

            lines.push_back(std::move(line));
            if (next == std::wstring::npos)
            {
                break;
            }
            inspectOffset = next + 1;
        }

        bool const removePrefix = hasContentLine && allContentLinesOrdered;
        std::wstring replacement;
        int index = 1;
        for (size_t i = 0; i < lines.size(); ++i)
        {
            std::wstring const& line = lines[i];
            if (removePrefix)
            {
                size_t prefixLength = 0;
                if (HasOrderedListPrefix(line, prefixLength))
                {
                    replacement += line.substr(prefixLength);
                }
                else
                {
                    replacement += line;
                }
            }
            else
            {
                replacement += std::to_wstring(index++);
                replacement += L". ";
                replacement += line;
            }

            if (i + 1 < lines.size())
            {
                replacement += L"\r\n";
            }
        }

        SourceTextBox().Select(static_cast<int32_t>(lineStart), static_cast<int32_t>(lineEnd - lineStart));
        SourceTextBox().SelectedText(replacement);
        SourceTextBox().Select(static_cast<int32_t>(lineStart), static_cast<int32_t>(replacement.size()));
        SourceTextBox().Focus(FocusState::Programmatic);
        return true;
    }

    void MainWindow::SourceTextBox_TextChanged(IInspectable const&, TextChangedEventArgs const&)
    {
        if (m_suppressTextChanged)
        {
            return;
        }

        m_document.SetText(SourceTextBox().Text().c_str());
        ApplyTitle();
        if (m_viewMode == ViewMode::Formatted)
        {
            RenderPreview();
        }
    }

    void MainWindow::OnClosed(IInspectable const&, WindowEventArgs const&)
    {
        StopAcrylicBackdrop();
        SaveWindowSize();
    }

    void MainWindow::OnAppWindowClosing(winrt::Microsoft::UI::Windowing::AppWindow const&, winrt::Microsoft::UI::Windowing::AppWindowClosingEventArgs const& args)
    {
        if (m_closeConfirmed || !m_document.IsDirty())
        {
            SaveWindowSize();
            return;
        }

        args.Cancel(true);
        RequestCloseAsync();
    }

    void MainWindow::LoadSettings()
    {
        m_settings = m_settingsStore.Load();
        OpenFormattedByDefaultItem().IsChecked(m_settings.openFormattedByDefault);
        WordWrapItem().IsChecked(m_settings.wordWrap);
        SourceTextBox().TextWrapping(m_settings.wordWrap ? TextWrapping::Wrap : TextWrapping::NoWrap);
    }

    void MainWindow::SaveSettings()
    {
        m_settingsStore.Save(m_settings);
    }

    void MainWindow::ApplyWindowSize()
    {
        HWND const hwnd = WindowHandle();
        if (!hwnd)
        {
            return;
        }

        SetWindowPos(
            hwnd,
            nullptr,
            0,
            0,
            m_settings.windowWidth,
            m_settings.windowHeight,
            SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }

    void MainWindow::SaveWindowSize()
    {
        HWND const hwnd = WindowHandle();
        if (!hwnd || IsIconic(hwnd))
        {
            return;
        }

        RECT rect{};
        if (!GetWindowRect(hwnd, &rect))
        {
            return;
        }

        int const width = rect.right - rect.left;
        int const height = rect.bottom - rect.top;
        if (width <= 0 || height <= 0)
        {
            return;
        }

        m_settings.windowWidth = std::clamp(width, 420, 3840);
        m_settings.windowHeight = std::clamp(height, 320, 2160);
        SaveSettings();
    }

    void MainWindow::ApplyAppTheme()
    {
        ElementTheme theme = ElementTheme::Default;
        if (m_settings.appTheme == AppTheme::Light)
        {
            theme = ElementTheme::Light;
        }
        else if (m_settings.appTheme == AppTheme::Dark)
        {
            theme = ElementTheme::Dark;
        }

        RootGrid().RequestedTheme(theme);
        ApplyAcrylicEffect();
    }

    void MainWindow::ApplyAcrylicEffect()
    {
        int const opacity = std::clamp(m_settings.acrylicOpacityPercent, 0, 100);
        bool const dark = m_settings.appTheme == AppTheme::Dark ||
            (m_settings.appTheme == AppTheme::System && RootGrid().ActualTheme() == ElementTheme::Dark);
        auto const fallback = dark ? Rgb(32, 32, 32) : Rgb(250, 250, 250);

        ApplyPreviewBackgroundColor();

        if (m_viewMode == ViewMode::Formatted)
        {
            StopAcrylicBackdrop();
            DocumentSurfaceGrid().Background(SolidColorBrush(fallback));
            DocumentAcrylicLayer().Background(nullptr);
            DocumentAcrylicLayer().Opacity(0.0);
            return;
        }

        DocumentSurfaceGrid().Background(SolidColorBrush(Windows::UI::Color{ 0, 0, 0, 0 }));

        bool const enabled = opacity > 0 && DesktopAcrylicController::IsSupported();
        if (!enabled)
        {
            StopAcrylicBackdrop();
            DocumentAcrylicLayer().Background(SolidColorBrush(fallback));
            DocumentAcrylicLayer().Opacity(1.0);
            return;
        }

        if (!m_acrylicController)
        {
            auto target = this->try_as<ICompositionSupportsSystemBackdrop>();
            if (!target)
            {
                DocumentAcrylicLayer().Background(SolidColorBrush(fallback));
                DocumentAcrylicLayer().Opacity(1.0);
                return;
            }

            m_acrylicConfiguration = SystemBackdropConfiguration();
            m_acrylicConfiguration.IsInputActive(true);
            m_acrylicController = DesktopAcrylicController();
            m_acrylicController.Kind(DesktopAcrylicKind::Base);
            m_acrylicController.AddSystemBackdropTarget(target);
            m_acrylicController.SetSystemBackdropConfiguration(m_acrylicConfiguration);
            m_acrylicBackdropEnabled = true;
        }

        DocumentAcrylicLayer().Background(nullptr);
        DocumentAcrylicLayer().Opacity(0.0);
        m_acrylicConfiguration.Theme(dark ? SystemBackdropTheme::Dark : SystemBackdropTheme::Light);

        float const strength = 1.0f - (static_cast<float>(opacity) / 100.0f);
        m_acrylicController.TintColor(dark ? Rgb(24, 24, 24) : Rgb(245, 245, 245));
        m_acrylicController.FallbackColor(fallback);
        m_acrylicController.TintOpacity(strength);
        m_acrylicController.LuminosityOpacity(std::clamp(strength * 0.90f, 0.0f, 0.90f));
    }

    void MainWindow::ApplyPreviewBackgroundColor()
    {
        bool const dark = m_settings.appTheme == AppTheme::Dark ||
            (m_settings.appTheme == AppTheme::System && RootGrid().ActualTheme() == ElementTheme::Dark);
        Windows::UI::Color const color = dark ? Rgb(32, 32, 32) : Rgb(250, 250, 250);
        PreviewWebView().DefaultBackgroundColor(color);
    }

    void MainWindow::StopAcrylicBackdrop()
    {
        if (m_acrylicController)
        {
            m_acrylicController.Close();
            m_acrylicController = nullptr;
        }

        m_acrylicConfiguration = nullptr;
        m_acrylicBackdropEnabled = false;
    }

    void MainWindow::ApplyNavigationState()
    {
        BackButton().IsEnabled(m_historyIndex > 0);
        ForwardButton().IsEnabled(m_historyIndex >= 0 && static_cast<size_t>(m_historyIndex + 1) < m_fileHistory.size());
    }

    void MainWindow::ApplyViewMode()
    {
        bool const formatted = m_viewMode == ViewMode::Formatted;
        PreviewWebView().Visibility(formatted ? Visibility::Visible : Visibility::Collapsed);
        SourceTextBox().Visibility(formatted ? Visibility::Collapsed : Visibility::Visible);
        ApplyAcrylicEffect();
        ModeToggleButton().Content(box_value(hstring(formatted ? L"Formatted" : L"Syntax")));
        ViewModeItem().Text(formatted ? L"Switch to syntax mode" : L"Switch to formatted mode");
        ApplyEditCommandState();

        if (formatted)
        {
            RenderPreview();
        }
        else
        {
            SourceTextBox().Focus(FocusState::Programmatic);
        }
    }

    void MainWindow::ApplyEditCommandState()
    {
        bool const editing = m_viewMode == ViewMode::Syntax;
        UndoItem().IsEnabled(editing);
        RedoItem().IsEnabled(editing);
        CutItem().IsEnabled(editing);
        PasteItem().IsEnabled(editing);
        DeleteItem().IsEnabled(editing);
        BoldItem().IsEnabled(editing);
        ItalicItem().IsEnabled(editing);
        InlineCodeItem().IsEnabled(editing);
        StrikethroughItem().IsEnabled(editing);
        LinkItem().IsEnabled(editing);
        QuoteItem().IsEnabled(editing);
        BulletListItem().IsEnabled(editing);
        NumberedListItem().IsEnabled(editing);
        ReplaceItem().IsEnabled(editing);
        ReplaceButton().IsEnabled(editing);
    }

    void MainWindow::ApplyTitle()
    {
        std::wstring title = m_document.DisplayName();
        if (m_document.IsDirty())
        {
            title += L"*";
        }
        title += L" - MDpad";
        Title(title);
        StatusTextBlock().Text(m_document.HasPath() ? hstring(m_document.Path().wstring()) : hstring(L"Untitled"));
    }

    void MainWindow::ApplyZoom()
    {
        SourceTextBox().FontSize(14.0 * m_settings.zoom);
        int const percent = static_cast<int>(std::round(m_settings.zoom * 100.0));
        ZoomTextBlock().Text(to_hstring(percent) + L"%");
    }

    void MainWindow::RenderPreview()
    {
        std::string const html = m_renderer.Render(m_document.Text());
        std::wstring const baseUri = ToVirtualFolderUri(m_document.Directory());
        m_pendingPreviewJson = BuildPreviewJson(html, baseUri, m_settings.zoom, m_settings.appTheme);

        if (m_previewReady && PreviewWebView().CoreWebView2())
        {
            if (EnsureDocumentResourceMapping())
            {
                return;
            }
            PostPreviewPayload();
        }
    }

    bool MainWindow::EnsureDocumentResourceMapping()
    {
        if (!m_document.HasPath())
        {
            m_mappedDocumentDirectory.clear();
            return false;
        }

        std::filesystem::path directory = m_document.Directory().lexically_normal();
        if (directory == m_mappedDocumentDirectory)
        {
            return false;
        }

        PreviewWebView().CoreWebView2().SetVirtualHostNameToFolderMapping(
            L"doc.mdpad.local",
            directory.wstring(),
            CoreWebView2HostResourceAccessKind::DenyCors);

        m_mappedDocumentDirectory = std::move(directory);
        m_previewReady = false;
        PreviewWebView().CoreWebView2().Reload();
        return true;
    }

    void MainWindow::PostPreviewPayload()
    {
        if (!m_previewReady || m_pendingPreviewJson.empty())
        {
            return;
        }

        PreviewWebView().CoreWebView2().PostWebMessageAsJson(m_pendingPreviewJson);
    }

    void MainWindow::AddHistoryEntry(std::filesystem::path const& path)
    {
        std::filesystem::path normalized = std::filesystem::absolute(path).lexically_normal();
        if (m_historyIndex >= 0 && static_cast<size_t>(m_historyIndex) < m_fileHistory.size() && m_fileHistory[m_historyIndex] == normalized)
        {
            ApplyNavigationState();
            return;
        }

        if (m_historyIndex >= 0 && static_cast<size_t>(m_historyIndex + 1) < m_fileHistory.size())
        {
            m_fileHistory.erase(m_fileHistory.begin() + m_historyIndex + 1, m_fileHistory.end());
        }

        m_fileHistory.push_back(std::move(normalized));
        m_historyIndex = static_cast<int>(m_fileHistory.size()) - 1;
        ApplyNavigationState();
    }

    void MainWindow::NavigateHistory(int offset)
    {
        NavigateHistoryAsync(offset);
    }

    fire_and_forget MainWindow::NavigateHistoryAsync(int offset)
    {
        int const targetIndex = m_historyIndex + offset;
        if (targetIndex < 0 || static_cast<size_t>(targetIndex) >= m_fileHistory.size())
        {
            co_return;
        }

        auto lifetime = get_strong();

        if (!co_await ConfirmDiscardIfDirtyAsync())
        {
            co_return;
        }

        std::filesystem::path const target = m_fileHistory[targetIndex];
        if (OpenDocumentPath(target, false))
        {
            m_historyIndex = targetIndex;
            ApplyNavigationState();
        }
    }

    bool MainWindow::Save()
    {
        try
        {
            if (!m_document.HasPath())
            {
                return SaveAs();
            }

            m_document.Save();
            ApplyTitle();
            return true;
        }
        catch (std::exception const& error)
        {
            ShowError(WindowHandle(), ErrorText("Could not save the file.", error));
            return false;
        }
    }

    bool MainWindow::SaveAs()
    {
        try
        {
            auto path = ShowSaveMarkdownDialog(WindowHandle(), m_document.Path());
            if (!path)
            {
                return false;
            }

            m_document.SaveAs(*path);
            ApplyTitle();
            AddHistoryEntry(m_document.Path());
            return true;
        }
        catch (std::exception const& error)
        {
            ShowError(WindowHandle(), ErrorText("Could not save the file.", error));
            return false;
        }
    }

    HWND MainWindow::WindowHandle()
    {
        HWND hwnd{};
        auto native = this->try_as<IWindowNative>();
        if (native)
        {
            native->get_WindowHandle(&hwnd);
        }
        return hwnd;
    }

    std::filesystem::path MainWindow::SuggestedHtmlPath() const
    {
        if (m_document.HasPath())
        {
            std::filesystem::path path = m_document.Path();
            path.replace_extension(L".html");
            return path;
        }

        return std::filesystem::path(L"Untitled.html");
    }

    void MainWindow::OnPreviewMessage(CoreWebView2 const&, CoreWebView2WebMessageReceivedEventArgs const& args)
    {
        std::wstring const message = args.TryGetWebMessageAsString().c_str();
        if (auto href = StripMessagePrefix(message, L"open-link:"); !href.empty())
        {
            OpenPreviewLink(href);
        }
        else if (auto command = StripMessagePrefix(message, L"shortcut:"); !command.empty())
        {
            HandlePreviewShortcut(command);
        }
    }

    void MainWindow::OnPreviewNavigationCompleted(CoreWebView2 const&, CoreWebView2NavigationCompletedEventArgs const&)
    {
        ApplyPreviewBackgroundColor();
        m_previewReady = true;
        RenderPreview();
    }

    void MainWindow::OpenPreviewLink(std::wstring const& href)
    {
        if (IsDocumentVirtualUri(href))
        {
            auto path = ResolveDocumentFileLink(href);
            if (!path)
            {
                StatusTextBlock().Text(L"Could not resolve the local file link.");
                return;
            }

            OpenLocalFileLink(*path);
            return;
        }

        if (auto path = ResolveFileUri(href))
        {
            OpenLocalFileLink(*path);
            return;
        }

        OpenExternalLink(href);
    }

    std::optional<std::filesystem::path> MainWindow::ResolveDocumentFileLink(std::wstring const& href) const
    {
        if (!m_document.HasPath())
        {
            return std::nullopt;
        }

        std::wstring_view value(href);
        constexpr std::wstring_view prefix = L"https://doc.mdpad.local/";
        if (!StartsWithInsensitive(value, prefix))
        {
            return std::nullopt;
        }

        std::wstring pathPart = StripQueryAndFragment(value.substr(prefix.size()));
        while (!pathPart.empty() && (pathPart.front() == L'/' || pathPart.front() == L'\\'))
        {
            pathPart.erase(pathPart.begin());
        }

        if (pathPart.empty())
        {
            return std::nullopt;
        }

        std::wstring decoded = UrlDecode(pathPart);
        std::replace(decoded.begin(), decoded.end(), L'/', std::filesystem::path::preferred_separator);
        return (m_document.Directory() / decoded).lexically_normal();
    }

    std::optional<std::filesystem::path> MainWindow::ResolveFileUri(std::wstring const& href) const
    {
        std::wstring_view value(href);
        std::wstring pathPart;

        if (StartsWithInsensitive(value, L"file:///"))
        {
            pathPart = StripQueryAndFragment(value.substr(8));
        }
        else if (StartsWithInsensitive(value, L"file://localhost/"))
        {
            pathPart = StripQueryAndFragment(value.substr(17));
        }
        else if (StartsWithInsensitive(value, L"file://"))
        {
            pathPart = L"\\\\" + StripQueryAndFragment(value.substr(7));
        }
        else
        {
            return std::nullopt;
        }

        if (pathPart.empty())
        {
            return std::nullopt;
        }

        std::wstring decoded = UrlDecode(pathPart);
        std::replace(decoded.begin(), decoded.end(), L'/', std::filesystem::path::preferred_separator);
        return std::filesystem::path(decoded).lexically_normal();
    }

    void MainWindow::OpenLocalFileLink(std::filesystem::path const& path)
    {
        std::filesystem::path normalized = std::filesystem::absolute(path).lexically_normal();
        std::error_code existsError;
        if (!std::filesystem::exists(normalized, existsError))
        {
            StatusTextBlock().Text(L"File link not found: " + normalized.wstring());
            return;
        }

        if (IsMarkdownPath(normalized))
        {
            if (m_settings.markdownFileLinkOpenMode == MarkdownFileLinkOpenMode::CurrentWindow)
            {
                OpenMarkdownFileInCurrentWindowAsync(normalized);
            }
            else
            {
                OpenMarkdownFileInNewWindow(normalized);
            }
            return;
        }

        std::wstring const target = normalized.wstring();
        std::wstring const directory = normalized.parent_path().wstring();
        ShellExecuteW(WindowHandle(), L"open", target.c_str(), nullptr, directory.c_str(), SW_SHOWNORMAL);
    }

    fire_and_forget MainWindow::OpenMarkdownFileInCurrentWindowAsync(std::filesystem::path path)
    {
        auto lifetime = get_strong();

        if (!co_await ConfirmDiscardIfDirtyAsync())
        {
            co_return;
        }

        OpenDocumentPath(path, true);
    }

    void MainWindow::OpenMarkdownFileInNewWindow(std::filesystem::path const& path)
    {
        winrt::MDpad::MainWindow window = make<MainWindow>();
        TrackSecondaryWindow(window);
        window.Activate();
        auto windowImpl = get_self<MainWindow>(window);
        windowImpl->OpenPath(path.wstring());
    }

    void MainWindow::OpenExternalLink(std::wstring const& href)
    {
        if (!HasAllowedExternalScheme(href))
        {
            StatusTextBlock().Text(L"Blocked unsupported link scheme");
            return;
        }

        ShellExecuteW(WindowHandle(), L"open", href.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }

    void MainWindow::ShowFindPanel(bool replaceMode)
    {
        FindPanel().Visibility(Visibility::Visible);
        ReplaceTextBox().Visibility(replaceMode ? Visibility::Visible : Visibility::Collapsed);
        ReplaceButton().Visibility(replaceMode ? Visibility::Visible : Visibility::Collapsed);
        FindTextBox().Focus(FocusState::Programmatic);
    }

    bool MainWindow::FindNext()
    {
        std::wstring needle = FindTextBox().Text().c_str();
        if (needle.empty())
        {
            return false;
        }

        if (m_viewMode != ViewMode::Syntax)
        {
            std::wstring script = L"window.mdpadFind && window.mdpadFind(" + JsonString(needle) + L");";
            PreviewWebView().CoreWebView2().ExecuteScriptAsync(script);
            return true;
        }

        std::wstring haystack = SourceTextBox().Text().c_str();
        int start = SourceTextBox().SelectionStart() + SourceTextBox().SelectionLength();
        auto found = haystack.find(needle, static_cast<size_t>(start));
        if (found == std::wstring::npos && start > 0)
        {
            found = haystack.find(needle);
        }

        if (found == std::wstring::npos)
        {
            StatusTextBlock().Text(L"No matches");
            return false;
        }

        SourceTextBox().Select(static_cast<int32_t>(found), static_cast<int32_t>(needle.size()));
        SourceTextBox().Focus(FocusState::Programmatic);
        return true;
    }
}
