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
        void Exit_Click(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
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
        void SourceTextBox_TextChanged(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::Controls::TextChangedEventArgs const& args);
        void OnClosed(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::WindowEventArgs const& args);

    private:
        fire_and_forget InitializePreviewAsync();
        void LoadSettings();
        void SaveSettings();
        void ApplyWindowSize();
        void SaveWindowSize();
        void ApplyViewMode();
        void ApplyEditCommandState();
        void ApplyTitle();
        void ApplyZoom();
        void RenderPreview();
        bool EnsureDocumentResourceMapping();
        void PostPreviewPayload();
        bool ConfirmDiscardIfDirty();
        bool Save();
        bool SaveAs();
        HWND WindowHandle();
        void OnPreviewMessage(Microsoft::Web::WebView2::Core::CoreWebView2 const& sender, Microsoft::Web::WebView2::Core::CoreWebView2WebMessageReceivedEventArgs const& args);
        void OnPreviewNavigationCompleted(Microsoft::Web::WebView2::Core::CoreWebView2 const& sender, Microsoft::Web::WebView2::Core::CoreWebView2NavigationCompletedEventArgs const& args);
        void OpenExternalLink(std::wstring const& href);
        void ShowFindPanel(bool replaceMode);
        bool FindNext();

        DocumentState m_document;
        MarkdownRenderer m_renderer;
        SettingsStore m_settingsStore;
        AppSettings m_settings{};
        ViewMode m_viewMode{ ViewMode::Formatted };
        std::filesystem::path m_mappedDocumentDirectory;
        std::wstring m_pendingPreviewJson;
        bool m_previewReady{ false };
        bool m_suppressTextChanged{ false };
    };
}

namespace winrt::MDpad::factory_implementation
{
    struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow>
    {
    };
}
