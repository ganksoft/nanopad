#pragma once
#include <windows.h>
#include <string>
#include "settings.h"

class NotepadReplace
{
  public:
    // Check if Nanopad is currently the registered Notepad replacement
    static bool IsReplacing();

    // Register as Notepad replacement (requires admin). interactive=false runs
    // silently with no prompts -- used by the elevated /register relaunch.
    static bool Replace(HWND hwndOwner, Settings &settings, bool interactive = true);

    // Restore original Notepad (requires admin). interactive=false runs silently
    // with no prompts -- used by the elevated /unregister relaunch.
    static bool Restore(HWND hwndOwner, Settings &settings, bool interactive = true);

    // Register/unregister in "Open with" for common text file extensions (HKCU, no admin)
    static bool RegisterOpenWith();
    static bool UnregisterOpenWith();
    static bool IsOpenWithRegistered();

    // Register/unregister "Edit in Nanopad" context menu (HKCU, no admin)
    static bool RegisterContextMenu();
    static bool UnregisterContextMenu();
    static bool IsContextMenuRegistered();

    // Strip "notepad.exe" from the front of the command line if we were
    // launched via IFEO (the Debugger value prepends us to the original cmd)
    static void StripNotepadFromCmdLine(std::wstring &cmdLine);

  private:
    static constexpr const wchar_t *IFEO_KEY =
        L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Image File Execution Options\\notepad.exe";

    static constexpr const wchar_t *PROGID = L"Nanopad.TextFile";

    static constexpr const wchar_t *TEXT_EXTENSIONS[] = {L".inf",  L".ini", L".log", L".ps1", L".psd1",
                                                         L".psm1", L".scp", L".txt", L".wtx"};

    static bool IsElevated();
    static bool RelaunchElevated(const wchar_t *args);
    static bool GetExePath(wchar_t *buf, DWORD bufSize);
};
