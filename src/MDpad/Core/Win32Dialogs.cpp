#include "pch.h"
#include "Win32Dialogs.h"

namespace
{
    enum class SaveKind
    {
        Markdown,
        Html
    };

    std::optional<std::filesystem::path> ShowFileDialog(HWND owner, bool save, std::filesystem::path const& currentPath, SaveKind kind = SaveKind::Markdown)
    {
        winrt::com_ptr<IFileDialog> dialog;

        if (save)
        {
            winrt::com_ptr<IFileSaveDialog> saveDialog;
            winrt::check_hresult(CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(saveDialog.put())));
            dialog = saveDialog.as<IFileDialog>();
        }
        else
        {
            winrt::com_ptr<IFileOpenDialog> openDialog;
            winrt::check_hresult(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(openDialog.put())));
            dialog = openDialog.as<IFileDialog>();
        }

        COMDLG_FILTERSPEC const markdownFilters[] = {
            { L"Markdown files", L"*.md;*.markdown" },
            { L"Text files", L"*.txt" },
            { L"All files", L"*.*" },
        };

        COMDLG_FILTERSPEC const htmlFilters[] = {
            { L"HTML files", L"*.html;*.htm" },
            { L"All files", L"*.*" },
        };

        if (kind == SaveKind::Html)
        {
            dialog->SetFileTypes(static_cast<UINT>(_countof(htmlFilters)), htmlFilters);
        }
        else
        {
            dialog->SetFileTypes(static_cast<UINT>(_countof(markdownFilters)), markdownFilters);
        }
        dialog->SetFileTypeIndex(1);

        if (save)
        {
            auto saveDialog = dialog.as<IFileSaveDialog>();
            saveDialog->SetDefaultExtension(kind == SaveKind::Html ? L"html" : L"md");
        }

        if (!currentPath.empty())
        {
            dialog->SetFileName(currentPath.filename().c_str());
        }

        HRESULT const hr = dialog->Show(owner);
        if (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED))
        {
            return std::nullopt;
        }
        if (FAILED(hr))
        {
            throw std::runtime_error("File dialog failed.");
        }

        winrt::com_ptr<IShellItem> item;
        winrt::check_hresult(dialog->GetResult(item.put()));

        PWSTR rawPath{};
        winrt::check_hresult(item->GetDisplayName(SIGDN_FILESYSPATH, &rawPath));
        std::wstring path(rawPath);
        CoTaskMemFree(rawPath);
        return std::filesystem::path(path);
    }
}

std::optional<std::filesystem::path> ShowOpenMarkdownDialog(HWND owner)
{
    return ShowFileDialog(owner, false, {});
}

std::optional<std::filesystem::path> ShowSaveMarkdownDialog(HWND owner, std::filesystem::path const& currentPath)
{
    return ShowFileDialog(owner, true, currentPath);
}

std::optional<std::filesystem::path> ShowSaveHtmlDialog(HWND owner, std::filesystem::path const& currentPath)
{
    return ShowFileDialog(owner, true, currentPath, SaveKind::Html);
}

int ShowDirtyPrompt(HWND owner, std::wstring const& displayName)
{
    std::wstring message = L"Do you want to save changes to " + displayName + L"?";
    return MessageBoxW(owner, message.c_str(), L"MDpad", MB_ICONWARNING | MB_YESNOCANCEL);
}

void ShowError(HWND owner, std::wstring const& message)
{
    MessageBoxW(owner, message.c_str(), L"MDpad", MB_ICONERROR | MB_OK);
}
