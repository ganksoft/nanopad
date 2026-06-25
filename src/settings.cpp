#include "settings.h"
#include "pathutil.h"
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <memory>

int Settings::ParseInt(const wchar_t *value, int def)
{
    if(!value || !value[0])
        return def;
    return _wtoi(value);
}

void Settings::ParseLine(const wchar_t *line)
{
    // Skip whitespace and section headers
    while(*line == L' ' || *line == L'\t')
        line++;
    if(*line == L'[' || *line == L';' || *line == L'\0')
        return;

    const wchar_t *eq = wcschr(line, L'=');
    if(!eq)
        return;

    // Extract key (trimmed)
    wchar_t key[64] = {};
    size_t keyLen   = eq - line;
    while(keyLen > 0 && (line[keyLen - 1] == L' ' || line[keyLen - 1] == L'\t'))
        keyLen--;
    if(keyLen == 0 || keyLen >= 63)
        return;
    wmemcpy(key, line, keyLen);
    key[keyLen] = L'\0';

    // Extract value (trimmed)
    const wchar_t *val = eq + 1;
    while(*val == L' ' || *val == L'\t')
        val++;

    // Font settings
    if(wcscmp(key, L"FontFace") == 0)
    {
        wcsncpy_s(font.lfFaceName, val, LF_FACESIZE - 1);
        fontLoaded = true;
    }
    else if(wcscmp(key, L"FontSize") == 0)
    {
        // FontSize is in points -- convert to lfHeight using screen DPI
        int pts = ParseInt(val, 11);
        HDC hdc = GetDC(nullptr);
        int dpi = hdc ? GetDeviceCaps(hdc, LOGPIXELSY) : 96;
        if(hdc)
            ReleaseDC(nullptr, hdc);
        font.lfHeight = -MulDiv(pts, dpi, 72);
        fontLoaded    = true;
    }
    else if(wcscmp(key, L"FontWeight") == 0)
    {
        font.lfWeight = ParseInt(val, FW_NORMAL);
        fontLoaded    = true;
    }
    else if(wcscmp(key, L"FontItalic") == 0)
    {
        font.lfItalic = (BYTE)ParseInt(val, 0);
    }
    else if(wcscmp(key, L"FontCharSet") == 0)
    {
        font.lfCharSet = (BYTE)ParseInt(val, DEFAULT_CHARSET);
    }
    else if(wcscmp(key, L"FontQuality") == 0)
    {
        font.lfQuality = (BYTE)ParseInt(val, CLEARTYPE_QUALITY);
    }
    // Theme
    else if(wcscmp(key, L"ThemeMode") == 0)
    {
        themeMode       = (DWORD)ParseInt(val, 0);
        themeModeLoaded = true;
    }
    // Window placement
    else if(wcscmp(key, L"WindowLeft") == 0)
    {
        windowPlacement.rcNormalPosition.left = ParseInt(val, 0);
        windowPlacementLoaded                 = true;
    }
    else if(wcscmp(key, L"WindowTop") == 0)
    {
        windowPlacement.rcNormalPosition.top = ParseInt(val, 0);
        windowPlacementLoaded                = true;
    }
    else if(wcscmp(key, L"WindowRight") == 0)
    {
        windowPlacement.rcNormalPosition.right = ParseInt(val, 0);
    }
    else if(wcscmp(key, L"WindowBottom") == 0)
    {
        windowPlacement.rcNormalPosition.bottom = ParseInt(val, 0);
    }
    else if(wcscmp(key, L"WindowShowCmd") == 0)
    {
        windowPlacement.showCmd = (UINT)ParseInt(val, SW_SHOWNORMAL);
    }
    // Notepad replacement
    else if(wcscmp(key, L"OriginalDebugger") == 0)
    {
        wcsncpy_s(originalDebugger, val, MAX_PATH - 1);
        originalDebuggerLoaded = true;
    }
    else if(wcscmp(key, L"OriginalUseFilter") == 0)
    {
        originalUseFilter       = (DWORD)ParseInt(val, 0);
        originalUseFilterLoaded = true;
    }
}

