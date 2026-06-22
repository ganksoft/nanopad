#include "statusbar.h"
#include "resource.h"
#include <commctrl.h>
#include <uxtheme.h>
#include <cstdio>

static constexpr int BASE_PARTS[] = {200, 380, 490, 560, 660};
static constexpr int NUM_BASE     = _countof(BASE_PARTS);

StatusBar::StatusBar() = default;

StatusBar::~StatusBar()
{
    if(m_hwnd)
        RemoveWindowSubclass(m_hwnd, SubclassProc, 0);
    if(m_cachedBgBrush)
        DeleteObject(m_cachedBgBrush);
    if(m_cachedDivPen)
        DeleteObject(m_cachedDivPen);
    if(m_linkFont)
        DeleteObject(m_linkFont);
}

bool StatusBar::Create(HWND parent, HINSTANCE hInst)
{
    m_hwnd = CreateWindowExW(0, STATUSCLASSNAMEW, nullptr, WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP, 0, 0, 0, 0, parent,
                             (HMENU)(UINT_PTR)IDC_STATUSBAR, hInst, nullptr);

    if(!m_hwnd)
        return false;

    SetWindowSubclass(m_hwnd, SubclassProc, 0, (DWORD_PTR)this);
    UpdateParts();
    return true;
}

void StatusBar::Resize()
{
    if(m_hwnd)
    {
        SendMessage(m_hwnd, WM_SIZE, 0, 0);
        if(m_updateAvail)
            UpdateParts();
    }
}

