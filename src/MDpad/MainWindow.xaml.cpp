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
using namespace Microsoft::UI::Xaml::Media;
using namespace Microsoft::Web::WebView2::Core;
using namespace Windows::ApplicationModel;
using namespace Windows::Foundation;

namespace
{
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
        SystemBackdrop(DesktopAcrylicBackdrop{});
        Closed({ this, &MainWindow::OnClosed });
        LoadSettings();
        ApplyWindowSize();
        ApplyAppTheme();
        ApplyTransparency();

        m_viewMode = m_settings.openFormattedByDefault ? ViewMode::Formatted : ViewMode::Syntax;
        m_suppressTextChanged = true;
        SourceTextBox().Text(m_document.Text());
        m_suppressTextChanged = false;

        ApplyViewMode();
        ApplyZoom();
        ApplyTitle();

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

        TextBlock transparencyLabel;
        transparencyLabel.Text(L"Transparency effect: " + to_hstring(m_settings.transparencyPercent) + L"%");

        Slider transparencySlider;
        transparencySlider.Minimum(0);
        transparencySlider.Maximum(100);
        transparencySlider.StepFrequency(1);
        transparencySlider.Value(static_cast<double>(m_settings.transparencyPercent));
        transparencySlider.ValueChanged([this, transparencyLabel](IInspectable const&, RangeBaseValueChangedEventArgs const& args) {
            m_settings.transparencyPercent = std::clamp(static_cast<int>(std::round(args.NewValue())), 0, 100);
            transparencyLabel.Text(L"Transparency effect: " + to_hstring(m_settings.transparencyPercent) + L"%");
            ApplyTransparency();
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

        content.Children().Append(transparencyLabel);
        content.Children().Append(transparencySlider);
        content.Children().Append(themeLabel);
        content.Children().Append(themeBox);
        content.Children().Append(defaultModeLabel);
        content.Children().Append(defaultModeBox);
        content.Children().Append(links);

        dialog.Content(content);
        co_await dialog.ShowAsync();
    }

    void MainWindow::OpenPath(std::wstring const& path)
    {
        try
        {
            m_document.LoadFromFile(path);
            m_suppressTextChanged = true;
            SourceTextBox().Text(m_document.Text());
            m_suppressTextChanged = false;
            ApplyTitle();
            RenderPreview();
        }
        catch (std::exception const& error)
        {
            ShowError(WindowHandle(), ErrorText("Could not open the selected file.", error));
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
        ApplyTitle();
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

    void MainWindow::ApplyTransparency()
    {
        HWND const hwnd = WindowHandle();
        if (!hwnd)
        {
            return;
        }

        int const transparency = std::clamp(m_settings.transparencyPercent, 0, 100);
        LONG_PTR const style = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
        SetWindowLongPtrW(hwnd, GWL_EXSTYLE, style | WS_EX_LAYERED);

        BYTE const alpha = static_cast<BYTE>(std::clamp(255 - static_cast<int>(std::round(transparency * 1.55)), 100, 255));
        SetLayeredWindowAttributes(hwnd, 0, alpha, LWA_ALPHA);
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
            OpenExternalLink(href);
        }
    }

    void MainWindow::OnPreviewNavigationCompleted(CoreWebView2 const&, CoreWebView2NavigationCompletedEventArgs const&)
    {
        m_previewReady = true;
        RenderPreview();
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
