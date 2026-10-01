#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <shellapi.h>
#include <wctype.h>

#define WM_APP_TRAY (WM_APP + 10)
#define ID_TRAY_EXIT 1001
#define ID_TRAY_TOGGLE 1002
#define ID_TIMER_PASTE 2001
#define PASTE_DELAY_MS 120

static HWND g_hwnd = NULL;
static NOTIFYICONDATAW g_nid;
static BOOL g_enabled = TRUE;
static BOOL g_ctrl = FALSE;
static BOOL g_shift = FALSE;
static BOOL g_pending = FALSE;
static HWND g_popup = NULL;
static wchar_t g_popup_text[128];
static const UINT ID_TIMER_POPUP = 2002;
static const UINT POPUP_SHOW_MS = 30000;

static unsigned count_nonempty_lines(const wchar_t *s)
{
    unsigned n = 0;
    BOOL has_text = FALSE;

    if (!s) return 0;

    for (const wchar_t *p = s;; ++p) {
        wchar_t c = *p;

        if (c == L'\r' || c == L'\n' || c == L'\0') {
            if (has_text) ++n;
            has_text = FALSE;

            if (c == L'\0') break;
            if (c == L'\r' && p[1] == L'\n') ++p;
        } else if (!iswspace(c)) {
            has_text = TRUE;
        }
    }
    return n;
}

static LRESULT CALLBACK popup_proc(HWND hwnd, UINT msg,
                                   WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_NCHITTEST:
        /* Popup menerima klik agar bisa ditutup dengan sekali klik. */
        return HTCLIENT;

    case WM_LBUTTONDOWN:
        KillTimer(hwnd, ID_TIMER_POPUP);
        ShowWindow(hwnd, SW_HIDE);
        return 0;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);

        /* Subtle neon/glass look: dark translucent body + soft neon border. */
        HBRUSH bg = CreateSolidBrush(RGB(18, 20, 28));
        FillRect(dc, &rc, bg);
        DeleteObject(bg);

        /* Outer dim glow */
        HPEN glow = CreatePen(PS_SOLID, 3, RGB(40, 180, 210));
        HGDIOBJ oldPen = SelectObject(dc, glow);
        HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
        RoundRect(dc, rc.left + 1, rc.top + 1, rc.right - 1, rc.bottom - 1, 14, 14);
        SelectObject(dc, oldBrush);
        SelectObject(dc, oldPen);
        DeleteObject(glow);

        /* Thin bright neon edge */
        HPEN neon = CreatePen(PS_SOLID, 1, RGB(90, 235, 255));
        oldPen = SelectObject(dc, neon);
        oldBrush = SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
        RoundRect(dc, rc.left + 2, rc.top + 2, rc.right - 2, rc.bottom - 2, 12, 12);
        SelectObject(dc, oldBrush);
        SelectObject(dc, oldPen);
        DeleteObject(neon);

        SetBkMode(dc, TRANSPARENT);

        /* "Paste:" is regular; the result/count is bold. */
        const wchar_t prefix[] = L"Paste:";
        const wchar_t *rest = g_popup_text + 6; /* after "Paste:" */

        HFONT font_regular = CreateFontW(
            14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

        HFONT font_bold = CreateFontW(
            14, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

        SIZE s1, s2;
        HGDIOBJ oldFont = SelectObject(dc, font_regular);
        GetTextExtentPoint32W(dc, prefix, 6, &s1);

        SelectObject(dc, font_bold);
        GetTextExtentPoint32W(dc, rest, -1, &s2);

        int totalW = s1.cx + s2.cx;
        int x = (rc.right - rc.left - totalW) / 2;
        int textH = (s1.cy > s2.cy) ? s1.cy : s2.cy;
        int y = (rc.bottom - rc.top - textH) / 2;

        SetTextColor(dc, RGB(210, 225, 232));
        SelectObject(dc, font_regular);
        TextOutW(dc, x, y, prefix, 6);

        SetTextColor(dc, RGB(235, 250, 255));
        SelectObject(dc, font_bold);
        TextOutW(dc, x + s1.cx, y, rest, lstrlenW(rest));

        SelectObject(dc, oldFont);
        DeleteObject(font_regular);
        DeleteObject(font_bold);

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_TIMER:
        if (wParam == ID_TIMER_POPUP) {
            KillTimer(hwnd, ID_TIMER_POPUP);
            ShowWindow(hwnd, SW_HIDE);
            return 0;
        }
        break;

    case WM_ERASEBKGND:
        return 1;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static BOOL create_popup(HINSTANCE hInst)
{
    const wchar_t cls[] = L"PasteLineCounterPopup";
    WNDCLASSW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.hInstance = hInst;
    wc.lpfnWndProc = popup_proc;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_INFOBK + 1);
    wc.lpszClassName = cls;

    if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return FALSE;

    g_popup = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_LAYERED,
        cls, L"PasteLineCounter",
        WS_POPUP,
        0, 0, 100, 28,
        NULL, NULL, hInst, NULL);

    if (!g_popup)
        return FALSE;

    /* Slight transparency so the popup feels like a floating neon badge. */
    SetLayeredWindowAttributes(g_popup, 0, 228, LWA_ALPHA);

    HRGN rgn = CreateRoundRectRgn(0, 0, 100, 28, 12, 12);
    SetWindowRgn(g_popup, rgn, TRUE);

    return TRUE;
}

