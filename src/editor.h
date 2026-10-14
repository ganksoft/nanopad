#pragma once
#include <windows.h>
#include <string>

class Editor
{
  public:
    Editor();
    ~Editor();

    bool Create(HWND parent, HINSTANCE hInst);
    void Resize(int x, int y, int cx, int cy);

    void SetText(const wchar_t *text);
    std::wstring GetText() const;

    void SetFont(HFONT font);

    bool IsDirty() const
    {
        return m_dirty;
    }
    void SetDirty(bool dirty)
    {
        m_dirty = dirty;
    }
    bool IsNotifySuppressed() const
    {
        return m_suppressNotify;
    }

    void ToggleWordWrap();
    void SetWordWrap(bool wrap);
    bool IsWordWrap() const
    {
        return m_wordWrap;
    }

    // Edit operations
    void Undo();
    void Cut();
    void Copy();
    void Paste();
    void Delete();
    void SelectAll();

    // Caret info
    void GetCaretPos(int &line, int &col) const;
    int GetLineCount() const;
    int GetCharCount() const;

    // Go To Line
    void ShowGoToDialog(HINSTANCE hInst);
    void GoToLine(int line);

    HWND GetHwnd() const
    {
        return m_hwndEdit;
    }

    // Posted to the parent window when the user holds Ctrl and turns the mouse
    // wheel. wParam carries the signed wheel delta (multiples of WHEEL_DELTA).
    static constexpr UINT WM_APP_ZOOM = WM_APP + 3;

  private:
    void RecreateControl();
    bool IsTripleClick(LPARAM lParam) const;
    void SelectCaretLine();

    static LRESULT CALLBACK EditSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass,
                                             DWORD_PTR dwRefData);

    HWND m_hwndParent     = nullptr;
    HWND m_hwndEdit       = nullptr;
    HINSTANCE m_hInst     = nullptr;
    HFONT m_hCurrentFont  = nullptr; // not owned
    bool m_dirty          = false;
    bool m_wordWrap       = false;
    bool m_suppressNotify = false;
    RECT m_rect           = {};

    // Triple-click tracking: the EDIT control handles double-click itself, so a
    // third click is detected as a WM_LBUTTONDOWN close in time and position to
    // the preceding WM_LBUTTONDBLCLK.
    bool m_dblClickPending = false;
    LONG m_dblClickTime    = 0;
    POINT m_dblClickPt     = {};
};
