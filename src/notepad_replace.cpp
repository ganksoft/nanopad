#include "notepad_replace.h"
#include "msgbox.h"
#include <shellapi.h>
#include <shlobj.h>
#include <string>
#include <cwchar>

bool NotepadReplace::IsElevated()
{
    BOOL elevated = FALSE;
    HANDLE hToken = nullptr;
    if(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken))
    {
        TOKEN_ELEVATION te = {};
        DWORD size         = sizeof(te);
        if(GetTokenInformation(hToken, TokenElevation, &te, sizeof(te), &size))
            elevated = te.TokenIsElevated;
        CloseHandle(hToken);
    }
    return elevated != FALSE;
}

bool NotepadReplace::GetExePath(wchar_t *buf, DWORD bufSize)
{
    return GetModuleFileNameW(nullptr, buf, bufSize) > 0;
}

bool NotepadReplace::RelaunchElevated(const wchar_t *args)
{
    wchar_t exePath[MAX_PATH];
    if(!GetExePath(exePath, MAX_PATH))
        return false;

    SHELLEXECUTEINFOW sei = {sizeof(sei)};
    sei.lpVerb            = L"runas";
    sei.lpFile            = exePath;
    sei.lpParameters      = args;
    sei.nShow             = SW_HIDE;
    sei.fMask             = SEE_MASK_NOCLOSEPROCESS;

    if(!ShellExecuteExW(&sei))
        return false;

    // Wait for the elevated process to finish
    if(sei.hProcess)
    {
        WaitForSingleObject(sei.hProcess, 10000);
        CloseHandle(sei.hProcess);
    }
    return true;
}

bool NotepadReplace::IsReplacing()
{
    HKEY hKey;
    if(RegOpenKeyExW(HKEY_LOCAL_MACHINE, IFEO_KEY, 0, KEY_READ, &hKey) != ERROR_SUCCESS)
        return false;

    // If UseFilter is enabled, IFEO Debugger is ignored
    DWORD useFilter = 0;
    DWORD ufSize    = sizeof(useFilter);
    RegQueryValueExW(hKey, L"UseFilter", nullptr, nullptr, (BYTE *)&useFilter, &ufSize);
    if(useFilter != 0)
    {
        RegCloseKey(hKey);
        return false;
    }

    wchar_t value[MAX_PATH] = {};
    DWORD size              = sizeof(value);
    DWORD type              = 0;
    bool result             = false;

    if(RegQueryValueExW(hKey, L"Debugger", nullptr, &type, (BYTE *)value, &size) == ERROR_SUCCESS && type == REG_SZ)
    {
        wchar_t exePath[MAX_PATH];
        if(GetExePath(exePath, MAX_PATH))
            result = (wcsstr(value, exePath) != nullptr);
    }

    RegCloseKey(hKey);
    return result;
}

