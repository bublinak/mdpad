#pragma once

class DocumentState
{
public:
    void New();
    void LoadFromFile(std::filesystem::path const& path);
    void Save();
    void SaveAs(std::filesystem::path const& path);

    std::wstring const& Text() const noexcept;
    void SetText(std::wstring text);

    std::filesystem::path const& Path() const noexcept;
    std::filesystem::path Directory() const;
    std::wstring DisplayName() const;
    bool HasPath() const noexcept;
    bool IsDirty() const noexcept;
    void MarkClean() noexcept;

private:
    std::filesystem::path m_path;
    std::wstring m_text;
    bool m_dirty{ false };
};
