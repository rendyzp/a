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

static void notify_result(unsigned n)
{
    wchar_t text[128];

    if (n == 0)
        lstrcpyW(text, L"Paste: tidak ada baris teks");
    else
        wsprintfW(text, L"Paste: %u baris", n);

    g_nid.uFlags = NIF_INFO;
    lstrcpynW(g_nid.szInfoTitle, L"PasteLineCounter",
              ARRAYSIZE(g_nid.szInfoTitle));
    lstrcpynW(g_nid.szInfo, text, ARRAYSIZE(g_nid.szInfo));
    g_nid.dwInfoFlags = NIIF_INFO;
    g_nid.uTimeout = 2500;

    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
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

    notify_result(n);
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