bool NotepadReplace::Replace(HWND hwndOwner, Settings &settings, bool interactive)
{
    // No up-front confirmation: the user reached this through the explicit
    // "Replace Notepad" menu item, and the UAC prompt is the real gate.

    // The HKCU work and the one-time elevation request belong to the unelevated,
    // interactive process -- only it is guaranteed to target the logged-in user's
    // own registry hive. The elevated worker (relaunched with /register) does the
    // HKLM work below and nothing else: the user already confirmed, so it must
    // not prompt again.
    if(!IsElevated())
    {
        // Disable Store Notepad's App Paths redirect (HKCU, no admin needed)
        // Back up the current value and rename the key to disable it
        {
            static constexpr const wchar_t *APP_PATHS_KEY =
                L"Software\\Microsoft\\Windows\\CurrentVersion\\App Paths\\notepad.exe";
            static constexpr const wchar_t *APP_PATHS_BAK =
                L"Software\\Microsoft\\Windows\\CurrentVersion\\App Paths\\notepad.exe.nanopad-backup";

            HKEY hSrc;
            if(RegOpenKeyExW(HKEY_CURRENT_USER, APP_PATHS_KEY, 0, KEY_READ, &hSrc) == ERROR_SUCCESS)
            {
                // Read current default value
                wchar_t appPath[MAX_PATH] = {};
                DWORD size                = sizeof(appPath);
                RegQueryValueExW(hSrc, nullptr, nullptr, nullptr, (BYTE *)appPath, &size);
                RegCloseKey(hSrc);

                // Save to backup key
                HKEY hBak;
                if(RegCreateKeyExW(HKEY_CURRENT_USER, APP_PATHS_BAK, 0, nullptr, 0, KEY_WRITE, nullptr, &hBak,
                                   nullptr) == ERROR_SUCCESS)
                {
                    RegSetValueExW(hBak, nullptr, 0, REG_SZ, (const BYTE *)appPath,
                                   (DWORD)((wcslen(appPath) + 1) * sizeof(wchar_t)));
                    RegCloseKey(hBak);
                }

                // Delete the original key to disable Store Notepad
                RegDeleteTreeW(HKEY_CURRENT_USER, APP_PATHS_KEY);
            }
        }

        if(!RelaunchElevated(L"/register"))
        {
            // Declining the UAC prompt is a deliberate choice, not an error --
            // stay silent. Only report a genuine failure to elevate.
            if(interactive && GetLastError() != ERROR_CANCELLED)
                CenteredMessageBox(hwndOwner, L"Failed to obtain administrator privileges.", L"Nanopad",
                                   MB_OK | MB_ICONERROR);
            return false;
        }

        // The elevated worker saved the original IFEO state to nanopad.ini.
        // Reload it so this process won't overwrite the backup when it exits.
        settings.Load();
        return IsReplacing();
    }

    // Elevated worker -- do the privileged registry work.
    wchar_t exePath[MAX_PATH];
    if(!GetExePath(exePath, MAX_PATH))
        return false;

    // Read existing Debugger value (may belong to another Notepad replacement)
    HKEY hKey;
    if(RegCreateKeyExW(HKEY_LOCAL_MACHINE, IFEO_KEY, 0, nullptr, 0, KEY_READ | KEY_WRITE, nullptr, &hKey, nullptr) !=
       ERROR_SUCCESS)
        return false;

    wchar_t oldValue[MAX_PATH] = {};
    DWORD size                 = sizeof(oldValue);
    DWORD type                 = 0;
    RegQueryValueExW(hKey, L"Debugger", nullptr, &type, (BYTE *)oldValue, &size);

    // Save original UseFilter value
    DWORD oldUseFilter = 0;
    size               = sizeof(oldUseFilter);
    RegQueryValueExW(hKey, L"UseFilter", nullptr, nullptr, (BYTE *)&oldUseFilter, &size);

    // Save originals to settings
    wcscpy_s(settings.originalDebugger, oldValue);
    settings.originalDebuggerLoaded  = true;
    settings.originalUseFilter       = oldUseFilter;
    settings.originalUseFilterLoaded = true;
    settings.Save();

    // Write our exe as the Debugger and disable UseFilter
    wchar_t newValue[MAX_PATH + 4];
    swprintf_s(newValue, _countof(newValue), L"\"%s\"", exePath);
    DWORD newSize = (DWORD)((wcslen(newValue) + 1) * sizeof(wchar_t));
    RegSetValueExW(hKey, L"Debugger", 0, REG_SZ, (const BYTE *)newValue, newSize);

    DWORD useFilter = 0;
    RegSetValueExW(hKey, L"UseFilter", 0, REG_DWORD, (const BYTE *)&useFilter, sizeof(useFilter));

    RegCloseKey(hKey);

    return true;
}

