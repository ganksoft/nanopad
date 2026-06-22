#include "editor.h"
#include "resource.h"
#include <commctrl.h>

Editor::Editor()  = default;
Editor::~Editor() = default;

bool Editor::Create(HWND parent, HINSTANCE hInst)
{
    m_hwndParent = parent;
    m_hInst      = hInst;

    DWORD style =
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_LEFT | ES_MULTILINE | ES_WANTRETURN | ES_NOHIDESEL | ES_AUTOVSCROLL;

    if(!m_wordWrap)
        style |= WS_HSCROLL | ES_AUTOHSCROLL;

    m_hwndEdit =
        CreateWindowExW(0, L"EDIT", nullptr, style, 0, 0, 0, 0, parent, (HMENU)(UINT_PTR)IDC_EDIT, hInst, nullptr);

    if(!m_hwndEdit)
        return false;

    // Remove the default text limit (default is 32KB for multiline EDIT)
    SendMessage(m_hwndEdit, EM_SETLIMITTEXT, 0, 0);

    SetWindowSubclass(m_hwndEdit, EditSubclassProc, 0, (DWORD_PTR)this);

    return true;
}

// Intercept Ctrl+mouse wheel for zoom; let the standard control handle the rest.
LRESULT CALLBACK Editor::EditSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR,
                                          DWORD_PTR dwRefData)
{
    if(msg == WM_MOUSEWHEEL && (GET_KEYSTATE_WPARAM(wParam) & MK_CONTROL))
    {
        Editor *self = (Editor *)dwRefData;
        int delta    = GET_WHEEL_DELTA_WPARAM(wParam);
        PostMessage(self->m_hwndParent, WM_APP_ZOOM, (WPARAM)delta, 0);
        return 0;
    }

    if(msg == WM_NCDESTROY)
        RemoveWindowSubclass(hwnd, EditSubclassProc, 0);

    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

void Editor::Resize(int x, int y, int cx, int cy)
{
    m_rect = {x, y, x + cx, y + cy};
    if(m_hwndEdit)
        MoveWindow(m_hwndEdit, x, y, cx, cy, TRUE);
}

void Editor::SetText(const wchar_t *text)
{
    if(!m_hwndEdit)
        return;
    m_suppressNotify = true;
    SetWindowTextW(m_hwndEdit, text);
    m_suppressNotify = false;
}

std::wstring Editor::GetText() const
{
    if(!m_hwndEdit)
        return L"";
    int len = GetWindowTextLengthW(m_hwndEdit);
    if(len == 0)
        return L"";
    std::wstring text(len, L'\0');
    GetWindowTextW(m_hwndEdit, text.data(), len + 1);
    return text;
}

void Editor::SetFont(HFONT font)
{
    m_hCurrentFont = font;
    if(m_hwndEdit && font)
        SendMessage(m_hwndEdit, WM_SETFONT, (WPARAM)font, TRUE);
}

void Editor::ToggleWordWrap()
{
    m_wordWrap = !m_wordWrap;
    RecreateControl();
}

// Preserve text, selection, and font when toggling word wrap
void Editor::RecreateControl()
{
    std::wstring text = GetText();
    DWORD selStart = 0, selEnd = 0;
    SendMessage(m_hwndEdit, EM_GETSEL, (WPARAM)&selStart, (LPARAM)&selEnd);
    bool wasDirty = m_dirty;

    DestroyWindow(m_hwndEdit);
    m_hwndEdit = nullptr;
    Create(m_hwndParent, m_hInst);

    SetText(text.c_str());

    if(m_hCurrentFont)
        SendMessage(m_hwndEdit, WM_SETFONT, (WPARAM)m_hCurrentFont, TRUE);

    SendMessage(m_hwndEdit, EM_SETSEL, selStart, selEnd);
    MoveWindow(m_hwndEdit, m_rect.left, m_rect.top, m_rect.right - m_rect.left, m_rect.bottom - m_rect.top, TRUE);

    m_dirty = wasDirty;
    SetFocus(m_hwndEdit);
}

void Editor::Undo()
{
    SendMessage(m_hwndEdit, EM_UNDO, 0, 0);
}
void Editor::Cut()
{
    SendMessage(m_hwndEdit, WM_CUT, 0, 0);
}
void Editor::Copy()
{
    SendMessage(m_hwndEdit, WM_COPY, 0, 0);
}
void Editor::Paste()
{
    SendMessage(m_hwndEdit, WM_PASTE, 0, 0);
}
void Editor::Delete()
{
    SendMessage(m_hwndEdit, WM_CLEAR, 0, 0);
}
void Editor::SelectAll()
{
    SendMessage(m_hwndEdit, EM_SETSEL, 0, -1);
}

void Editor::GetCaretPos(int &line, int &col) const
{
    if(!m_hwndEdit)
    {
        line = 1;
        col  = 1;
        return;
    }

    DWORD selStart = 0;
    SendMessage(m_hwndEdit, EM_GETSEL, (WPARAM)&selStart, 0);

    line          = (int)SendMessage(m_hwndEdit, EM_LINEFROMCHAR, selStart, 0) + 1;
    int lineStart = (int)SendMessage(m_hwndEdit, EM_LINEINDEX, line - 1, 0);
    col           = selStart - lineStart + 1;
}

int Editor::GetLineCount() const
{
    if(!m_hwndEdit)
        return 0;
    return (int)SendMessage(m_hwndEdit, EM_GETLINECOUNT, 0, 0);
}

int Editor::GetCharCount() const
{
    if(!m_hwndEdit)
        return 0;
    return GetWindowTextLengthW(m_hwndEdit);
}

// Go To Line dialog proc
static INT_PTR CALLBACK GoToDlgProc(HWND hdlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch(msg)
    {
        case WM_INITDIALOG:
            SetWindowLongPtr(hdlg, GWLP_USERDATA, lParam);
            SetFocus(GetDlgItem(hdlg, IDC_GOTO_LINE));
            return FALSE;

        case WM_COMMAND:
            switch(LOWORD(wParam))
            {
                case IDOK:
                {
                    wchar_t buf[32];
                    GetDlgItemTextW(hdlg, IDC_GOTO_LINE, buf, 32);
                    int line = _wtoi(buf);
                    if(line > 0)
                    {
                        Editor *editor = (Editor *)GetWindowLongPtr(hdlg, GWLP_USERDATA);
                        if(editor)
                            editor->GoToLine(line);
                        EndDialog(hdlg, IDOK);
                    }
                    return TRUE;
                }
                case IDCANCEL:
                    EndDialog(hdlg, IDCANCEL);
                    return TRUE;
            }
            break;
    }
    return FALSE;
}

void Editor::ShowGoToDialog(HINSTANCE hInst)
{
    DialogBoxParamW(hInst, MAKEINTRESOURCE(IDD_GOTO), m_hwndParent, GoToDlgProc, (LPARAM)this);
}

void Editor::GoToLine(int line)
{
    if(!m_hwndEdit)
        return;
    int pos = (int)SendMessage(m_hwndEdit, EM_LINEINDEX, line - 1, 0);
    if(pos >= 0)
    {
        SendMessage(m_hwndEdit, EM_SETSEL, pos, pos);
        SendMessage(m_hwndEdit, EM_SCROLLCARET, 0, 0);
    }
}
