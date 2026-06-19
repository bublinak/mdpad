#include "pch.h"
#include "App.xaml.h"
#include "MainWindow.xaml.h"

#include <winrt/Microsoft.Windows.AppLifecycle.h>
#include <winrt/Windows.ApplicationModel.Activation.h>
#include <winrt/Windows.Storage.h>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::Windows::AppLifecycle;
using namespace Windows::ApplicationModel::Activation;
using namespace Windows::Storage;

namespace
{
    bool HasPackageIdentity()
    {
        UINT32 length = 0;
        auto const result = GetCurrentPackageFullName(&length, nullptr);
        return result != APPMODEL_ERROR_NO_PACKAGE;
    }

    std::optional<std::wstring> CommandLineFilePath()
    {
        int argc = 0;
        PWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        if (!argv)
        {
            return std::nullopt;
        }

        std::optional<std::wstring> path;
        if (argc > 1)
        {
            path = argv[1];
        }

        LocalFree(argv);
        return path;
    }
}

namespace winrt::MDpad::implementation
{
    App::App()
    {
        InitializeComponent();
    }

    void App::OnLaunched(Microsoft::UI::Xaml::LaunchActivatedEventArgs const&)
    {
        m_window = make<MainWindow>();
        m_window.Activate();

        auto windowImpl = get_self<MainWindow>(m_window);
        if (!HasPackageIdentity())
        {
            if (auto path = CommandLineFilePath())
            {
                windowImpl->OpenPath(*path);
            }
            return;
        }

        try
        {
            auto const activatedArgs = AppInstance::GetCurrent().GetActivatedEventArgs();
            if (activatedArgs.Kind() != ExtendedActivationKind::File)
            {
                return;
            }

            auto fileArgs = activatedArgs.Data().try_as<IFileActivatedEventArgs>();
            if (!fileArgs || fileArgs.Files().Size() == 0)
            {
                return;
            }

            auto storageFile = fileArgs.Files().GetAt(0).try_as<StorageFile>();
            if (!storageFile || storageFile.Path().empty())
            {
                return;
            }

            windowImpl->OpenPath(storageFile.Path().c_str());
        }
        catch (hresult_error const&)
        {
            return;
        }
    }
}
