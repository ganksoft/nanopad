#include "font.h"
#include <commdlg.h>

FontManager::FontManager()
{
    ZeroMemory(&m_logFont, sizeof(m_logFont));
    // 11pt Consolas scaled by screen DPI (GetDpiForSystem() available on Win10+)
    HDC hdc = GetDC(nullptr);
    int dpi = hdc ? GetDeviceCaps(hdc, LOGPIXELSY) : 96;
    if(hdc)
        ReleaseDC(nullptr, hdc);
    m_logFont.lfHeight  = -MulDiv(11, dpi, 72);
    m_logFont.lfWeight  = FW_NORMAL;
    m_logFont.lfCharSet = DEFAULT_CHARSET;
    m_logFont.lfQuality = CLEARTYPE_QUALITY;
    wcscpy_s(m_logFont.lfFaceName, L"Consolas");
}

FontManager::~FontManager()
{
    if(m_hFont)
        DeleteObject(m_hFont);
}

static constexpr int ZOOM_MIN  = 10;
static constexpr int ZOOM_MAX  = 500;
static constexpr int ZOOM_STEP = 10;

void FontManager::RecreateFont()
{
    if(m_hFont)
    {
        DeleteObject(m_hFont);
        m_hFont = nullptr;
    }

    LOGFONTW lf = m_logFont;
    lf.lfHeight = MulDiv(m_logFont.lfHeight, m_zoomPercent, 100);
    if(lf.lfHeight == 0)
        lf.lfHeight = (m_logFont.lfHeight < 0) ? -1 : 1;

    m_hFont       = CreateFontIndirectW(&lf);
    m_fontCreated = true;
}

HFONT FontManager::GetFont()
{
    if(!m_fontCreated)
        RecreateFont();
    return m_hFont;
}

bool FontManager::ShowChooseFont(HWND hwndOwner)
{
    CHOOSEFONTW cf = {};
    cf.lStructSize = sizeof(cf);
    cf.hwndOwner   = hwndOwner;
    cf.lpLogFont   = &m_logFont;
    cf.Flags       = CF_INITTOLOGFONTSTRUCT | CF_SCREENFONTS | CF_EFFECTS;

    if(!ChooseFontW(&cf))
        return false;

    // An explicit font choice resets any zoom applied via Ctrl+wheel.
    m_zoomPercent = 100;
    RecreateFont();
    return m_hFont != nullptr;
}

void FontManager::LoadFromSettings(const LOGFONTW &lf)
{
    m_logFont = lf;
    if(m_hFont)
    {
        DeleteObject(m_hFont);
        m_hFont = nullptr;
    }
    m_fontCreated = false;
}

void FontManager::OnDpiChanged(int newDpi, int oldDpi)
{
    if(oldDpi == 0)
        oldDpi = 96;
    m_logFont.lfHeight = MulDiv(m_logFont.lfHeight, newDpi, oldDpi);
    RecreateFont();
}

bool FontManager::AdjustZoom(int notches)
{
    int newZoom = m_zoomPercent + notches * ZOOM_STEP;
    if(newZoom < ZOOM_MIN)
        newZoom = ZOOM_MIN;
    if(newZoom > ZOOM_MAX)
        newZoom = ZOOM_MAX;

    if(newZoom == m_zoomPercent)
        return false;

    m_zoomPercent = newZoom;
    RecreateFont();
    return true;
}

bool FontManager::ResetZoom()
{
    if(m_zoomPercent == 100)
        return false;

    m_zoomPercent = 100;
    RecreateFont();
    return true;
}
