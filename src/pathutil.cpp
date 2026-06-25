#include "pathutil.h"
#include <cwchar>

void PathUtil::GetPortableFilePath(const wchar_t *fileName, wchar_t (&out)[MAX_PATH])
{
    wchar_t modulePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, modulePath, MAX_PATH);

    out[0] = L'\0';

    // Resolve symlinks (e.g. WinGet Links dir -> actual Packages dir)
    HANDLE hFile = CreateFileW(modulePath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL, nullptr);
    if(hFile != INVALID_HANDLE_VALUE)
    {
        DWORD len = GetFinalPathNameByHandleW(hFile, out, MAX_PATH, FILE_NAME_NORMALIZED);
        CloseHandle(hFile);

        // GetFinalPathNameByHandle returns \\?\ prefix -- skip it
        if(len > 4 && wcsncmp(out, L"\\\\?\\", 4) == 0)
            wmemmove(out, out + 4, len - 4 + 1);
    }

    // Fall back to module path if resolution failed
    if(!out[0])
        wcscpy_s(out, modulePath);

    wchar_t *lastSlash = wcsrchr(out, L'\\');
    if(lastSlash)
        wcscpy_s(lastSlash + 1, MAX_PATH - (lastSlash + 1 - out), fileName);
}