bool NotepadReplace::Restore(HWND hwndOwner, Settings &settings, bool interactive)
{
    // No up-front confirmation: the user toggled the "Replace Notepad" menu item
    // off deliberately, and the UAC prompt is the real gate.

    // As with Replace, the HKCU work and elevation request belong to the
    // unelevated process; the elevated worker only restores the HKLM state.
    if(!IsElevated())
    {
        // Restore Store Notepad's App Paths redirect if we backed it up
        {
            static constexpr const wchar_t *APP_PATHS_KEY =
                L"Software\\Microsoft\\Windows\\CurrentVersion\\App Paths\\notepad.exe";
            static constexpr const wchar_t *APP_PATHS_BAK =
                L"Software\\Microsoft\\Windows\\CurrentVersion\\App Paths\\notepad.exe.nanopad-backup";

            HKEY hBak;
            if(RegOpenKeyExW(HKEY_CURRENT_USER, APP_PATHS_BAK, 0, KEY_READ, &hBak) == ERROR_SUCCESS)
            {
                wchar_t appPath[MAX_PATH] = {};
                DWORD size                = sizeof(appPath);
                RegQueryValueExW(hBak, nullptr, nullptr, nullptr, (BYTE *)appPath, &size);
                RegCloseKey(hBak);

                if(appPath[0])
                {
                    HKEY hDst;
                    if(RegCreateKeyExW(HKEY_CURRENT_USER, APP_PATHS_KEY, 0, nullptr, 0, KEY_WRITE, nullptr, &hDst,
                                       nullptr) == ERROR_SUCCESS)
                    {
                        RegSetValueExW(hDst, nullptr, 0, REG_SZ, (const BYTE *)appPath,
                                       (DWORD)((wcslen(appPath) + 1) * sizeof(wchar_t)));
                        RegCloseKey(hDst);
                    }
                }

                RegDeleteTreeW(HKEY_CURRENT_USER, APP_PATHS_BAK);
            }
        }

        if(!RelaunchElevated(L"/unregister"))
        {
            // Declining the UAC prompt is a deliberate choice, not an error.
            if(interactive && GetLastError() != ERROR_CANCELLED)
                CenteredMessageBox(hwndOwner, L"Failed to obtain administrator privileges.", L"Nanopad",
                                   MB_OK | MB_ICONERROR);
            return false;
        }

        // The elevated worker cleared the saved original IFEO state. Mirror that
        // here so this process won't rewrite the stale backup when it exits.
        settings.originalDebugger[0]     = L'\0';
        settings.originalDebuggerLoaded  = false;
        settings.originalUseFilterLoaded = false;
        settings.Save();
        return !IsReplacing();
    }

    // Elevated worker -- restore registry
    HKEY hKey;
    if(RegOpenKeyExW(HKEY_LOCAL_MACHINE, IFEO_KEY, 0, KEY_WRITE, &hKey) != ERROR_SUCCESS)
        return false;

    if(settings.originalDebuggerLoaded && settings.originalDebugger[0])
    {
        DWORD size = (DWORD)((wcslen(settings.originalDebugger) + 1) * sizeof(wchar_t));
        RegSetValueExW(hKey, L"Debugger", 0, REG_SZ, (const BYTE *)settings.originalDebugger, size);
    }
    else
    {
        RegDeleteValueW(hKey, L"Debugger");
    }

    // Restore UseFilter
    if(settings.originalUseFilterLoaded)
    {
        RegSetValueExW(hKey, L"UseFilter", 0, REG_DWORD, (const BYTE *)&settings.originalUseFilter,
                       sizeof(settings.originalUseFilter));
    }

    RegCloseKey(hKey);

    // Clear saved original
    settings.originalDebugger[0]    = L'\0';
    settings.originalDebuggerLoaded = false;
    settings.Save();

    return true;
}

