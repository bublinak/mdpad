#pragma once

enum class AppTheme
{
    System = 0,
    Light = 1,
    Dark = 2
};

struct AppSettings
{
    bool openFormattedByDefault{ true };
    bool wordWrap{ true };
    AppTheme appTheme{ AppTheme::System };
    int transparencyPercent{ 0 };
    double zoom{ 1.0 };
    int windowWidth{ 780 };
    int windowHeight{ 560 };
};

class SettingsStore
{
public:
    AppSettings Load() const;
    void Save(AppSettings const& settings) const;
};
