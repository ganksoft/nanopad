#include "update.h"
#include "msgbox.h"
#include <winhttp.h>
#include <shellapi.h>
#include <cwchar>

#pragma comment(lib, "winhttp.lib")

static constexpr const wchar_t *GITHUB_API_HOST = L"api.github.com";
static constexpr const wchar_t *GITHUB_API_PATH = L"/repos/ganksoft/nanopad/releases/latest";
static constexpr const wchar_t *RELEASES_URL    = L"https://github.com/ganksoft/nanopad/releases/latest";

static constexpr const wchar_t *ABOUT_TEXT =
    L"Nanopad %s\n\n"
    L"A simple text editor in the spirit of classic Notepad.\n\n" SN_COPYRIGHT L"\n"
    L"github.com/ganksoft/nanopad\n\n"
    L"%s";

wchar_t UpdateChecker::s_newVersion[64]  = {};
wchar_t UpdateChecker::s_releaseUrl[512] = {};
bool UpdateChecker::s_updateAvailable    = false;

void UpdateChecker::CheckAsync(HWND hwnd)
{
    HANDLE hThread = CreateThread(nullptr, 0, CheckThread, (LPVOID)hwnd, 0, nullptr);
    if(hThread)
        CloseHandle(hThread);
}

// Simple JSON value extractor -- finds "key": "value" and returns the value.
// Handles escaped quotes. Bounded by jsonLen to prevent overreads.
static bool ExtractJsonString(const char *json, size_t jsonLen, const char *key, wchar_t *out, int outLen)
{
    size_t keyLen = strlen(key);
    if(keyLen > 120)
        return false;

    char pattern[128];
    int plen = sprintf_s(pattern, sizeof(pattern), "\"%s\"", key);
    if(plen <= 0)
        return false;

    const char *jsonEnd = json + jsonLen;
    const char *found   = nullptr;
    for(size_t i = 0; i + (size_t)plen < jsonLen; i++)
    {
        if(memcmp(json + i, pattern, plen) == 0)
        {
            found = json + i + plen;
            break;
        }
    }
    if(!found)
        return false;

    // Skip whitespace and colon (bounded)
    while(found < jsonEnd && (*found == ' ' || *found == ':' || *found == '\t'))
        found++;
    if(found >= jsonEnd || *found != '"')
        return false;
    found++;

    // Extract until closing quote, handling escaped quotes
    const char *end = found;
    while(end < jsonEnd && *end != '"')
    {
        if(*end == '\\' && end + 1 < jsonEnd)
            end++;
        end++;
    }
    if(end >= jsonEnd)
        return false;

    size_t len = (size_t)(end - found);
    if(len >= (size_t)outLen)
        return false;

    MultiByteToWideChar(CP_UTF8, 0, found, (int)len, out, outLen);
    out[len] = L'\0';
    return true;
}

static int CompareVersions(const wchar_t *a, const wchar_t *b)
{
    int aMajor = 0, aMinor = 0, aPatch = 0;
    int bMajor = 0, bMinor = 0, bPatch = 0;

    // Skip leading 'v' if present
    if(*a == L'v' || *a == L'V')
        a++;
    if(*b == L'v' || *b == L'V')
        b++;

    swscanf_s(a, L"%d.%d.%d", &aMajor, &aMinor, &aPatch);
    swscanf_s(b, L"%d.%d.%d", &bMajor, &bMinor, &bPatch);

    if(aMajor != bMajor)
        return aMajor - bMajor;
    if(aMinor != bMinor)
        return aMinor - bMinor;
    return aPatch - bPatch;
}

bool UpdateChecker::DoCheck()
{
    HINTERNET hSession = WinHttpOpen(L"Nanopad/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                                     WINHTTP_NO_PROXY_BYPASS, 0);
    if(!hSession)
        return false;

    WinHttpSetTimeouts(hSession, 5000, 5000, 10000, 10000);

    HINTERNET hConnect = WinHttpConnect(hSession, GITHUB_API_HOST, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if(!hConnect)
    {
        WinHttpCloseHandle(hSession);
        return false;
    }

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", GITHUB_API_PATH, nullptr, WINHTTP_NO_REFERER,
                                            WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if(!hRequest)
    {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    if(!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
       !WinHttpReceiveResponse(hRequest, nullptr))
    {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    DWORD statusCode = 0;
    DWORD statusSize = sizeof(statusCode);
    WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                        &statusCode, &statusSize, WINHTTP_NO_HEADER_INDEX);

    if(statusCode != 200)
    {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    char body[32768] = {};
    DWORD totalRead  = 0;
    DWORD bytesRead  = 0;
    while(totalRead < sizeof(body) - 1)
    {
        if(!WinHttpReadData(hRequest, body + totalRead, sizeof(body) - 1 - totalRead, &bytesRead) || bytesRead == 0)
            break;
        totalRead += bytesRead;
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    if(totalRead == 0)
        return false;

    wchar_t tagName[64]  = {};
    wchar_t htmlUrl[512] = {};
    if(!ExtractJsonString(body, totalRead, "tag_name", tagName, 64))
        return false;
    ExtractJsonString(body, totalRead, "html_url", htmlUrl, 512);

    if(CompareVersions(tagName, SN_VERSION_WSTR) > 0)
    {
        wcscpy_s(s_newVersion, tagName);
        wcscpy_s(s_releaseUrl, htmlUrl[0] ? htmlUrl : RELEASES_URL);
        s_updateAvailable = true;
        return true;
    }

    return false;
}

DWORD WINAPI UpdateChecker::CheckThread(LPVOID param)
{
    HWND hwnd = (HWND)param;
    if(DoCheck())
        PostMessage(hwnd, WM_APP_UPDATE_AVAILABLE, 0, 0);
    return 0;
}

void UpdateChecker::OpenReleasePage()
{
    const wchar_t *url = s_releaseUrl[0] ? s_releaseUrl : RELEASES_URL;
    ShellExecuteW(nullptr, L"open", url, nullptr, nullptr, SW_SHOWNORMAL);
}

void UpdateChecker::ShowAboutDialog(HWND hwnd)
{
    wchar_t msg[512];

    if(s_updateAvailable)
    {
        wchar_t updateLine[256];
        swprintf_s(updateLine,
                   L"\x2605 Version %s is available!\n\n"
                   L"Update with:  winget upgrade Ganksoft.Nanopad\n\n"
                   L"Or open the download page?",
                   s_newVersion);
        swprintf_s(msg, ABOUT_TEXT, SN_VERSION_WSTR, updateLine);

        int result = CenteredMessageBox(hwnd, msg, L"About Nanopad", MB_YESNO | MB_ICONINFORMATION);
        if(result == IDYES)
            OpenReleasePage();
    }
    else
    {
        swprintf_s(msg, ABOUT_TEXT, SN_VERSION_WSTR, L"You are running the latest version.");
        CenteredMessageBox(hwnd, msg, L"About Nanopad", MB_OK | MB_ICONINFORMATION);
    }
}