void NotepadReplace::StripNotepadFromCmdLine(std::wstring &cmdLine)
{
    // When launched via IFEO Debugger, pCmdLine contains everything after our exe:
    //   C:\Windows\notepad.exe new.txt
    //   "C:\Windows\notepad.exe" new.txt
    //   C:\Windows\notepad.exe
    // We need to find and remove the notepad.exe token (possibly with a full path).

    if(cmdLine.empty())
        return;

    std::wstring lower = cmdLine;
    CharLowerBuffW(lower.data(), (DWORD)lower.size());

    // Look for notepad.exe or just notepad (IFEO may pass either)
    size_t notepadPos = lower.find(L"notepad.exe");
    size_t notepadLen = wcslen(L"notepad.exe");
    if(notepadPos == std::wstring::npos)
    {
        // Try bare "notepad" -- but only as a whole token, not inside another word
        notepadPos = lower.find(L"notepad");
        notepadLen = wcslen(L"notepad");
        if(notepadPos != std::wstring::npos)
        {
            size_t afterPos = notepadPos + notepadLen;
            // Verify it's a token boundary (space, quote, or end of string)
            if(afterPos < lower.size() && lower[afterPos] != L' ' && lower[afterPos] != L'"' &&
               lower[afterPos] != L'\0')
                return; // "notepad" is part of a longer word, don't strip
        }
    }
    if(notepadPos == std::wstring::npos)
        return;

    // Find the start of this token -- walk back to the beginning or a space/quote boundary
    size_t tokenStart = 0;
    bool quoted       = false;

    // Check if the notepad path is quoted
    if(notepadPos > 0 && cmdLine[notepadPos - 1] != L' ' && cmdLine[notepadPos - 1] != L'"')
    {
        // Part of a full path like C:\Windows\notepad.exe -- find the token start
        size_t s = notepadPos;
        while(s > 0 && cmdLine[s - 1] != L' ' && cmdLine[s - 1] != L'"')
            s--;
        if(s > 0 && cmdLine[s - 1] == L'"')
        {
            tokenStart = s - 1;
            quoted     = true;
        }
        else
        {
            tokenStart = s;
        }
    }
    else if(notepadPos > 0 && cmdLine[notepadPos - 1] == L'"')
    {
        // "notepad.exe" -- quoted, find opening quote
        size_t s = notepadPos - 1;
        while(s > 0 && cmdLine[s - 1] != L'"')
            s--;
        // s might be 0 or pointing after the opening quote
        if(s > 0)
            s--;
        tokenStart = s;
        quoted     = true;
    }
    else
    {
        tokenStart = notepadPos;
    }

    // Find the end of the notepad.exe token
    size_t tokenEnd = notepadPos + notepadLen;
    if(quoted && tokenEnd < cmdLine.size() && cmdLine[tokenEnd] == L'"')
        tokenEnd++;

    // Skip whitespace after the token
    while(tokenEnd < cmdLine.size() && cmdLine[tokenEnd] == L' ')
        tokenEnd++;

    // Keep everything after the notepad token
    cmdLine = cmdLine.substr(tokenEnd);
}

// --- Open With registration (HKCU, no admin) ---

static bool SetRegString(HKEY hParent, const wchar_t *subKey, const wchar_t *valueName, const wchar_t *data)
{
    HKEY hKey;
    if(RegCreateKeyExW(hParent, subKey, 0, nullptr, 0, KEY_WRITE, nullptr, &hKey, nullptr) != ERROR_SUCCESS)
        return false;
    DWORD size = (DWORD)((wcslen(data) + 1) * sizeof(wchar_t));
    RegSetValueExW(hKey, valueName, 0, REG_SZ, (const BYTE *)data, size);
    RegCloseKey(hKey);
    return true;
}

static void DeleteRegTree(HKEY hParent, const wchar_t *subKey)
{
    RegDeleteTreeW(hParent, subKey);
}

bool NotepadReplace::RegisterOpenWith()
{
    wchar_t exePath[MAX_PATH];
    if(!GetExePath(exePath, MAX_PATH))
        return false;

    // Build command: "C:\path\nanopad.exe" "%1"
    wchar_t command[MAX_PATH + 16];
    swprintf_s(command, L"\"%s\" \"%%1\"", exePath);

    // Register ProgId: HKCU\Software\Classes\Nanopad.TextFile\shell\open\command
    wchar_t progIdCmd[256];
    swprintf_s(progIdCmd, L"Software\\Classes\\%s\\shell\\open\\command", PROGID);
    SetRegString(HKEY_CURRENT_USER, progIdCmd, nullptr, command);

    // Set friendly name
    wchar_t progIdKey[256];
    swprintf_s(progIdKey, L"Software\\Classes\\%s", PROGID);
    SetRegString(HKEY_CURRENT_USER, progIdKey, nullptr, L"Nanopad Text File");

    // Set icon
    wchar_t iconKey[256];
    swprintf_s(iconKey, L"Software\\Classes\\%s\\DefaultIcon", PROGID);
    wchar_t iconPath[MAX_PATH + 4];
    swprintf_s(iconPath, L"\"%s\",0", exePath);
    SetRegString(HKEY_CURRENT_USER, iconKey, nullptr, iconPath);

    // Register app: HKCU\Software\Classes\Applications\nanopad.exe\shell\open\command
    SetRegString(HKEY_CURRENT_USER, L"Software\\Classes\\Applications\\nanopad.exe\\shell\\open\\command", nullptr,
                 command);

    // Set friendly app name (shown in "Open with" instead of "nanopad.exe")
    SetRegString(HKEY_CURRENT_USER, L"Software\\Classes\\Applications\\nanopad.exe", L"FriendlyAppName", L"Nanopad");

    // For each text extension, add to OpenWithProgids
    for(const wchar_t *ext : TEXT_EXTENSIONS)
    {
        wchar_t extKey[128];
        swprintf_s(extKey, L"Software\\Classes\\%s\\OpenWithProgids", ext);

        HKEY hKey;
        if(RegCreateKeyExW(HKEY_CURRENT_USER, extKey, 0, nullptr, 0, KEY_WRITE, nullptr, &hKey, nullptr) ==
           ERROR_SUCCESS)
        {
            // Empty REG_NONE value with the ProgId as the name
            RegSetValueExW(hKey, PROGID, 0, REG_NONE, nullptr, 0);
            RegCloseKey(hKey);
        }
    }

    // Notify the shell
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return true;
}