void Settings::Load()
{
    wchar_t iniPath[MAX_PATH];
    PathUtil::GetPortableFilePath(L"nanopad.ini", iniPath);

    HANDLE hFile =
        CreateFileW(iniPath, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if(hFile == INVALID_HANDLE_VALUE)
        return;

    DWORD fileSize = GetFileSize(hFile, nullptr);
    // Sanity check -- settings file should never be large
    if(fileSize == 0 || fileSize > 64 * 1024)
    {
        CloseHandle(hFile);
        return;
    }

    // Read as UTF-8, convert to wide
    auto buf = std::make_unique<char[]>(fileSize + 1);
    DWORD bytesRead;
    ::ReadFile(hFile, buf.get(), fileSize, &bytesRead, nullptr);
    CloseHandle(hFile);
    buf[bytesRead] = '\0';

    int wlen  = MultiByteToWideChar(CP_UTF8, 0, buf.get(), (int)bytesRead, nullptr, 0);
    auto wbuf = std::make_unique<wchar_t[]>(wlen + 1);
    MultiByteToWideChar(CP_UTF8, 0, buf.get(), (int)bytesRead, wbuf.get(), wlen);
    wbuf[wlen] = L'\0';
    buf.reset();

    // Initialize window placement length field
    windowPlacement.length = sizeof(WINDOWPLACEMENT);

    // Parse line by line
    wchar_t *ctx  = nullptr;
    wchar_t *line = wcstok_s(wbuf.get(), L"\r\n", &ctx);
    while(line)
    {
        ParseLine(line);
        line = wcstok_s(nullptr, L"\r\n", &ctx);
    }
}

void Settings::Save()
{
    wchar_t iniPath[MAX_PATH];
    PathUtil::GetPortableFilePath(L"nanopad.ini", iniPath);

    wchar_t buf[2048];
    int pos = 0;

    pos += swprintf_s(buf + pos, _countof(buf) - pos, L"[Font]\r\n");
    pos += swprintf_s(buf + pos, _countof(buf) - pos, L"FontFace=%s\r\n", font.lfFaceName);

    // Convert lfHeight to point size for human readability
    HDC hdc = GetDC(nullptr);
    int dpi = hdc ? GetDeviceCaps(hdc, LOGPIXELSY) : 96;
    if(hdc)
        ReleaseDC(nullptr, hdc);
    int pointSize = MulDiv(font.lfHeight < 0 ? -font.lfHeight : font.lfHeight, 72, dpi);
    pos += swprintf_s(buf + pos, _countof(buf) - pos, L"FontSize=%d\r\n", pointSize);

    pos += swprintf_s(buf + pos, _countof(buf) - pos, L"FontWeight=%d\r\n", font.lfWeight);
    pos += swprintf_s(buf + pos, _countof(buf) - pos, L"FontItalic=%d\r\n", font.lfItalic);
    pos += swprintf_s(buf + pos, _countof(buf) - pos, L"FontCharSet=%d\r\n", font.lfCharSet);
    pos += swprintf_s(buf + pos, _countof(buf) - pos, L"FontQuality=%d\r\n", font.lfQuality);

    pos += swprintf_s(buf + pos, _countof(buf) - pos, L"\r\n[Theme]\r\n");
    pos += swprintf_s(buf + pos, _countof(buf) - pos, L"ThemeMode=%u\r\n", themeMode);

    pos += swprintf_s(buf + pos, _countof(buf) - pos, L"\r\n[Window]\r\n");
    pos += swprintf_s(buf + pos, _countof(buf) - pos, L"WindowLeft=%d\r\n", windowPlacement.rcNormalPosition.left);
    pos += swprintf_s(buf + pos, _countof(buf) - pos, L"WindowTop=%d\r\n", windowPlacement.rcNormalPosition.top);
    pos += swprintf_s(buf + pos, _countof(buf) - pos, L"WindowRight=%d\r\n", windowPlacement.rcNormalPosition.right);
    pos += swprintf_s(buf + pos, _countof(buf) - pos, L"WindowBottom=%d\r\n", windowPlacement.rcNormalPosition.bottom);
    pos += swprintf_s(buf + pos, _countof(buf) - pos, L"WindowShowCmd=%u\r\n", windowPlacement.showCmd);

    if(originalDebuggerLoaded)
    {
        pos += swprintf_s(buf + pos, _countof(buf) - pos, L"\r\n[NotepadReplace]\r\n");
        pos += swprintf_s(buf + pos, _countof(buf) - pos, L"OriginalDebugger=%s\r\n", originalDebugger);
        pos += swprintf_s(buf + pos, _countof(buf) - pos, L"OriginalUseFilter=%u\r\n", originalUseFilter);
    }

    // Convert to UTF-8
    int utf8Len = WideCharToMultiByte(CP_UTF8, 0, buf, pos, nullptr, 0, nullptr, nullptr);
    auto utf8   = std::make_unique<char[]>(utf8Len);
    WideCharToMultiByte(CP_UTF8, 0, buf, pos, utf8.get(), utf8Len, nullptr, nullptr);

    HANDLE hFile = CreateFileW(iniPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if(hFile != INVALID_HANDLE_VALUE)
    {
        DWORD written;
        ::WriteFile(hFile, utf8.get(), utf8Len, &written, nullptr);
        CloseHandle(hFile);
    }
}
