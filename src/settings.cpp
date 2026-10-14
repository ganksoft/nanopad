#include "settings.h"
#include "pathutil.h"
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <memory>

namespace
{
enum class FieldType
{
    Long,
    UInt,
    Dword,
    Byte,
    Bool,
    String,
    FontPoints // lfHeight in memory, point size in the file
};

struct Field
{
    const wchar_t *section;
    const wchar_t *key;
    FieldType type;
    void *ptr;
    int def;            // used when the value is empty; buffer size in wchar_t for String
    bool *loaded;       // set when the key is read, optional
    const bool *saveIf; // row is only written when this is true, optional
};

int GetScreenDpi()
{
    HDC hdc = GetDC(nullptr);
    int dpi = hdc ? GetDeviceCaps(hdc, LOGPIXELSY) : 96;
    if(hdc)
        ReleaseDC(nullptr, hdc);
    return dpi;
}

// Returns false instead of truncating so a partial file is never written.
bool Append(wchar_t *buf, int cch, int &pos, const wchar_t *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    int n = _vsnwprintf_s(buf + pos, cch - pos, _TRUNCATE, fmt, args);
    va_end(args);
    if(n < 0)
        return false;
    pos += n;
    return true;
}
} // namespace

// The table is rebuilt per call so the pointers bind to this instance.
template <typename F> void Settings::ForEachField(F fn)
{
    // clang-format off
    Field fields[] = {
        {L"Font",           L"FontFace",          FieldType::String,     font.lfFaceName,                          LF_FACESIZE,       &fontLoaded,              nullptr},
        {L"Font",           L"FontSize",          FieldType::FontPoints, &font.lfHeight,                           11,                &fontLoaded,              nullptr},
        {L"Font",           L"FontWeight",        FieldType::Long,       &font.lfWeight,                           FW_NORMAL,         &fontLoaded,              nullptr},
        {L"Font",           L"FontItalic",        FieldType::Byte,       &font.lfItalic,                           0,                 nullptr,                  nullptr},
        {L"Font",           L"FontCharSet",       FieldType::Byte,       &font.lfCharSet,                          DEFAULT_CHARSET,   nullptr,                  nullptr},
        {L"Font",           L"FontQuality",       FieldType::Byte,       &font.lfQuality,                          CLEARTYPE_QUALITY, nullptr,                  nullptr},
        {L"Theme",          L"ThemeMode",         FieldType::Dword,      &themeMode,                               0,                 &themeModeLoaded,         nullptr},
        {L"Editor",         L"WordWrap",          FieldType::Bool,       &wordWrap,                                0,                 nullptr,                  nullptr},
        {L"Window",         L"WindowLeft",        FieldType::Long,       &windowPlacement.rcNormalPosition.left,   0,                 &windowPlacementLoaded,   nullptr},
        {L"Window",         L"WindowTop",         FieldType::Long,       &windowPlacement.rcNormalPosition.top,    0,                 &windowPlacementLoaded,   nullptr},
        {L"Window",         L"WindowRight",       FieldType::Long,       &windowPlacement.rcNormalPosition.right,  0,                 nullptr,                  nullptr},
        {L"Window",         L"WindowBottom",      FieldType::Long,       &windowPlacement.rcNormalPosition.bottom, 0,                 nullptr,                  nullptr},
        {L"Window",         L"WindowShowCmd",     FieldType::UInt,       &windowPlacement.showCmd,                 SW_SHOWNORMAL,     nullptr,                  nullptr},
        {L"NotepadReplace", L"OriginalDebugger",  FieldType::String,     originalDebugger,                         MAX_PATH,          &originalDebuggerLoaded,  &originalDebuggerLoaded},
        {L"NotepadReplace", L"OriginalUseFilter", FieldType::Dword,      &originalUseFilter,                       0,                 &originalUseFilterLoaded, &originalDebuggerLoaded},
    };
    // clang-format on

    for(Field &f : fields)
        fn(f);
}

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

    ForEachField(
        [&](Field &f)
        {
            if(wcscmp(key, f.key) != 0)
                return;

            switch(f.type)
            {
                case FieldType::Long:
                    *(LONG *)f.ptr = ParseInt(val, f.def);
                    break;
                case FieldType::UInt:
                    *(UINT *)f.ptr = (UINT)ParseInt(val, f.def);
                    break;
                case FieldType::Dword:
                    *(DWORD *)f.ptr = (DWORD)ParseInt(val, f.def);
                    break;
                case FieldType::Byte:
                    *(BYTE *)f.ptr = (BYTE)ParseInt(val, f.def);
                    break;
                case FieldType::Bool:
                    *(bool *)f.ptr = ParseInt(val, f.def) != 0;
                    break;
                case FieldType::String:
                    wcsncpy_s((wchar_t *)f.ptr, f.def, val, _TRUNCATE);
                    break;
                case FieldType::FontPoints:
                    *(LONG *)f.ptr = -MulDiv(ParseInt(val, f.def), GetScreenDpi(), 72);
                    break;
            }

            if(f.loaded)
                *f.loaded = true;
        });
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
    int pos                = 0;
    bool ok                = true;
    const wchar_t *section = nullptr;

    ForEachField(
        [&](Field &f)
        {
            if(!ok || (f.saveIf && !*f.saveIf))
                return;

            if(!section || wcscmp(section, f.section) != 0)
            {
                ok      = Append(buf, _countof(buf), pos, section ? L"\r\n[%s]\r\n" : L"[%s]\r\n", f.section);
                section = f.section;
            }

            switch(f.type)
            {
                case FieldType::Long:
                    ok = ok && Append(buf, _countof(buf), pos, L"%s=%ld\r\n", f.key, *(LONG *)f.ptr);
                    break;
                case FieldType::UInt:
                    ok = ok && Append(buf, _countof(buf), pos, L"%s=%u\r\n", f.key, *(UINT *)f.ptr);
                    break;
                case FieldType::Dword:
                    ok = ok && Append(buf, _countof(buf), pos, L"%s=%lu\r\n", f.key, *(DWORD *)f.ptr);
                    break;
                case FieldType::Byte:
                    ok = ok && Append(buf, _countof(buf), pos, L"%s=%d\r\n", f.key, *(BYTE *)f.ptr);
                    break;
                case FieldType::Bool:
                    ok = ok && Append(buf, _countof(buf), pos, L"%s=%d\r\n", f.key, *(bool *)f.ptr ? 1 : 0);
                    break;
                case FieldType::String:
                    ok = ok && Append(buf, _countof(buf), pos, L"%s=%s\r\n", f.key, (const wchar_t *)f.ptr);
                    break;
                case FieldType::FontPoints:
                {
                    // Stored as a point size for human readability
                    LONG height = *(LONG *)f.ptr;
                    int points  = MulDiv(height < 0 ? -height : height, 72, GetScreenDpi());
                    ok          = ok && Append(buf, _countof(buf), pos, L"%s=%d\r\n", f.key, points);
                    break;
                }
            }
        });

    if(!ok)
        return;

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
