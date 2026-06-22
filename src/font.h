#pragma once
#include <windows.h>

class FontManager
{
  public:
    FontManager();
    ~FontManager();

    bool ShowChooseFont(HWND hwndOwner);
    HFONT GetFont();

    void LoadFromSettings(const LOGFONTW &lf);
    void SaveToSettings(LOGFONTW &outLf) const
    {
        outLf = m_logFont;
    }

    // Recreate font scaled to new DPI
    void OnDpiChanged(int newDpi, int oldDpi);

    // Ctrl+mouse wheel zoom. notches is the signed number of wheel detents
    // (positive zooms in, negative zooms out). Returns true if the zoom level
    // changed and the font was recreated.
    bool AdjustZoom(int notches);
    bool ResetZoom();
    int GetZoomPercent() const
    {
        return m_zoomPercent;
    }

  private:
    void EnsureFont();
    void RecreateFont();

    HFONT m_hFont      = nullptr;
    LOGFONTW m_logFont = {};
    bool m_fontCreated = false;
    int m_zoomPercent  = 100;
};
