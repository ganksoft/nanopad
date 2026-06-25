// Nanopad - A simple Win32 text editor
// Entry point and main window procedure

#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <string>

#include "resource.h"
#include "editor.h"
#include "file_io.h"
#include "theme.h"
#include "find.h"
#include "font.h"
#include "statusbar.h"
#include "print.h"
#include "settings.h"
#include "notepad_replace.h"
#include "update.h"
#include "recovery.h"
#include "version.h"
#include "msgbox.h"

// Globals
static HINSTANCE g_hInst;
static HWND g_hwndMain;
static Editor g_editor;
static Theme g_theme;
static FindReplace g_findReplace;
static FontManager g_fontManager;
static StatusBar g_statusBar;
static Settings g_settings;
static FileInfo g_fileInfo;

static constexpr const wchar_t *APP_NAME     = SN_APP_NAME;
static constexpr const wchar_t *WINDOW_CLASS = L"NanopadMainWindow";

static constexpr UINT_PTR TIMER_STATUSBAR = 1;
static constexpr UINT TIMER_STATUSBAR_MS  = 100;
static constexpr UINT_PTR TIMER_FILEWATCH = 2;
static constexpr UINT TIMER_FILEWATCH_MS  = 250;
static constexpr UINT_PTR TIMER_RECOVERY  = 3;
static constexpr UINT TIMER_RECOVERY_MS   = 10000;
static constexpr UINT WM_DEFERRED_THEME   = WM_APP + 1;
static int g_currentDpi                   = 96;
static HANDLE g_hFileWatch                = INVALID_HANDLE_VALUE;

// Recovery launch state, set from the command line before the window is created.
// g_recoveryRelaunch: this instance should restore an orphaned session into its
//   own window (Windows relaunch via /restored, or a /recover child we spawned).
// g_recoverySpawn: this instance exists only to recover (/recover); if there is
//   nothing left to claim it should exit quietly instead of showing a window.
// g_recoveryClaimed: set once a session has been restored into this window.
static bool g_recoveryRelaunch = false;
static bool g_recoverySpawn    = false;
static bool g_recoveryClaimed  = false;

struct FileStamp
{
    bool exists        = false;
    FILETIME lastWrite = {};
    ULONGLONG fileSize = 0;
};

static FileStamp g_loadedFileStamp;
static FileStamp g_lastObservedFileStamp;

