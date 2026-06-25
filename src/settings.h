#pragma once
#include <windows.h>

// Centralized application settings stored in nanopad.ini next to the exe.
// Smart defaults if the file doesn't exist.
class Settings
{
  public:
    // Font
    LOGFONTW font   = {};
    bool fontLoaded = false;

    // Theme: 0=System, 1=Light, 2=Dark
    DWORD themeMode      = 0;
    bool themeModeLoaded = false;

    // Window placement
    WINDOWPLACEMENT windowPlacement = {};
    bool windowPlacementLoaded      = false;

    // Notepad replacement: stores original IFEO Debugger value for restore
    wchar_t originalDebugger[MAX_PATH] = {};
    bool originalDebuggerLoaded        = false;
    DWORD originalUseFilter            = 0;
    bool originalUseFilterLoaded       = false;

    void Load();
    void Save();

    void SaveFont()
    {
        Save();
    }
    void SaveThemeMode()
    {
        Save();
    }
    void SaveWindowPlacement()
    {
        Save();
    }

  private:
    void ParseLine(const wchar_t *line);
    static int ParseInt(const wchar_t *value, int def);
};