static void show_popup_result(unsigned n)
{
    if (!g_popup)
        return;

    if (n == 0)
        lstrcpyW(g_popup_text, L"Paste: tidak ada baris teks");
    else
        wsprintfW(g_popup_text, L"Paste: %u baris", n);

    /* Fit the popup width to the actual text instead of keeping a wide fixed box. */
    HDC dc = GetDC(g_popup);
    HFONT measureFont = CreateFontW(
        14, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HGDIOBJ oldFont = SelectObject(dc, measureFont);
    SIZE textSize = {0, 0};
    GetTextExtentPoint32W(dc, g_popup_text, -1, &textSize);
    SelectObject(dc, oldFont);
    DeleteObject(measureFont);
    ReleaseDC(g_popup, dc);

    int w = textSize.cx + 24;
    if (w < 90) w = 90;
    int h = 28;

    SetWindowPos(g_popup, NULL, 0, 0, w, h,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);

    HRGN rgn = CreateRoundRectRgn(0, 0, w, h, 12, 12);
    SetWindowRgn(g_popup, rgn, TRUE);

    /* Fixed position: upper-left area of the current primary work area. */
    RECT wa;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);

    /* Mepet sisi kiri; diturunkan sekitar 3x tinggi popup dari posisi lama. */
    int x = wa.left;
    int y = wa.top + 114;

    if (x < wa.left) x = wa.left;
    if (y < wa.top + 4) y = wa.top + 4;
    if (y + h > wa.bottom - 4) y = wa.bottom - h - 4;

    KillTimer(g_popup, ID_TIMER_POPUP);
    SetWindowPos(g_popup, HWND_TOPMOST, x, y, w, h,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(g_popup, NULL, TRUE);
    UpdateWindow(g_popup);
    SetTimer(g_popup, ID_TIMER_POPUP, POPUP_SHOW_MS, NULL);
}

static void count_clipboard(void)
{
    if (!OpenClipboard(g_hwnd))
        return;

    HANDLE h = GetClipboardData(CF_UNICODETEXT);
    if (!h) {
        CloseClipboard();
        return;
    }

    const wchar_t *p = (const wchar_t *)GlobalLock(h);
    if (!p) {
        CloseClipboard();
        return;
    }

    unsigned n = count_nonempty_lines(p);
    GlobalUnlock(h);
    CloseClipboard();

    show_popup_result(n);
}

static void request_count(void)
{
    if (!g_enabled || g_pending)
        return;

    g_pending = TRUE;
    SetTimer(g_hwnd, ID_TIMER_PASTE, PASTE_DELAY_MS, NULL);
}

static void update_tip(void)
{
    g_nid.uFlags = NIF_TIP;
    lstrcpynW(g_nid.szTip,
              g_enabled ? L"PasteLineCounter - Aktif"
                        : L"PasteLineCounter - Nonaktif",
              ARRAYSIZE(g_nid.szTip));
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

static void show_tray_menu(void)
{
    POINT pt;
    GetCursorPos(&pt);

    HMENU menu = CreatePopupMenu();
    if (!menu) return;

    AppendMenuW(menu, MF_STRING, ID_TRAY_TOGGLE,
                g_enabled ? L"Nonaktifkan sementara" : L"Aktifkan");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, ID_TRAY_EXIT, L"Keluar");

    SetForegroundWindow(g_hwnd);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN,
                   pt.x, pt.y, 0, g_hwnd, NULL);
    PostMessageW(g_hwnd, WM_NULL, 0, 0);
    DestroyMenu(menu);
}