// Forward declarations
static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
static void UpdateTitle();
static void UpdateStatusBar();
static void StopWatchingCurrentFile();
static void StartWatchingCurrentFile();
static bool LoadFileIntoEditor(const std::wstring &path);
static void HandleWatchedFileChange();
static bool PromptSave();
static void DoNew();
static void DoOpen(const std::wstring &path = L"");
static void DoSave();
static void DoSaveAs();
static void SaveWindowPlacement(HWND hwnd);

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR pCmdLine, int nCmdShow)
{
    g_hInst = hInstance;

    INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_BAR_CLASSES};
    InitCommonControlsEx(&icc);

    Theme::EnableDarkModeForApp();

    // Load all settings from INI file
    g_settings.Load();
    if(g_settings.fontLoaded)
        g_fontManager.LoadFromSettings(g_settings.font);
    if(g_settings.themeModeLoaded)
        g_theme.LoadFromSettings(g_settings.themeMode);

    // Handle elevated /register and /unregister (silent, no window)
    if(pCmdLine)
    {
        std::wstring cmd = pCmdLine;
        // Trim whitespace
        while(!cmd.empty() && cmd.back() == L' ')
            cmd.pop_back();
        while(!cmd.empty() && cmd.front() == L' ')
            cmd.erase(cmd.begin());

        if(cmd == L"/register")
        {
            NotepadReplace::Replace(nullptr, g_settings, false);
            return 0;
        }
        if(cmd == L"/unregister")
        {
            NotepadReplace::Restore(nullptr, g_settings, false);
            return 0;
        }
        if(cmd == L"/recover")
        {
            // Spawned by another instance solely to restore the next orphaned
            // session into its own window.
            g_recoveryRelaunch = true;
            g_recoverySpawn    = true;
        }
    }

    // A Windows-initiated restart relaunches us with /restored; treat it as a
    // recovery relaunch so it restores a session into this window.
    if(Recovery::LaunchedByRestart(pCmdLine))
        g_recoveryRelaunch = true;

    WNDCLASSEXW wc   = {};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.hIcon         = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_NANOPAD));
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszMenuName  = MAKEINTRESOURCE(IDR_MAINMENU);
    wc.lpszClassName = WINDOW_CLASS;
    wc.hIconSm       = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_NANOPAD));
    RegisterClassExW(&wc);

    // Ask Windows to relaunch us after an update reboot, crash, or hang so we
    // can offer to recover unsaved text.
    Recovery::RegisterForRestart();

    g_hwndMain = CreateWindowExW(WS_EX_ACCEPTFILES, WINDOW_CLASS, APP_NAME, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
                                 CW_USEDEFAULT, 1200, 850, nullptr, nullptr, hInstance, nullptr);

    if(!g_hwndMain)
        return 1;

    // A dedicated /recover spawn that found nothing left to claim (it lost a
    // race to another instance) should exit quietly without showing a window.
    if(g_recoverySpawn && !g_recoveryClaimed)
    {
        DestroyWindow(g_hwndMain);
        return 0;
    }

    // Restore saved window position
    if(g_settings.windowPlacementLoaded)
    {
        g_settings.windowPlacement.showCmd = nCmdShow;
        SetWindowPlacement(g_hwndMain, &g_settings.windowPlacement);
    }
    else
    {
        ShowWindow(g_hwndMain, nCmdShow);
    }

    UpdateWindow(g_hwndMain);

    // Open file from command line. A Windows-initiated restart (/restored) and a
    // recovery spawn (/recover) carry our own switches, not a file path, so skip
    // command-line handling for them.
    if(pCmdLine && pCmdLine[0] && !g_recoveryRelaunch && !Recovery::LaunchedByRestart(pCmdLine))
    {
        std::wstring path = pCmdLine;

        // If launched via IFEO, strip the "notepad.exe" from the front
        NotepadReplace::StripNotepadFromCmdLine(path);

        // Trim whitespace
        while(!path.empty() && (path.front() == L' ' || path.front() == L'\t'))
            path.erase(path.begin());
        while(!path.empty() && (path.back() == L' ' || path.back() == L'\t'))
            path.pop_back();

        // Strip surrounding quotes
        if(path.size() >= 2 && path.front() == L'"' && path.back() == L'"')
            path = path.substr(1, path.size() - 2);

        if(!path.empty())
            DoOpen(path);
    }

    HACCEL hAccel = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDR_ACCELERATOR));

    MSG msg;
    while(GetMessage(&msg, nullptr, 0, 0))
    {
        if(g_findReplace.GetDialog() && IsDialogMessage(g_findReplace.GetDialog(), &msg))
            continue;

        if(!TranslateAccelerator(g_hwndMain, hAccel, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    return (int)msg.wParam;
}

void UpdateTitle()
{
    wchar_t title[MAX_PATH + 32];
    wchar_t *p         = title;
    const wchar_t *end = title + _countof(title);

    if(g_editor.IsDirty())
        *p++ = L'*';

    if(g_fileInfo.filePath.empty())
    {
        wcscpy_s(p, end - p, L"Untitled");
        p += 8;
    }
    else
    {
        const wchar_t *filePath = g_fileInfo.filePath.c_str();
        size_t pathLen          = g_fileInfo.filePath.size();
        const wchar_t *name     = filePath;
        for(size_t i = 0; i < pathLen; i++)
            if(filePath[i] == L'\\' || filePath[i] == L'/')
                name = filePath + i + 1;
        size_t nameLen = wcslen(name);
        if(nameLen > (size_t)(end - p - 20))
            nameLen = (size_t)(end - p - 20);
        wmemcpy(p, name, nameLen);
        p += nameLen;
    }

    wcscpy_s(p, end - p, L" - ");
    p += 3;
    wcscpy_s(p, end - p, APP_NAME);
    SetWindowTextW(g_hwndMain, title);
}

static int g_lastLine = -1, g_lastCol = -1, g_lastCharCount = -1, g_lastLineCount = -1;

void UpdateStatusBar()
{
    int line, col;
    g_editor.GetCaretPos(line, col);
    int charCount = g_editor.GetCharCount();
    int lineCount = g_editor.GetLineCount();

    if(line == g_lastLine && col == g_lastCol && charCount == g_lastCharCount && lineCount == g_lastLineCount)
        return;

    g_lastLine      = line;
    g_lastCol       = col;
    g_lastCharCount = charCount;
    g_lastLineCount = lineCount;

    g_statusBar.Update(line, col, charCount, lineCount, g_fileInfo.encoding, g_fileInfo.lineEnding);
}

static bool AreFileStampsEqual(const FileStamp &lhs, const FileStamp &rhs)
{
    if(lhs.exists != rhs.exists)
        return false;
    if(!lhs.exists)
        return true;
    return CompareFileTime(&lhs.lastWrite, &rhs.lastWrite) == 0 && lhs.fileSize == rhs.fileSize;
}

static bool QueryFileStamp(const std::wstring &path, FileStamp &outStamp)
{
    outStamp = {};
    if(path.empty())
        return false;

    WIN32_FILE_ATTRIBUTE_DATA attribs;
    if(!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &attribs))
        return false;

    ULARGE_INTEGER size;
    size.LowPart  = attribs.nFileSizeLow;
    size.HighPart = attribs.nFileSizeHigh;

    outStamp.exists    = true;
    outStamp.lastWrite = attribs.ftLastWriteTime;
    outStamp.fileSize  = size.QuadPart;
    return true;
}

static std::wstring GetDirectoryPath(const std::wstring &path)
{
    size_t pos = path.find_last_of(L"\\/");
    if(pos == std::wstring::npos)
        return L".";
    if(pos == 0)
        return path.substr(0, 1);
    if(pos == 2 && path[1] == L':')
        return path.substr(0, 3);
    return path.substr(0, pos);
}

static void StopWatchingCurrentFile()
{
    if(g_hFileWatch != INVALID_HANDLE_VALUE)
    {
        FindCloseChangeNotification(g_hFileWatch);
        g_hFileWatch = INVALID_HANDLE_VALUE;
    }

    g_loadedFileStamp       = {};
    g_lastObservedFileStamp = {};
}

static void StartWatchingCurrentFile()
{
    StopWatchingCurrentFile();

    if(g_fileInfo.filePath.empty())
        return;

    QueryFileStamp(g_fileInfo.filePath, g_loadedFileStamp);
    g_lastObservedFileStamp = g_loadedFileStamp;

    std::wstring directoryPath = GetDirectoryPath(g_fileInfo.filePath);
    g_hFileWatch = FindFirstChangeNotificationW(directoryPath.c_str(), FALSE,
                                                FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE |
                                                    FILE_NOTIFY_CHANGE_SIZE);
    if(g_hFileWatch == INVALID_HANDLE_VALUE)
    {
        g_loadedFileStamp       = {};
        g_lastObservedFileStamp = {};
    }
}

static bool LoadFileIntoEditor(const std::wstring &path)
{
    std::wstring text;
    FileInfo info;
    if(!FileIO::ReadFile(path, text, info))
        return false;

    g_fileInfo = info;
    g_editor.SetText(text.c_str());
    g_editor.SetDirty(false);
    UpdateTitle();
    UpdateStatusBar();
    StartWatchingCurrentFile();
    return true;
}

static void HandleWatchedFileChange()
{
    if(g_hFileWatch == INVALID_HANDLE_VALUE || g_fileInfo.filePath.empty())
        return;

    if(WaitForSingleObject(g_hFileWatch, 0) != WAIT_OBJECT_0)
        return;

    FileStamp currentStamp;
    QueryFileStamp(g_fileInfo.filePath, currentStamp);

    if(!FindNextChangeNotification(g_hFileWatch))
    {
        StartWatchingCurrentFile();
        return;
    }

    if(AreFileStampsEqual(currentStamp, g_lastObservedFileStamp))
        return;

    g_lastObservedFileStamp = currentStamp;
    if(!currentStamp.exists)
        return;

    if(g_editor.IsDirty())
    {
        int result = CenteredMessageBox(g_hwndMain,
                                        L"This file was modified outside Nanopad.\n\n"
                                        L"Reload it and discard your unsaved changes?",
                                        APP_NAME, MB_YESNO | MB_ICONQUESTION);
        if(result != IDYES)
            return;
    }

    if(!LoadFileIntoEditor(g_fileInfo.filePath))
        CenteredMessageBox(g_hwndMain, L"Failed to reload the file after it changed on disk.", APP_NAME,
                           MB_OK | MB_ICONERROR);
}

bool PromptSave()
{
    if(!g_editor.IsDirty())
        return true;

    std::wstring msg = L"Do you want to save changes";
    if(!g_fileInfo.filePath.empty())
    {
        msg += L" to ";
        size_t pos = g_fileInfo.filePath.find_last_of(L"\\/");
        msg += (pos != std::wstring::npos) ? g_fileInfo.filePath.substr(pos + 1) : g_fileInfo.filePath;
    }
    msg += L"?";

    int result = CenteredMessageBox(g_hwndMain, msg.c_str(), APP_NAME, MB_YESNOCANCEL | MB_ICONWARNING);
    if(result == IDCANCEL)
        return false;
    if(result == IDYES)
    {
        DoSave();
        return !g_editor.IsDirty();
    }
    return true;
}

void DoNew()
{
    if(!PromptSave())
        return;
    StopWatchingCurrentFile();
    g_editor.SetText(L"");
    g_editor.SetDirty(false);
    g_fileInfo            = {};
    g_fileInfo.encoding   = Encoding::UTF8;
    g_fileInfo.lineEnding = LineEnding::CRLF;
    UpdateTitle();
    UpdateStatusBar();
}

void DoOpen(const std::wstring &path)
{
    if(!PromptSave())
        return;

    std::wstring filePath = path;
    if(filePath.empty())
    {
        if(!FileIO::ShowOpenDialog(g_hwndMain, filePath))
            return;
    }

    // If file doesn't exist, ask to create it
    DWORD attribs = GetFileAttributesW(filePath.c_str());
    if(attribs == INVALID_FILE_ATTRIBUTES)
    {
        std::wstring msg = filePath + L"\n\nThis file does not exist.\n\nDo you want to create it?";
        int result       = CenteredMessageBox(g_hwndMain, msg.c_str(), APP_NAME, MB_YESNO | MB_ICONQUESTION);
        if(result != IDYES)
            return;

        // Create empty file and set up as new document
        if(!FileIO::WriteFile(filePath, L"", Encoding::UTF8, LineEnding::CRLF))
        {
            CenteredMessageBox(g_hwndMain, L"Failed to create file.", APP_NAME, MB_OK | MB_ICONERROR);
            return;
        }

        g_editor.SetText(L"");
        g_editor.SetDirty(false);
        g_fileInfo            = {};
        g_fileInfo.filePath   = filePath;
        g_fileInfo.encoding   = Encoding::UTF8;
        g_fileInfo.lineEnding = LineEnding::CRLF;
        UpdateTitle();
        UpdateStatusBar();
        StartWatchingCurrentFile();
        return;
    }

    if(!LoadFileIntoEditor(filePath))
        CenteredMessageBox(g_hwndMain, L"Failed to open file.", APP_NAME, MB_OK | MB_ICONERROR);
}

void DoSave()
{
    if(g_fileInfo.filePath.empty())
    {
        DoSaveAs();
        return;
    }

    std::wstring text = g_editor.GetText();
    if(!FileIO::WriteFile(g_fileInfo.filePath, text, g_fileInfo.encoding, g_fileInfo.lineEnding))
    {
        CenteredMessageBox(g_hwndMain, L"Failed to save file.", APP_NAME, MB_OK | MB_ICONERROR);
        return;
    }

    g_editor.SetDirty(false);
    UpdateTitle();
    StartWatchingCurrentFile();
}

void DoSaveAs()
{
    std::wstring filePath;
    if(!FileIO::ShowSaveDialog(g_hwndMain, filePath))
        return;

    std::wstring text = g_editor.GetText();
    if(!FileIO::WriteFile(filePath, text, g_fileInfo.encoding, g_fileInfo.lineEnding))
    {
        CenteredMessageBox(g_hwndMain, L"Failed to save file.", APP_NAME, MB_OK | MB_ICONERROR);
        return;
    }

    g_fileInfo.filePath = filePath;
    g_editor.SetDirty(false);
    UpdateTitle();
    UpdateStatusBar();
    StartWatchingCurrentFile();
}

void SaveWindowPlacement(HWND hwnd)
{
    g_settings.windowPlacement = {sizeof(WINDOWPLACEMENT)};
    GetWindowPlacement(hwnd, &g_settings.windowPlacement);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    // WM_FINDREPLACE is registered at runtime -- can't be a switch case
    static UINT WM_FINDREPLACE = FindReplace::GetFindMessageId();
    if(msg == WM_FINDREPLACE)
    {
        g_findReplace.HandleFindMessage(lParam);
        return 0;
    }

    switch(msg)
    {
        case WM_UAHDRAWMENU:
        {
            if(g_theme.HandleUahDrawMenu(hwnd, lParam))
                return TRUE;
            break;
        }
        case WM_UAHDRAWMENUITEM:
        {
            if(g_theme.HandleUahDrawMenuItem(hwnd, lParam))
                return TRUE;
            break;
        }
        case UpdateChecker::WM_APP_UPDATE_AVAILABLE:
        {
            g_statusBar.SetUpdateAvailable();
            return 0;
        }
        case Editor::WM_APP_ZOOM:
        {
            int notches = (int)(short)wParam / WHEEL_DELTA;
            if(notches != 0 && g_fontManager.AdjustZoom(notches))
            {
                g_editor.SetFont(g_fontManager.GetFont());
                g_statusBar.SetZoom(g_fontManager.GetZoomPercent());
            }
            return 0;
        }
        case WM_CREATE:
        {
            // Capture initial DPI for the window's monitor
            HDC hdc      = GetDC(hwnd);
            g_currentDpi = hdc ? GetDeviceCaps(hdc, LOGPIXELSY) : 96;
            if(hdc)
                ReleaseDC(hwnd, hdc);

            g_editor.Create(hwnd, g_hInst);
            g_statusBar.Create(hwnd, g_hInst);
            g_statusBar.SetDpi(g_currentDpi);
            g_findReplace.Initialize(hwnd, g_editor.GetHwnd());

            g_editor.SetFont(g_fontManager.GetFont());
            g_statusBar.SetZoom(g_fontManager.GetZoomPercent());
            g_theme.Initialize();

            // Apply dark mode to window immediately (before ShowWindow) to prevent white flash
            g_theme.ApplyToWindow(hwnd);
            Theme::ApplyToScrollbars(g_editor.GetHwnd(), g_theme.IsDark());

            g_fileInfo.encoding   = Encoding::UTF8;
            g_fileInfo.lineEnding = LineEnding::CRLF;

            SetTimer(hwnd, TIMER_STATUSBAR, TIMER_STATUSBAR_MS, nullptr);
            SetTimer(hwnd, TIMER_FILEWATCH, TIMER_FILEWATCH_MS, nullptr);
            SetTimer(hwnd, TIMER_RECOVERY, TIMER_RECOVERY_MS, nullptr);
            UpdateTitle();

            // Crash / shutdown recovery. Each instance owns a per-pid snapshot,
            // so claiming only ever takes a session orphaned by a dead process,
            // never the live buffer of another running Nanopad.
            if(g_recoveryRelaunch)
            {
                // This instance exists to restore a session: load one orphan
                // into its own window (silently -- the recovered text is marked
                // dirty so the user stays in control), then hand off the rest.
                std::wstring recoverText;
                FileInfo recoverInfo;
                if(Recovery::ClaimOrphan(recoverText, recoverInfo))
                {
                    g_editor.SetText(recoverText.c_str());
                    g_fileInfo = recoverInfo;
                    g_editor.SetDirty(true);
                    UpdateTitle();
                    g_recoveryClaimed = true;
                }
                if(Recovery::HasOrphans())
                    Recovery::LaunchRecoveryInstance();
            }
            else if(Recovery::HasOrphans())
            {
                // Normal launch with orphaned sessions present (e.g. after a
                // crash or power loss where Windows did not relaunch us). Restore
                // them into their own separate windows rather than hijacking this
                // one, which the user opened for their own purpose.
                Recovery::LaunchRecoveryInstance();
            }

            // Defer menu checks and status bar styling
            PostMessage(hwnd, WM_DEFERRED_THEME, 0, 0);
            return 0;
        }

        case WM_DEFERRED_THEME:
        {
            // Status bar dark mode (needs window to be created)
            g_statusBar.SetDarkMode(g_theme.IsDark(), g_theme.GetEditBgColor(), g_theme.GetEditFgColor());

            // Menu check states
            HMENU hMenu = GetMenu(hwnd);
            CheckMenuItem(hMenu, IDM_VIEW_STATUSBAR, MF_CHECKED);

            ThemeMode tm = g_theme.GetMode();
            CheckMenuItem(hMenu, IDM_VIEW_DARK, (tm == ThemeMode::Dark) ? MF_CHECKED : MF_UNCHECKED);
            CheckMenuItem(hMenu, IDM_VIEW_LIGHT, (tm == ThemeMode::Light) ? MF_CHECKED : MF_UNCHECKED);
            CheckMenuItem(hMenu, IDM_VIEW_SYSTEM, (tm == ThemeMode::System) ? MF_CHECKED : MF_UNCHECKED);

            if(g_editor.IsWordWrap())
                CheckMenuItem(hMenu, IDM_FORMAT_WORDWRAP, MF_CHECKED);

            bool replacing = NotepadReplace::IsReplacing();
            CheckMenuItem(hMenu, IDM_HELP_REPLACE, replacing ? MF_CHECKED : MF_UNCHECKED);

            bool openWith = NotepadReplace::IsOpenWithRegistered();
            CheckMenuItem(hMenu, IDM_HELP_OPENWITH, openWith ? MF_CHECKED : MF_UNCHECKED);

            bool ctxMenu = NotepadReplace::IsContextMenuRegistered();
            CheckMenuItem(hMenu, IDM_HELP_CTXMENU, ctxMenu ? MF_CHECKED : MF_UNCHECKED);

            // Start background update check (never blocks UI)
            UpdateChecker::CheckAsync(hwnd);
            return 0;
        }

        case WM_SIZE:
        {
            int statusHeight = g_statusBar.IsVisible() ? g_statusBar.GetHeight() : 0;
            g_statusBar.Resize();

            RECT rc;
            GetClientRect(hwnd, &rc);
            int pad = 0;
            g_editor.Resize(pad, pad, rc.right - pad * 2, rc.bottom - statusHeight - pad);
            return 0;
        }

        case WM_TIMER:
            if(wParam == TIMER_STATUSBAR)
            {
                UpdateStatusBar();
            }
            else if(wParam == TIMER_FILEWATCH)
            {
                HandleWatchedFileChange();
            }
            else if(wParam == TIMER_RECOVERY)
            {
                if(g_editor.IsDirty())
                    Recovery::Save(g_editor.GetText(), g_fileInfo);
                else
                    Recovery::Clear();
            }
            return 0;

        case WM_ERASEBKGND:
        {
            HDC hdc = (HDC)wParam;
            RECT rc;
            GetClientRect(hwnd, &rc);
            FillRect(hdc, &rc, g_theme.GetEditBgBrush());
            return 1;
        }

        case WM_SETFOCUS:
            SetFocus(g_editor.GetHwnd());
            return 0;

        case WM_DPICHANGED:
        {
            int newDpi = HIWORD(wParam);

            // Scale font to new DPI
            g_fontManager.OnDpiChanged(newDpi, g_currentDpi);
            g_editor.SetFont(g_fontManager.GetFont());

            // Invalidate cached menu font so it's recreated at new DPI
            g_theme.InvalidateMenuFont(newDpi);

            g_statusBar.SetDpi(newDpi);

            g_currentDpi = newDpi;

            // Resize window to the suggested rect
            const RECT *prc = (const RECT *)lParam;
            SetWindowPos(hwnd, nullptr, prc->left, prc->top, prc->right - prc->left, prc->bottom - prc->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            DrawMenuBar(hwnd);
            return 0;
        }

        case WM_NCPAINT:
        case WM_NCACTIVATE:
        {
            LRESULT result = DefWindowProcW(hwnd, msg, wParam, lParam);
            g_theme.PaintDarkMenuBar(hwnd);
            return result;
        }

        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORSTATIC:
        {
            HDC hdc = (HDC)wParam;
            SetTextColor(hdc, g_theme.GetEditFgColor());
            SetBkColor(hdc, g_theme.GetEditBgColor());
            return (LRESULT)g_theme.GetEditBgBrush();
        }

        case WM_DROPFILES:
        {
            HDROP hDrop = (HDROP)wParam;
            wchar_t path[MAX_PATH];
            if(DragQueryFileW(hDrop, 0, path, MAX_PATH))
                DoOpen(path);
            DragFinish(hDrop);
            return 0;
        }

        case WM_SETTINGCHANGE:
            if(lParam && wcscmp((const wchar_t *)lParam, L"ImmersiveColorSet") == 0)
            {
                g_theme.OnSystemThemeChanged();
                g_theme.ApplyToWindow(hwnd);
                Theme::ApplyToScrollbars(g_editor.GetHwnd(), g_theme.IsDark());
                g_statusBar.SetDarkMode(g_theme.IsDark(), g_theme.GetEditBgColor(), g_theme.GetEditFgColor());
                InvalidateRect(hwnd, nullptr, TRUE);
            }
            return 0;

        case WM_NOTIFY:
        {
            auto *nmhdr = reinterpret_cast<NMHDR *>(lParam);
            if(nmhdr->hwndFrom == g_statusBar.GetHwnd() && nmhdr->code == NM_CLICK)
            {
                auto *nmmouse = reinterpret_cast<NMMOUSE *>(lParam);
                if(g_statusBar.HandleClick(nmmouse->pt))
                    UpdateChecker::OpenReleasePage();
                return 0;
            }
            break;
        }

        case WM_COMMAND:
        {
            int wmId    = LOWORD(wParam);
            int wmEvent = HIWORD(wParam);

            // Edit control notifications
            if(wmEvent == EN_CHANGE && (HWND)lParam == g_editor.GetHwnd())
            {
                if(!g_editor.IsNotifySuppressed() && !g_editor.IsDirty())
                {
                    g_editor.SetDirty(true);
                    UpdateTitle();
                }
                return 0;
            }

            switch(wmId)
            {
                // File
                case IDM_FILE_NEW:
                    DoNew();
                    break;
                case IDM_FILE_OPEN:
                    DoOpen();
                    break;
                case IDM_FILE_SAVE:
                    DoSave();
                    break;
                case IDM_FILE_SAVEAS:
                    DoSaveAs();
                    break;
                case IDM_FILE_PRINT:
                {
                    std::wstring text     = g_editor.GetText();
                    std::wstring fileName = g_fileInfo.filePath.empty() ? L"Untitled" : g_fileInfo.filePath;
                    Printer::Print(hwnd, text, fileName, g_fontManager.GetFont());
                    break;
                }
                case IDM_FILE_EXIT:
                    SendMessage(hwnd, WM_CLOSE, 0, 0);
                    break;

                // Edit
                case IDM_EDIT_UNDO:
                    g_editor.Undo();
                    break;
                case IDM_EDIT_CUT:
                    g_editor.Cut();
                    break;
                case IDM_EDIT_COPY:
                    g_editor.Copy();
                    break;
                case IDM_EDIT_PASTE:
                    g_editor.Paste();
                    break;
                case IDM_EDIT_DELETE:
                    g_editor.Delete();
                    break;
                case IDM_EDIT_SELECTALL:
                    g_editor.SelectAll();
                    break;
                case IDM_EDIT_FIND:
                    g_findReplace.ShowFind();
                    break;
                case IDM_EDIT_FINDNEXT:
                    g_findReplace.FindNext(true);
                    break;
                case IDM_EDIT_FINDPREV:
                    g_findReplace.FindNext(false);
                    break;
                case IDM_EDIT_REPLACE:
                    g_findReplace.ShowReplace();
                    break;
                case IDM_EDIT_GOTO:
                    g_editor.ShowGoToDialog(g_hInst);
                    break;

                // Format
                case IDM_FORMAT_WORDWRAP:
                {
                    g_editor.ToggleWordWrap();
                    Theme::ApplyToScrollbars(g_editor.GetHwnd(), g_theme.IsDark());
                    HMENU hMenu = GetMenu(hwnd);
                    CheckMenuItem(hMenu, IDM_FORMAT_WORDWRAP, g_editor.IsWordWrap() ? MF_CHECKED : MF_UNCHECKED);
                    SendMessage(hwnd, WM_SIZE, 0, 0);
                    break;
                }
                case IDM_FORMAT_FONT:
                {
                    if(g_fontManager.ShowChooseFont(hwnd))
                    {
                        g_editor.SetFont(g_fontManager.GetFont());
                        g_statusBar.SetZoom(g_fontManager.GetZoomPercent());
                        g_fontManager.SaveToSettings(g_settings.font);
                        g_settings.Save();
                    }
                    break;
                }

                // View
                case IDM_VIEW_ZOOMIN:
                case IDM_VIEW_ZOOMOUT:
                {
                    if(g_fontManager.AdjustZoom(wmId == IDM_VIEW_ZOOMIN ? 1 : -1))
                    {
                        g_editor.SetFont(g_fontManager.GetFont());
                        g_statusBar.SetZoom(g_fontManager.GetZoomPercent());
                    }
                    break;
                }
                case IDM_VIEW_ZOOMRESET:
                {
                    if(g_fontManager.ResetZoom())
                    {
                        g_editor.SetFont(g_fontManager.GetFont());
                        g_statusBar.SetZoom(g_fontManager.GetZoomPercent());
                    }
                    break;
                }
                case IDM_VIEW_STATUSBAR:
                {
                    bool visible = !g_statusBar.IsVisible();
                    g_statusBar.SetVisible(visible);
                    HMENU hMenu = GetMenu(hwnd);
                    CheckMenuItem(hMenu, IDM_VIEW_STATUSBAR, visible ? MF_CHECKED : MF_UNCHECKED);
                    SendMessage(hwnd, WM_SIZE, 0, 0);
                    break;
                }
                case IDM_VIEW_DARK:
                case IDM_VIEW_LIGHT:
                case IDM_VIEW_SYSTEM:
                {
                    ThemeMode mode = (wmId == IDM_VIEW_DARK)    ? ThemeMode::Dark
                                     : (wmId == IDM_VIEW_LIGHT) ? ThemeMode::Light
                                                                : ThemeMode::System;
                    g_theme.SetMode(mode);
                    g_theme.ApplyToWindow(hwnd);
                    Theme::ApplyToScrollbars(g_editor.GetHwnd(), g_theme.IsDark());
                    g_statusBar.SetDarkMode(g_theme.IsDark(), g_theme.GetEditBgColor(), g_theme.GetEditFgColor());

                    HMENU hMenu = GetMenu(hwnd);
                    CheckMenuItem(hMenu, IDM_VIEW_DARK, (mode == ThemeMode::Dark) ? MF_CHECKED : MF_UNCHECKED);
                    CheckMenuItem(hMenu, IDM_VIEW_LIGHT, (mode == ThemeMode::Light) ? MF_CHECKED : MF_UNCHECKED);
                    CheckMenuItem(hMenu, IDM_VIEW_SYSTEM, (mode == ThemeMode::System) ? MF_CHECKED : MF_UNCHECKED);

                    InvalidateRect(hwnd, nullptr, TRUE);
                    g_theme.SaveToSettings(g_settings.themeMode);
                    g_settings.Save();
                    break;
                }

                // Help
                case IDM_HELP_OPENWITH:
                {
                    bool registered = NotepadReplace::IsOpenWithRegistered();
                    if(registered)
                    {
                        NotepadReplace::UnregisterOpenWith();
                        CenteredMessageBox(hwnd, L"Nanopad has been removed from the Open With menu.", L"Nanopad",
                                           MB_OK | MB_ICONINFORMATION);
                    }
                    else
                    {
                        NotepadReplace::RegisterOpenWith();
                        CenteredMessageBox(hwnd,
                                           L"Nanopad has been added to the Open With menu\n"
                                           L"for common text file types.",
                                           L"Nanopad", MB_OK | MB_ICONINFORMATION);
                    }
                    HMENU hMenu = GetMenu(hwnd);
                    CheckMenuItem(hMenu, IDM_HELP_OPENWITH,
                                  NotepadReplace::IsOpenWithRegistered() ? MF_CHECKED : MF_UNCHECKED);
                    break;
                }
                case IDM_HELP_CTXMENU:
                {
                    bool registered = NotepadReplace::IsContextMenuRegistered();
                    if(registered)
                    {
                        NotepadReplace::UnregisterContextMenu();
                        CenteredMessageBox(hwnd, L"Context menu entry has been removed.", L"Nanopad",
                                           MB_OK | MB_ICONINFORMATION);
                    }
                    else
                    {
                        NotepadReplace::RegisterContextMenu();
                        CenteredMessageBox(hwnd, L"\"Edit in Nanopad\" has been added to the context menu.", L"Nanopad",
                                           MB_OK | MB_ICONINFORMATION);
                    }
                    HMENU hMenu = GetMenu(hwnd);
                    CheckMenuItem(hMenu, IDM_HELP_CTXMENU,
                                  NotepadReplace::IsContextMenuRegistered() ? MF_CHECKED : MF_UNCHECKED);
                    break;
                }
                case IDM_HELP_REPLACE:
                {
                    bool replacing = NotepadReplace::IsReplacing();
                    if(replacing)
                    {
                        if(NotepadReplace::Restore(hwnd, g_settings))
                        {
                            CenteredMessageBox(hwnd, L"Original Notepad has been restored.", L"Nanopad",
                                               MB_OK | MB_ICONINFORMATION);
                        }
                    }
                    else
                    {
                        if(NotepadReplace::Replace(hwnd, g_settings))
                        {
                            CenteredMessageBox(hwnd, L"Nanopad is now the default Notepad.", L"Nanopad",
                                               MB_OK | MB_ICONINFORMATION);
                        }
                    }
                    HMENU hMenu = GetMenu(hwnd);
                    CheckMenuItem(hMenu, IDM_HELP_REPLACE, NotepadReplace::IsReplacing() ? MF_CHECKED : MF_UNCHECKED);
                    break;
                }
                case IDM_HELP_ABOUT:
                    UpdateChecker::ShowAboutDialog(hwnd);
                    break;
            }
            return 0;
        }

        case WM_QUERYENDSESSION:
            // Windows is shutting down / restarting / logging off. Do not block
            // with a modal prompt -- under fast shutdown we may be killed within
            // seconds regardless. Silently snapshot any unsaved text so we can
            // offer to recover it on next launch, then allow the session to end.
            if(g_editor.IsDirty())
                Recovery::Save(g_editor.GetText(), g_fileInfo);
            return TRUE;

        case WM_ENDSESSION:
            // The session really is ending -- persist settings while we still can.
            if(wParam)
            {
                g_fontManager.SaveToSettings(g_settings.font);
                g_theme.SaveToSettings(g_settings.themeMode);
                SaveWindowPlacement(hwnd);
                g_settings.Save();
            }
            return 0;

        case WM_CLOSE:
            if(!PromptSave())
                return 0;
            // Save all settings at shutdown
            g_fontManager.SaveToSettings(g_settings.font);
            g_theme.SaveToSettings(g_settings.themeMode);
            SaveWindowPlacement(hwnd);
            g_settings.Save();
            // Clean exit -- discard any autosaved recovery snapshot.
            Recovery::Clear();
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            KillTimer(hwnd, TIMER_STATUSBAR);
            KillTimer(hwnd, TIMER_FILEWATCH);
            KillTimer(hwnd, TIMER_RECOVERY);
            StopWatchingCurrentFile();
            PostQuitMessage(0);
            return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
