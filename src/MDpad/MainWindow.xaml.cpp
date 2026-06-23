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

    VirtualKeyModifiers CombineModifiers(VirtualKeyModifiers left, VirtualKeyModifiers right)
    {
        return static_cast<VirtualKeyModifiers>(static_cast<uint32_t>(left) | static_cast<uint32_t>(right));
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
        co_await PreviewWebView().EnsureCoreWebView2Async();

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
        auto add = [this](VirtualKey key, VirtualKeyModifiers modifiers, std::function<bool()> action) {
            KeyboardAccelerator accelerator;
            accelerator.Key(key);
            accelerator.Modifiers(modifiers);
            accelerator.Invoked([action = std::move(action)](KeyboardAccelerator const&, KeyboardAcceleratorInvokedEventArgs const& args) {
                args.Handled(action());
            });
            RootGrid().KeyboardAccelerators().Append(accelerator);
        };

        auto invoke = [this](void (MainWindow::*handler)(IInspectable const&, RoutedEventArgs const&)) {
            IInspectable sender{ nullptr };
            RoutedEventArgs args{ nullptr };
            (this->*handler)(sender, args);
        };

        add(VirtualKey::N, VirtualKeyModifiers::Control, [this, invoke] {
            invoke(&MainWindow::New_Click);
            return true;
        });
        add(VirtualKey::O, VirtualKeyModifiers::Control, [this, invoke] {
            invoke(&MainWindow::Open_Click);
            return true;
        });
        add(VirtualKey::S, VirtualKeyModifiers::Control, [this] {
            return Save();
        });
        add(VirtualKey::S, CombineModifiers(VirtualKeyModifiers::Control, VirtualKeyModifiers::Shift), [this] {
            return SaveAs();
        });

        add(VirtualKey::F, VirtualKeyModifiers::Control, [this] {
            ShowFindPanel(false);
            return true;
        });
        add(VirtualKey::H, VirtualKeyModifiers::Control, [this] {
            if (m_viewMode != ViewMode::Syntax)
            {
                return false;
            }

            ShowFindPanel(true);
            return true;
        });

        add(VirtualKey::Z, VirtualKeyModifiers::Control, [this] {
            if (m_viewMode != ViewMode::Syntax)
            {
                return false;
            }

            SourceTextBox().Undo();
            return true;
        });
        add(VirtualKey::Y, VirtualKeyModifiers::Control, [this] {
            if (m_viewMode != ViewMode::Syntax)
            {
                return false;
            }

            SourceTextBox().Redo();
            return true;
        });
        add(VirtualKey::X, VirtualKeyModifiers::Control, [this] {
            if (m_viewMode != ViewMode::Syntax)
            {
                return false;
            }

            SourceTextBox().CutSelectionToClipboard();
            return true;
        });
        add(VirtualKey::C, VirtualKeyModifiers::Control, [this, invoke] {
            invoke(&MainWindow::Copy_Click);
            return true;
        });
        add(VirtualKey::V, VirtualKeyModifiers::Control, [this] {
            if (m_viewMode != ViewMode::Syntax)
            {
                return false;
            }

            SourceTextBox().PasteFromClipboard();
            return true;
        });
        add(VirtualKey::Delete, VirtualKeyModifiers::None, [this] {
            if (m_viewMode != ViewMode::Syntax)
            {
                return false;
            }

            SourceTextBox().SelectedText(L"");
            return true;
        });
        add(VirtualKey::A, VirtualKeyModifiers::Control, [this, invoke] {
            invoke(&MainWindow::SelectAll_Click);
            return true;
        });

        add(VirtualKey::Add, VirtualKeyModifiers::Control, [this, invoke] {
            invoke(&MainWindow::ZoomIn_Click);
            return true;
        });
        add(static_cast<VirtualKey>(0xBB), VirtualKeyModifiers::Control, [this, invoke] {
            invoke(&MainWindow::ZoomIn_Click);
            return true;
        });
        add(VirtualKey::Subtract, VirtualKeyModifiers::Control, [this, invoke] {
            invoke(&MainWindow::ZoomOut_Click);
            return true;
        });
        add(static_cast<VirtualKey>(0xBD), VirtualKeyModifiers::Control, [this, invoke] {
            invoke(&MainWindow::ZoomOut_Click);
            return true;
        });
        add(VirtualKey::Number0, VirtualKeyModifiers::Control, [this, invoke] {
            invoke(&MainWindow::ResetZoom_Click);
            return true;
        });

        add(VirtualKey::Left, VirtualKeyModifiers::Menu, [this] {
            NavigateHistory(-1);
            return true;
        });
        add(VirtualKey::Right, VirtualKeyModifiers::Menu, [this] {
            NavigateHistory(1);
            return true;
        });
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
        acrylicLabel.Text(L"Acrylic background: " + to_hstring(m_settings.acrylicOpacityPercent) + L"%");

        Slider acrylicSlider;
        acrylicSlider.Minimum(0);
        acrylicSlider.Maximum(100);
        acrylicSlider.StepFrequency(1);
        acrylicSlider.Value(static_cast<double>(m_settings.acrylicOpacityPercent));
        acrylicSlider.ValueChanged([this, acrylicLabel](IInspectable const&, RangeBaseValueChangedEventArgs const& args) {
            m_settings.acrylicOpacityPercent = std::clamp(static_cast<int>(std::round(args.NewValue())), 0, 100);
            acrylicLabel.Text(L"Acrylic background: " + to_hstring(m_settings.acrylicOpacityPercent) + L"%");
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
        if (!ConfirmDiscardIfDirty())
        {
            return;
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
        if (!ConfirmDiscardIfDirty())
        {
            return;
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
        if (ConfirmDiscardIfDirty())
        {
            Close();
        }
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
        SaveWindowSize();
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
    }

    void MainWindow::ApplyAcrylicEffect()
    {
        int const opacity = std::clamp(m_settings.acrylicOpacityPercent, 0, 100);
        bool const enabled = opacity > 0;
        if (enabled != m_acrylicBackdropEnabled)
        {
            if (enabled)
            {
                SystemBackdrop(DesktopAcrylicBackdrop{});
            }
            else
            {
                SystemBackdrop(nullptr);
            }
            m_acrylicBackdropEnabled = enabled;
        }

        DocumentAcrylicLayer().Opacity(enabled ? opacity / 100.0 : 0.0);
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
        int const targetIndex = m_historyIndex + offset;
        if (targetIndex < 0 || static_cast<size_t>(targetIndex) >= m_fileHistory.size())
        {
            return;
        }

        if (!ConfirmDiscardIfDirty())
        {
            return;
        }

        std::filesystem::path const target = m_fileHistory[targetIndex];
        if (OpenDocumentPath(target, false))
        {
            m_historyIndex = targetIndex;
            ApplyNavigationState();
        }
    }

    bool MainWindow::ConfirmDiscardIfDirty()
    {
        if (!m_document.IsDirty())
        {
            return true;
        }

        int const result = ShowDirtyPrompt(WindowHandle(), m_document.DisplayName());
        if (result == IDCANCEL)
        {
            return false;
        }

        if (result == IDYES)
        {
            return Save();
        }

        return true;
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
    }

    void MainWindow::OnPreviewNavigationCompleted(CoreWebView2 const&, CoreWebView2NavigationCompletedEventArgs const&)
    {
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
                if (!ConfirmDiscardIfDirty())
                {
                    return;
                }

                OpenDocumentPath(normalized, true);
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
