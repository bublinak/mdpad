#include "pch.h"
#include "DocumentState.h"
#include "TextEncoding.h"

void DocumentState::New()
{
    m_path.clear();
    m_text.clear();
    m_dirty = false;
}

void DocumentState::LoadFromFile(std::filesystem::path const& path)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
    {
        throw std::runtime_error("Unable to open file for reading.");
    }

    std::string bytes((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    m_path = std::filesystem::absolute(path).lexically_normal();
    m_text = FromUtf8(bytes);
    m_dirty = false;
}

void DocumentState::Save()
{
    if (!HasPath())
    {
        throw std::logic_error("Cannot save a document without a path.");
    }

    SaveAs(m_path);
}

void DocumentState::SaveAs(std::filesystem::path const& path)
{
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream)
    {
        throw std::runtime_error("Unable to open file for writing.");
    }

    std::string const bytes = ToUtf8(m_text);
    stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    m_path = std::filesystem::absolute(path).lexically_normal();
    m_dirty = false;
}

std::wstring const& DocumentState::Text() const noexcept
{
    return m_text;
}

void DocumentState::SetText(std::wstring text)
{
    if (m_text == text)
    {
        return;
    }

    m_text = std::move(text);
    m_dirty = true;
}

std::filesystem::path const& DocumentState::Path() const noexcept
{
    return m_path;
}

std::filesystem::path DocumentState::Directory() const
{
    if (!HasPath())
    {
        return {};
    }

    return m_path.parent_path();
}

std::wstring DocumentState::DisplayName() const
{
    if (!HasPath())
    {
        return L"Untitled";
    }

    return m_path.filename().wstring();
}

bool DocumentState::HasPath() const noexcept
{
    return !m_path.empty();
}

bool DocumentState::IsDirty() const noexcept
{
    return m_dirty;
}

void DocumentState::MarkClean() noexcept
{
    m_dirty = false;
}
