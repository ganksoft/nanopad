#include "print.h"
#include <commdlg.h>

// Line iterator: call with offset=0 to start, returns false at end of text
static bool NextLine(const std::wstring &text, size_t &offset, const wchar_t *&out, int &outLen)
{
    if(offset > text.size())
        return false;
    if(offset == text.size())
    {
        out    = nullptr;
        outLen = 0;
        offset++;
        return true;
    }

    size_t eol = text.find(L'\n', offset);
    size_t len = (eol == std::wstring::npos) ? text.size() - offset : eol - offset;
    if(len > 0 && text[offset + len - 1] == L'\r')
        len--;

    out    = text.c_str() + offset;
    outLen = (int)len;
    offset = (eol == std::wstring::npos) ? text.size() + 1 : eol + 1;
    return true;
}

static bool IsBreakChar(wchar_t c)
{
    return c == L' ' || c == L'	';
}

// Returns how many chars of p (n > 0) to print on one line of the given pixel width
static int WrapSegment(HDC hdc, const wchar_t *p, int n, int width)
{
    int fit = 0;
    SIZE sz;
    if(!GetTextExtentExPointW(hdc, p, n, width, &fit, nullptr, &sz) || fit >= n)
        return n;

    // Let whitespace at the break hang past the margin instead of starting the next line
    if(IsBreakChar(p[fit]))
    {
        while(fit < n && IsBreakChar(p[fit]))
            fit++;
        return fit;
    }
    for(int i = fit; i > 0; i--)
        if(IsBreakChar(p[i - 1]))
            return i;

    // No break opportunity: hard break, always make progress, never split a surrogate pair
    if(fit < 1)
        fit = 1;
    if(fit < n && IS_HIGH_SURROGATE(p[fit - 1]))
        fit += (fit == 1) ? 1 : -1;
    return fit;
}

static int CountPrintedLines(HDC hdc, const std::wstring &text, int width)
{
    int count         = 0;
    size_t textOffset = 0;
    const wchar_t *linePtr;
    int lineLen;
    while(NextLine(text, textOffset, linePtr, lineLen))
    {
        do
        {
            int seg = lineLen > 0 ? WrapSegment(hdc, linePtr, lineLen, width) : 0;
            linePtr += seg;
            lineLen -= seg;
            count++;
        } while(lineLen > 0);
    }
    return count;
}

