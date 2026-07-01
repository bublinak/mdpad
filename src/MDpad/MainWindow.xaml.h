#pragma once

#include "MainWindow.g.h"
#include "Core/DocumentState.h"
#include "Core/MarkdownRenderer.h"
#include "Core/SettingsStore.h"

namespace winrt::MDpad::implementation
{
    enum class ViewMode
    {
        Formatted,
        Syntax
    };

    struct MainWindow : MainWindowT<MainWindow>
    {
        MainWindow();

        void OpenPath(std::wstring const& path);

        void New_Click(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void Open_Click(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void Save_Click(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void SaveAs_Click(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void SaveHtml_Click(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void Exit_Click(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void Settings_Click(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void Back_Click(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void Forward_Click(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void ToggleMode_Click(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OpenFormattedByDefault_Click(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void WordWrap_Click(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void ZoomIn_Click(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void ZoomOut_Click(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void ResetZoom_Click(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void Undo_Click(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void Redo_Click(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void Cut_Click(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void Copy_Click(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void Paste_Click(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void Delete_Click(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void Find_Click(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void Replace_Click(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void FindNext_Click(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void ReplaceNext_Click(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void CloseFind_Click(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void SelectAll_Click(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void Bold_Click(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void Italic_Click(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void InlineCode_Click(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void Strikethrough_Click(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void Link_Click(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void Quote_Click(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void BulletList_Click(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void NumberedList_Click(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void SourceTextBox_TextChanged(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::Controls::TextChangedEventArgs const& args);
        void OnClosed(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::WindowEventArgs const& args);
        void OnAppWindowClosing(Microsoft::UI::Windowing::AppWindow const& sender, Microsoft::UI::Windowing::AppWindowClosingEventArgs const& args);

    private:
        fire_and_forget InitializePreviewAsync();
        fire_and_forget SaveGeneratedHtmlAsync();
        fire_and_forget ShowSettingsAsync();
        fire_and_forget NewWithPromptAsync();
        fire_and_forget OpenWithPromptAsync();
        fire_and_forget RequestCloseAsync();
        fire_and_forget NavigateHistoryAsync(int offset);
        fire_and_forget OpenMarkdownFileInCurrentWindowAsync(std::filesystem::path path);
        winrt::Windows::Foundation::IAsyncOperation<bool> ConfirmDiscardIfDirtyAsync();
        void RegisterKeyboardAccelerators();
        bool HandleNativeShortcut(winrt::Windows::System::VirtualKey key, bool control, bool shift, bool alt);
        bool HandlePreviewShortcut(std::wstring const& command);
        void RequestClose();
        void LoadSettings();
        void SaveSettings();
        void ApplyWindowSize();
        void SaveWindowSize();
        void ApplyAppTheme();
        void ApplyAcrylicEffect();
        void ApplyPreviewBackgroundColor();
        void StopAcrylicBackdrop();
        void ApplyNavigationState();
        void ApplyViewMode();
        void ApplyEditCommandState();
        void ApplyTitle();
        void ApplyZoom();
        void RenderPreview();
        bool EnsureDocumentResourceMapping();
        void PostPreviewPayload();
        bool OpenDocumentPath(std::filesystem::path const& path, bool addToHistory);
        void AddHistoryEntry(std::filesystem::path const& path);
        void NavigateHistory(int offset);
        bool Save();
        bool SaveAs();
        HWND WindowHandle();
        std::filesystem::path SuggestedHtmlPath() const;
        void OnPreviewMessage(Microsoft::Web::WebView2::Core::CoreWebView2 const& sender, Microsoft::Web::WebView2::Core::CoreWebView2WebMessageReceivedEventArgs const& args);
        void OnPreviewNavigationCompleted(Microsoft::Web::WebView2::Core::CoreWebView2 const& sender, Microsoft::Web::WebView2::Core::CoreWebView2NavigationCompletedEventArgs const& args);
        void OpenPreviewLink(std::wstring const& href);
        std::optional<std::filesystem::path> ResolveDocumentFileLink(std::wstring const& href) const;
        std::optional<std::filesystem::path> ResolveFileUri(std::wstring const& href) const;
        void OpenLocalFileLink(std::filesystem::path const& path);
        void OpenMarkdownFileInNewWindow(std::filesystem::path const& path);
        void OpenExternalLink(std::wstring const& href);
        void ShowFindPanel(bool replaceMode);
        bool FindNext();
        bool ApplyInlineMarkdown(std::wstring_view prefix, std::wstring_view suffix, std::wstring_view placeholder);
        bool ApplyMarkdownLink();
        bool ApplyLinePrefix(std::wstring_view prefix);
        bool ApplyOrderedList();

        DocumentState m_document;
        MarkdownRenderer m_renderer;
        SettingsStore m_settingsStore;
        AppSettings m_settings{};
        ViewMode m_viewMode{ ViewMode::Formatted };
        std::vector<std::filesystem::path> m_fileHistory;
        std::filesystem::path m_mappedDocumentDirectory;
        std::wstring m_pendingPreviewJson;
        Microsoft::UI::Composition::SystemBackdrops::DesktopAcrylicController m_acrylicController{ nullptr };
        Microsoft::UI::Composition::SystemBackdrops::SystemBackdropConfiguration m_acrylicConfiguration{ nullptr };
        int m_historyIndex{ -1 };
        bool m_previewReady{ false };
        bool m_suppressTextChanged{ false };
        bool m_acrylicBackdropEnabled{ false };
        bool m_closeConfirmed{ false };
        bool m_dirtyPromptActive{ false };
    };
}

namespace winrt::MDpad::factory_implementation
{
    struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow>
    {
    };
}