bool NotepadReplace::UnregisterOpenWith()
{
    // Remove ProgId
    wchar_t progIdKey[256];
    swprintf_s(progIdKey, L"Software\\Classes\\%s", PROGID);
    DeleteRegTree(HKEY_CURRENT_USER, progIdKey);

    // Remove app registration
    DeleteRegTree(HKEY_CURRENT_USER, L"Software\\Classes\\Applications\\nanopad.exe");

    // Remove from each extension's OpenWithProgids
    for(const wchar_t *ext : TEXT_EXTENSIONS)
    {
        wchar_t extKey[128];
        swprintf_s(extKey, L"Software\\Classes\\%s\\OpenWithProgids", ext);

        HKEY hKey;
        if(RegOpenKeyExW(HKEY_CURRENT_USER, extKey, 0, KEY_WRITE, &hKey) == ERROR_SUCCESS)
        {
            RegDeleteValueW(hKey, PROGID);
            RegCloseKey(hKey);
        }
    }

    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return true;
}

bool NotepadReplace::IsOpenWithRegistered()
{
    wchar_t progIdKey[256];
    swprintf_s(progIdKey, L"Software\\Classes\\%s", PROGID);

    HKEY hKey;
    if(RegOpenKeyExW(HKEY_CURRENT_USER, progIdKey, 0, KEY_READ, &hKey) != ERROR_SUCCESS)
        return false;
    RegCloseKey(hKey);
    return true;
}

// --- Context menu registration (HKCU, no admin) ---

// Classic context menu: HKCU\Software\Classes\*\shell\NanopadEdit
// Appears in "Show more options" on Win11, directly on Win10.
static constexpr const wchar_t *CTX_SHELL_KEY = L"Software\\Classes\\*\\shell\\NanopadEdit";
static constexpr const wchar_t *CTX_CMD_KEY   = L"Software\\Classes\\*\\shell\\NanopadEdit\\command";

bool NotepadReplace::RegisterContextMenu()
{
    wchar_t exePath[MAX_PATH];
    if(!GetExePath(exePath, MAX_PATH))
        return false;

    wchar_t command[MAX_PATH + 16];
    swprintf_s(command, _countof(command), L"\"%s\" \"%%1\"", exePath);

    // Create the shell verb
    SetRegString(HKEY_CURRENT_USER, CTX_SHELL_KEY, nullptr, L"Edit in Nanopad");

    // Set icon
    wchar_t iconPath[MAX_PATH + 4];
    swprintf_s(iconPath, _countof(iconPath), L"\"%s\",0", exePath);
    SetRegString(HKEY_CURRENT_USER, CTX_SHELL_KEY, L"Icon", iconPath);

    // Set command
    SetRegString(HKEY_CURRENT_USER, CTX_CMD_KEY, nullptr, command);

    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return true;
}

bool NotepadReplace::UnregisterContextMenu()
{
    DeleteRegTree(HKEY_CURRENT_USER, CTX_SHELL_KEY);
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return true;
}

bool NotepadReplace::IsContextMenuRegistered()
{
    HKEY hKey;
    if(RegOpenKeyExW(HKEY_CURRENT_USER, CTX_SHELL_KEY, 0, KEY_READ, &hKey) != ERROR_SUCCESS)
        return false;
    RegCloseKey(hKey);
    return true;
}
