#include "pch.h"
#include "SettingsStore.h"

#include <winrt/Windows.Foundation.Collections.h>

using namespace winrt;
using namespace Windows::Foundation::Collections;
using namespace Windows::Storage;

namespace
{
    bool HasPackageIdentity()
    {
        UINT32 length = 0;
        auto const result = GetCurrentPackageFullName(&length, nullptr);
        return result != APPMODEL_ERROR_NO_PACKAGE;
    }

    std::filesystem::path LocalSettingsPath()
    {
        std::wstring localAppData(MAX_PATH, L'\0');
        DWORD copied = GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData.data(), static_cast<DWORD>(localAppData.size()));
        while (copied >= localAppData.size())
        {
            localAppData.resize(copied + 1);
            copied = GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData.data(), static_cast<DWORD>(localAppData.size()));
        }

        if (copied == 0)
        {
            return std::filesystem::temp_directory_path() / L"MDpad" / L"settings.txt";
        }

        localAppData.resize(copied);
        return std::filesystem::path(localAppData) / L"MDpad" / L"settings.txt";
    }

    template <typename T>
    T TryGet(IPropertySet const& values, wchar_t const* key, T fallback)
    {
        auto boxed = values.TryLookup(key);
        if (!boxed)
        {
            return fallback;
        }

        return unbox_value_or<T>(boxed, fallback);
    }

    int ClampWindowSize(int value, int fallback)
    {
        if (value <= 0)
        {
            return fallback;
        }
        return std::clamp(value, 420, 3840);
    }

    AppTheme ClampTheme(int value)
    {
        if (value < static_cast<int>(AppTheme::System) || value > static_cast<int>(AppTheme::Dark))
        {
            return AppTheme::System;
        }
        return static_cast<AppTheme>(value);
    }
}

AppSettings SettingsStore::Load() const
{
    AppSettings settings;
    if (HasPackageIdentity())
    {
        auto values = ApplicationData::Current().LocalSettings().Values();
        settings.openFormattedByDefault = TryGet(values, L"openFormattedByDefault", settings.openFormattedByDefault);
        settings.wordWrap = TryGet(values, L"wordWrap", settings.wordWrap);
        settings.appTheme = ClampTheme(TryGet(values, L"appTheme", static_cast<int>(settings.appTheme)));
        settings.transparencyPercent = std::clamp(TryGet(values, L"transparencyPercent", settings.transparencyPercent), 0, 100);
        settings.zoom = std::clamp(TryGet(values, L"zoom", settings.zoom), 0.5, 2.0);
        settings.windowWidth = ClampWindowSize(TryGet(values, L"windowWidth", settings.windowWidth), settings.windowWidth);
        settings.windowHeight = ClampWindowSize(TryGet(values, L"windowHeight", settings.windowHeight), settings.windowHeight);
        return settings;
    }

    std::ifstream file(LocalSettingsPath());
    std::string line;
    while (std::getline(file, line))
    {
        auto const separator = line.find('=');
        if (separator == std::string::npos)
        {
            continue;
        }

        auto const key = line.substr(0, separator);
        auto const value = line.substr(separator + 1);
        if (key == "openFormattedByDefault")
        {
            settings.openFormattedByDefault = value == "true";
        }
        else if (key == "wordWrap")
        {
            settings.wordWrap = value == "true";
        }
        else if (key == "appTheme")
        {
            try
            {
                settings.appTheme = ClampTheme(std::stoi(value));
            }
            catch (...)
            {
                settings.appTheme = AppTheme::System;
            }
        }
        else if (key == "transparencyPercent")
        {
            try
            {
                settings.transparencyPercent = std::clamp(std::stoi(value), 0, 100);
            }
            catch (...)
            {
                settings.transparencyPercent = 0;
            }
        }
        else if (key == "zoom")
        {
            try
            {
                settings.zoom = std::clamp(std::stod(value), 0.5, 2.0);
            }
            catch (...)
            {
                settings.zoom = 1.0;
            }
        }
        else if (key == "windowWidth")
        {
            try
            {
                settings.windowWidth = ClampWindowSize(std::stoi(value), settings.windowWidth);
            }
            catch (...)
            {
                settings.windowWidth = 780;
            }
        }
        else if (key == "windowHeight")
        {
            try
            {
                settings.windowHeight = ClampWindowSize(std::stoi(value), settings.windowHeight);
            }
            catch (...)
            {
                settings.windowHeight = 560;
            }
        }
    }

    return settings;
}

void SettingsStore::Save(AppSettings const& settings) const
{
    if (HasPackageIdentity())
    {
        auto values = ApplicationData::Current().LocalSettings().Values();
        values.Insert(L"openFormattedByDefault", box_value(settings.openFormattedByDefault));
        values.Insert(L"wordWrap", box_value(settings.wordWrap));
        values.Insert(L"appTheme", box_value(static_cast<int>(settings.appTheme)));
        values.Insert(L"transparencyPercent", box_value(settings.transparencyPercent));
        values.Insert(L"zoom", box_value(settings.zoom));
        values.Insert(L"windowWidth", box_value(settings.windowWidth));
        values.Insert(L"windowHeight", box_value(settings.windowHeight));
        return;
    }

    auto const path = LocalSettingsPath();
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::trunc);
    file << "openFormattedByDefault=" << (settings.openFormattedByDefault ? "true" : "false") << '\n';
    file << "wordWrap=" << (settings.wordWrap ? "true" : "false") << '\n';
    file << "appTheme=" << static_cast<int>(settings.appTheme) << '\n';
    file << "transparencyPercent=" << settings.transparencyPercent << '\n';
    file << "zoom=" << settings.zoom << '\n';
    file << "windowWidth=" << settings.windowWidth << '\n';
    file << "windowHeight=" << settings.windowHeight << '\n';
}
