#pragma once

struct AppSettings
{
    bool openFormattedByDefault{ true };
    bool wordWrap{ true };
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