bool Printer::Print(HWND hwndOwner, const std::wstring &text, const std::wstring &fileName, HFONT hFont)
{
    PRINTDLGW pd   = {};
    pd.lStructSize = sizeof(pd);
    pd.hwndOwner   = hwndOwner;
    pd.Flags       = PD_RETURNDC | PD_USEDEVMODECOPIESANDCOLLATE | PD_NOSELECTION;

    if(!PrintDlgW(&pd))
    {
        if(pd.hDevMode)
            GlobalFree(pd.hDevMode);
        if(pd.hDevNames)
            GlobalFree(pd.hDevNames);
        return false;
    }

    HDC hdc = pd.hDC;
    if(!hdc)
        return false;

    int pageWidth  = GetDeviceCaps(hdc, HORZRES);
    int pageHeight = GetDeviceCaps(hdc, VERTRES);
    int dpiX       = GetDeviceCaps(hdc, LOGPIXELSX);
    int dpiY       = GetDeviceCaps(hdc, LOGPIXELSY);

    int physWidth  = GetDeviceCaps(hdc, PHYSICALWIDTH);
    int physHeight = GetDeviceCaps(hdc, PHYSICALHEIGHT);
    int offsetX    = GetDeviceCaps(hdc, PHYSICALOFFSETX);
    int offsetY    = GetDeviceCaps(hdc, PHYSICALOFFSETY);

    // 1" margins measured from the paper edge. Device coords start at the printable area.
    int marginLeft   = dpiX - offsetX;
    int marginTop    = dpiY - offsetY;
    int marginRight  = physWidth - offsetX - dpiX;
    int marginBottom = physHeight - offsetY - dpiY;
    if(marginLeft < 0)
        marginLeft = 0;
    if(marginTop < 0)
        marginTop = 0;
    if(marginRight > pageWidth)
        marginRight = pageWidth;
    if(marginBottom > pageHeight)
        marginBottom = pageHeight;
    if(marginRight <= marginLeft)
    {
        marginLeft  = 0;
        marginRight = pageWidth;
    }
    if(marginBottom <= marginTop)
    {
        marginTop    = 0;
        marginBottom = pageHeight;
    }
    int printWidth  = marginRight - marginLeft;
    int printHeight = marginBottom - marginTop;

    HFONT hPrintFont = nullptr;
    if(hFont)
    {
        LOGFONTW lf;
        GetObjectW(hFont, sizeof(lf), &lf);
        // Scale font from screen DPI to printer DPI
        HDC hScreenDC = GetDC(nullptr);
        int screenDpi = hScreenDC ? GetDeviceCaps(hScreenDC, LOGPIXELSY) : 96;
        if(hScreenDC)
            ReleaseDC(nullptr, hScreenDC);
        lf.lfHeight = MulDiv(lf.lfHeight, dpiY, screenDpi);
        hPrintFont  = CreateFontIndirectW(&lf);
    }

    HFONT hOldFont = hPrintFont ? (HFONT)SelectObject(hdc, hPrintFont) : nullptr;

    TEXTMETRICW tm;
    GetTextMetricsW(hdc, &tm);
    int lineHeight   = tm.tmHeight + tm.tmExternalLeading;
    int linesPerPage = printHeight / lineHeight;
    if(linesPerPage < 1)
        linesPerPage = 1;

    int totalLines = CountPrintedLines(hdc, text, printWidth);
    int totalPages = (totalLines + linesPerPage - 1) / linesPerPage;
    if(totalPages < 1)
        totalPages = 1;

    // Extract just filename using pointer arithmetic
    const wchar_t *docName = fileName.c_str();
    size_t lastSlash       = fileName.find_last_of(L"\\/");
    if(lastSlash != std::wstring::npos)
        docName = fileName.c_str() + lastSlash + 1;
    int docNameLen = (int)wcslen(docName);

    DOCINFOW di    = {};
    di.cbSize      = sizeof(di);
    di.lpszDocName = docName;

    if(StartDocW(hdc, &di) <= 0)
    {
        if(hPrintFont)
        {
            SelectObject(hdc, hOldFont);
            DeleteObject(hPrintFont);
        }
        DeleteDC(hdc);
        return false;
    }

    // Header and footer sit centered in the top and bottom half-inch bands
    int headerY = dpiY / 2 - offsetY;
    if(headerY < 0)
        headerY = 0;
    int footerY = physHeight - offsetY - dpiY / 2;
    if(footerY > pageHeight - lineHeight)
        footerY = pageHeight - lineHeight;

    size_t textOffset      = 0;
    const wchar_t *linePtr = nullptr;
    int lineLen            = 0;
    bool inLine            = false;
    for(int page = 0; page < totalPages; page++)
    {
        StartPage(hdc);

        if(hPrintFont)
            SelectObject(hdc, hPrintFont);

        SetTextAlign(hdc, TA_LEFT | TA_TOP);
        TextOutW(hdc, marginLeft, headerY, docName, docNameLen);

        wchar_t footer[64];
        int footerLen = swprintf_s(footer, L"Page %d of %d", page + 1, totalPages);
        SetTextAlign(hdc, TA_CENTER | TA_TOP);
        TextOutW(hdc, (marginLeft + marginRight) / 2, footerY, footer, footerLen);

        SetTextAlign(hdc, TA_LEFT | TA_TOP);
        int y = marginTop;
        for(int i = 0; i < linesPerPage; i++)
        {
            if(!inLine)
            {
                if(!NextLine(text, textOffset, linePtr, lineLen))
                    break;
                inLine = true;
            }
            int seg = lineLen > 0 ? WrapSegment(hdc, linePtr, lineLen, printWidth) : 0;
            if(seg > 0)
                TextOutW(hdc, marginLeft, y, linePtr, seg);
            linePtr += seg;
            lineLen -= seg;
            if(lineLen == 0)
                inLine = false;
            y += lineHeight;
        }

        EndPage(hdc);
    }

    EndDoc(hdc);

    if(hPrintFont)
    {
        SelectObject(hdc, hOldFont);
        DeleteObject(hPrintFont);
    }
    DeleteDC(hdc);
    if(pd.hDevMode)
        GlobalFree(pd.hDevMode);
    if(pd.hDevNames)
        GlobalFree(pd.hDevNames);

    return true;
}