void StatusBar::Update(int line, int col, int charCount, int lineCount, Encoding enc, LineEnding le)
{
    if(!m_hwnd || !m_visible)
        return;

    swprintf_s(m_partText[0], L"  Ln %d, Col %d", line, col);
    swprintf_s(m_partText[1], L"  %d chars, %d lines", charCount, lineCount);
    swprintf_s(m_partText[2], L"  %s", FileIO::EncodingToString(enc));
    swprintf_s(m_partText[3], L"  %s", FileIO::LineEndingToString(le));

    if(m_darkMode)
    {
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
    else
    {
        for(int i = 0; i < NUM_BASE; i++)
            SendMessage(m_hwnd, SB_SETTEXTW, i, (LPARAM)m_partText[i]);
    }
}

void StatusBar::SetZoom(int percent)
{
    swprintf_s(m_partText[4], L"  Zoom: %d%%", percent);

    if(!m_hwnd || !m_visible)
        return;

    if(m_darkMode)
        InvalidateRect(m_hwnd, nullptr, FALSE);
    else
        SendMessage(m_hwnd, SB_SETTEXTW, 4, (LPARAM)m_partText[4]);
}

void StatusBar::SetVisible(bool visible)
{
    m_visible = visible;
    if(m_hwnd)
        ShowWindow(m_hwnd, visible ? SW_SHOW : SW_HIDE);
}

bool StatusBar::IsVisible() const
{
    return m_visible;
}

int StatusBar::GetHeight() const
{
    if(!m_hwnd || !m_visible)
        return 0;
    RECT rc;
    GetWindowRect(m_hwnd, &rc);
    return rc.bottom - rc.top;
}

void StatusBar::SetDarkMode(bool dark, COLORREF bg, COLORREF fg)
{
    m_darkMode = dark;
    m_bgColor  = bg;
    m_fgColor  = fg;

    if(m_cachedBgBrush)
    {
        DeleteObject(m_cachedBgBrush);
        m_cachedBgBrush = nullptr;
    }
    if(m_cachedDivPen)
    {
        DeleteObject(m_cachedDivPen);
        m_cachedDivPen = nullptr;
    }

    if(dark)
    {
        m_cachedBgBrush = CreateSolidBrush(bg);
        m_cachedDivPen  = CreatePen(PS_SOLID, 1, RGB(60, 60, 60));
    }

    if(m_hwnd)
    {
        SetWindowTheme(m_hwnd, dark ? L"DarkMode_Explorer" : L"Explorer", nullptr);

        if(!dark)
        {
            SendMessage(m_hwnd, SB_SETBKCOLOR, 0, (LPARAM)CLR_DEFAULT);
            for(int i = 0; i < NUM_BASE; i++)
                SendMessage(m_hwnd, SB_SETTEXTW, i, (LPARAM)m_partText[i]);
        }

        InvalidateRect(m_hwnd, nullptr, TRUE);
    }
}

void StatusBar::SetDpi(int dpi)
{
    m_dpi = dpi;
    if(m_updateAvail)
        CreateLinkFont();
    UpdateParts();
}

void StatusBar::SetUpdateAvailable()
{
    m_updateAvail = true;
    wcscpy_s(m_partText[6], L"Update available");
    CreateLinkFont();
    UpdateParts();
    if(m_hwnd)
        InvalidateRect(m_hwnd, nullptr, FALSE);
}

bool StatusBar::HandleClick(POINT pt)
{
    if(!m_updateAvail || !m_hwnd)
        return false;

    int edges[7] = {};
    SendMessage(m_hwnd, SB_GETPARTS, NUM_BASE + 2, (LPARAM)edges);

    return pt.x >= edges[NUM_BASE];
}

void StatusBar::UpdateParts()
{
    if(!m_hwnd)
        return;

    if(m_updateAvail)
    {
        RECT rc;
        GetClientRect(m_hwnd, &rc);
        int grip     = GetSystemMetrics(SM_CXHSCROLL);
        int linkEdge = rc.right - m_linkPartWidth - grip;

        int parts[7];
        for(int i = 0; i < NUM_BASE; i++)
            parts[i] = MulDiv(BASE_PARTS[i], m_dpi, 96);
        parts[NUM_BASE]     = linkEdge;
        parts[NUM_BASE + 1] = -1;
        SendMessage(m_hwnd, SB_SETPARTS, NUM_BASE + 2, (LPARAM)parts);
    }
    else
    {
        int parts[6];
        for(int i = 0; i < NUM_BASE; i++)
            parts[i] = MulDiv(BASE_PARTS[i], m_dpi, 96);
        parts[NUM_BASE] = -1;
        SendMessage(m_hwnd, SB_SETPARTS, NUM_BASE + 1, (LPARAM)parts);
    }
}

void StatusBar::CreateLinkFont()
{
    if(m_linkFont)
    {
        DeleteObject(m_linkFont);
        m_linkFont = nullptr;
    }

    if(!m_hwnd)
        return;

    HFONT hBase = (HFONT)SendMessage(m_hwnd, WM_GETFONT, 0, 0);
    if(!hBase)
        hBase = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

    LOGFONTW lf = {};
    GetObjectW(hBase, sizeof(lf), &lf);
    lf.lfUnderline = TRUE;
    m_linkFont     = CreateFontIndirectW(&lf);

    // Measure text width for part sizing
    HDC hdc    = GetDC(m_hwnd);
    HFONT hOld = (HFONT)SelectObject(hdc, m_linkFont);
    SIZE sz    = {};
    GetTextExtentPoint32W(hdc, m_partText[6], (int)wcslen(m_partText[6]), &sz);
    m_linkPartWidth = sz.cx + MulDiv(16, m_dpi, 96);
    SelectObject(hdc, hOld);
    ReleaseDC(m_hwnd, hdc);
}

void StatusBar::DrawLinkPart(HDC hdc, const RECT &rc)
{
    COLORREF linkColor = m_darkMode ? RGB(100, 149, 237) : RGB(0, 102, 204);

    HFONT hOldFont = m_linkFont ? (HFONT)SelectObject(hdc, m_linkFont) : nullptr;
    SetTextColor(hdc, linkColor);
    SetBkMode(hdc, TRANSPARENT);

    RECT rcText = rc;
    DrawTextW(hdc, m_partText[6], -1, &rcText, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    if(hOldFont)
        SelectObject(hdc, hOldFont);
}

LRESULT CALLBACK StatusBar::SubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR,
                                         DWORD_PTR dwRefData)
{
    auto *sb = (StatusBar *)dwRefData;

    if(msg == WM_ERASEBKGND && sb->m_darkMode)
    {
        HDC hdc = (HDC)wParam;
        RECT rc;
        GetClientRect(hwnd, &rc);
        if(sb->m_cachedBgBrush)
            FillRect(hdc, &rc, sb->m_cachedBgBrush);
        return 1;
    }

    if(msg == WM_PAINT && sb->m_darkMode)
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        RECT rc;
        GetClientRect(hwnd, &rc);

        if(sb->m_cachedBgBrush)
            FillRect(hdc, &rc, sb->m_cachedBgBrush);

        int numParts = sb->m_updateAvail ? NUM_BASE + 2 : NUM_BASE + 1;
        int parts[7] = {};
        SendMessage(hwnd, SB_GETPARTS, numParts, (LPARAM)parts);

        HFONT hFont = (HFONT)SendMessage(hwnd, WM_GETFONT, 0, 0);
        if(!hFont)
            hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);

        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, sb->m_fgColor);

        HPEN hOldPen = sb->m_cachedDivPen ? (HPEN)SelectObject(hdc, sb->m_cachedDivPen) : nullptr;

        int left = 0;
        for(int i = 0; i < numParts; i++)
        {
            int right = (parts[i] == -1) ? rc.right : parts[i];

            if(i > 0)
            {
                MoveToEx(hdc, left, rc.top + 3, nullptr);
                LineTo(hdc, left, rc.bottom - 3);
            }

            // Link part gets special styling
            if(i == NUM_BASE + 1 && sb->m_updateAvail)
            {
                RECT rcPart = {left, rc.top, right, rc.bottom};
                sb->DrawLinkPart(hdc, rcPart);
            }
            else if(sb->m_partText[i][0])
            {
                RECT rcPart = {left + 2, rc.top, right - 2, rc.bottom};
                DrawTextW(hdc, sb->m_partText[i], -1, &rcPart, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            }

            left = right;
        }

        // Sizing grip
        COLORREF divColor = RGB(60, 60, 60);
        int gs            = GetSystemMetrics(SM_CXHSCROLL);
        for(int row = 0; row < 3; row++)
        {
            for(int col = row; col < 3; col++)
            {
                int x = rc.right - gs + col * 4 + 2;
                int y = rc.bottom - gs + row * 4 + 4;
                SetPixelV(hdc, x, y, divColor);
                SetPixelV(hdc, x + 1, y, divColor);
                SetPixelV(hdc, x, y + 1, divColor);
                SetPixelV(hdc, x + 1, y + 1, divColor);
            }
        }

        if(hOldPen)
            SelectObject(hdc, hOldPen);
        SelectObject(hdc, hOldFont);
        EndPaint(hwnd, &ps);
        return 0;
    }

    // Light mode: custom-paint the link part after standard rendering
    if(msg == WM_PAINT && !sb->m_darkMode && sb->m_updateAvail)
    {
        // Let the status bar paint all standard parts first
        LRESULT lr = DefSubclassProc(hwnd, msg, wParam, lParam);

        // Now overpaint just the link part (5) with our styled text
        RECT rcLink;
        SendMessage(hwnd, SB_GETRECT, NUM_BASE + 1, (LPARAM)&rcLink);

        HDC hdc = GetDC(hwnd);
        // Erase the part background before drawing (prevents ghost text)
        HBRUSH hBg = (HBRUSH)(COLOR_BTNFACE + 1);
        FillRect(hdc, &rcLink, hBg);
        sb->DrawLinkPart(hdc, rcLink);
        ReleaseDC(hwnd, hdc);
        return lr;
    }

    return DefSubclassProc(hwnd, msg, wParam, lParam);
}
