#include "pch.h"
#include "MainWindow.xaml.h"
#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif

#include "Core/HtmlUtil.h"
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

    std::wstring BuildPreviewJson(std::string_view html, std::wstring_view baseUri, double zoom)
    {
        std::wstringstream payload;
        payload << L"{"
            << L"\"kind\":\"render\","
            << L"\"html\":" << JsonStringUtf8(html) << L","
            << L"\"baseUri\":" << JsonString(baseUri) << L","
            << L"\"zoom\":" << zoom
            << L"}";
        return payload.str();
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

    void MainWindow::Exit_Click(IInspectable const&, RoutedEventArgs const&)
    {
        if (ConfirmDiscardIfDirty())
        {
            Close();
        }
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
        m_pendingPreviewJson = BuildPreviewJson(html, baseUri, m_settings.zoom);

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
