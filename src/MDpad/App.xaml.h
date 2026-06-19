#pragma once

#include <winrt/MDpad.h>
#include "App.xaml.g.h"

namespace winrt::MDpad::implementation
{
    struct App : AppT<App>
    {
        App();

        void OnLaunched(Microsoft::UI::Xaml::LaunchActivatedEventArgs const& args);

    private:
        winrt::MDpad::MainWindow m_window{ nullptr };
    };
}