static BOOL register_raw_keyboard(void)
{
    RAWINPUTDEVICE rid;
    rid.usUsagePage = 0x01;       /* Generic Desktop Controls */
    rid.usUsage = 0x06;           /* Keyboard */
    rid.dwFlags = RIDEV_INPUTSINK;
    rid.hwndTarget = g_hwnd;

    return RegisterRawInputDevices(&rid, 1, sizeof(rid));
}

static void process_raw_input(HRAWINPUT hRaw)
{
    UINT size = 0;
    if (GetRawInputData(hRaw, RID_INPUT, NULL, &size,
                        sizeof(RAWINPUTHEADER)) == (UINT)-1)
        return;

    BYTE *buffer = (BYTE *)HeapAlloc(GetProcessHeap(), 0, size);
    if (!buffer) return;

    if (GetRawInputData(hRaw, RID_INPUT, buffer, &size,
                        sizeof(RAWINPUTHEADER)) != size) {
        HeapFree(GetProcessHeap(), 0, buffer);
        return;
    }

    RAWINPUT *ri = (RAWINPUT *)buffer;

    if (ri->header.dwType == RIM_TYPEKEYBOARD) {
        RAWKEYBOARD *k = &ri->data.keyboard;
        BOOL up = (k->Flags & RI_KEY_BREAK) != 0;
        UINT v = k->VKey;

        if (v == VK_LCONTROL || v == VK_RCONTROL || v == VK_CONTROL)
            g_ctrl = !up;

        else if (v == VK_LSHIFT || v == VK_RSHIFT || v == VK_SHIFT)
            g_shift = !up;

        else if (!up && v == 'V' && g_ctrl)
            request_count();

        else if (!up && v == VK_INSERT && g_shift)
            request_count();
    }

    HeapFree(GetProcessHeap(), 0, buffer);
}

static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg,
                                 WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_INPUT:
        process_raw_input((HRAWINPUT)lParam);
        return 0;

    case WM_TIMER:
        if (wParam == ID_TIMER_PASTE) {
            KillTimer(hwnd, ID_TIMER_PASTE);
            g_pending = FALSE;
            count_clipboard();
            return 0;
        }
        break;

    case WM_COMMAND:
        if (LOWORD(wParam) == ID_TRAY_EXIT) {
            DestroyWindow(hwnd);
            return 0;
        }

        if (LOWORD(wParam) == ID_TRAY_TOGGLE) {
            g_enabled = !g_enabled;
            update_tip();
            return 0;
        }
        break;

    case WM_APP_TRAY:
        if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU)
            show_tray_menu();
        return 0;

    case WM_DESTROY:
        if (g_popup) {
            KillTimer(g_popup, ID_TIMER_POPUP);
            DestroyWindow(g_popup);
            g_popup = NULL;
        }
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE hPrev,
                    PWSTR cmd, int show)
{
    (void)hPrev;
    (void)cmd;
    (void)show;

    const wchar_t cls[] = L"PasteLineCounterWindow";

    WNDCLASSW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.hInstance = hInst;
    wc.lpfnWndProc = wnd_proc;
    wc.lpszClassName = cls;

    if (!RegisterClassW(&wc))
        return 1;

    g_hwnd = CreateWindowExW(
        0, cls, L"PasteLineCounter",
        0, 0, 0, 0, 0,
        HWND_MESSAGE, NULL, hInst, NULL);

    if (!g_hwnd)
        return 2;

    if (!create_popup(hInst)) {
        DestroyWindow(g_hwnd);
        return 5;
    }

    ZeroMemory(&g_nid, sizeof(g_nid));
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = g_hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    g_nid.uCallbackMessage = WM_APP_TRAY;
    g_nid.hIcon = LoadIconW(NULL, IDI_APPLICATION);
    lstrcpynW(g_nid.szTip, L"PasteLineCounter - Aktif",
              ARRAYSIZE(g_nid.szTip));

    if (!Shell_NotifyIconW(NIM_ADD, &g_nid)) {
        DestroyWindow(g_hwnd);
        return 3;
    }

    if (!register_raw_keyboard()) {
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        DestroyWindow(g_hwnd);
        return 4;
    }

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return 0;
}
