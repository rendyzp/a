#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <shellapi.h>
#include <wchar.h>
#include <wctype.h>

#define WM_TRY_COUNT_PASTE (WM_APP + 1)
#define ID_TRAY_EXIT 1001
#define ID_TRAY_TOGGLE 1002
#define ID_TIMER_PASTE 2001
#define HOTKEY_DELAY_MS 120

static HINSTANCE g_hInst;
static HWND g_hwnd;
static HHOOK g_hook;
static BOOL g_enabled = TRUE;
static BOOL g_ctrlDown = FALSE;
static BOOL g_shiftDown = FALSE;
static BOOL g_countPending = FALSE;
static NOTIFYICONDATAW g_nid;

static unsigned count_nonempty_lines(const wchar_t *s)
{
    unsigned count = 0;
    BOOL hasText = FALSE;

    if (!s || !*s) return 0;

    for (const wchar_t *p = s;; ++p) {
        wchar_t c = *p;

        if (c == L'\r' || c == L'\n' || c == L'\0') {
            if (hasText) ++count;
            hasText = FALSE;

            if (c == L'\0') break;

            if (c == L'\r' && p[1] == L'\n') ++p;
        } else if (!iswspace(c)) {
            hasText = TRUE;
        }
    }
    return count;
}

static void show_result(unsigned lines)
{
    wchar_t msg[128];

    if (lines == 0) {
        lstrcpyW(msg, L"Paste: tidak ada baris teks");
    } else {
        wsprintfW(msg, L"Paste: %u baris", lines);
    }

    g_nid.uFlags = NIF_INFO;
    lstrcpynW(g_nid.szInfoTitle, L"PasteLineCounter", ARRAYSIZE(g_nid.szInfoTitle));
    lstrcpynW(g_nid.szInfo, msg, ARRAYSIZE(g_nid.szInfo));
    g_nid.dwInfoFlags = NIIF_INFO;
    g_nid.uTimeout = 2500;
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

static void count_clipboard_now(void)
{
    if (!g_enabled) return;

    if (!OpenClipboard(g_hwnd))
        return;

    HANDLE h = GetClipboardData(CF_UNICODETEXT);
    if (!h) {
        CloseClipboard();
        return;
    }

    const wchar_t *text = (const wchar_t *)GlobalLock(h);
    if (!text) {
        CloseClipboard();
        return;
    }

    unsigned lines = count_nonempty_lines(text);
    GlobalUnlock(h);
    CloseClipboard();

    show_result(lines);
}

static void request_count(void)
{
    if (!g_enabled || g_countPending) return;

    g_countPending = TRUE;
    SetTimer(g_hwnd, ID_TIMER_PASTE, HOTKEY_DELAY_MS, NULL);
}

static LRESULT CALLBACK keyboard_proc(int code, WPARAM wParam, LPARAM lParam)
{
    if (code == HC_ACTION && g_enabled) {
        KBDLLHOOKSTRUCT *k = (KBDLLHOOKSTRUCT *)lParam;

        if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) {
            switch (k->vkCode) {
                case VK_LCONTROL:
                case VK_RCONTROL:
                case VK_CONTROL:
                    g_ctrlDown = TRUE;
                    break;

                case VK_LSHIFT:
                case VK_RSHIFT:
                case VK_SHIFT:
                    g_shiftDown = TRUE;
                    break;

                case 'V':
                    if (g_ctrlDown)
                        request_count();
                    break;

                case VK_INSERT:
                    if (g_shiftDown)
                        request_count();
                    break;
            }
        } else if (wParam == WM_KEYUP || wParam == WM_SYSKEYUP) {
            switch (k->vkCode) {
                case VK_LCONTROL:
                case VK_RCONTROL:
                case VK_CONTROL:
                    g_ctrlDown = FALSE;
                    break;

                case VK_LSHIFT:
                case VK_RSHIFT:
                case VK_SHIFT:
                    g_shiftDown = FALSE;
                    break;
            }
        }
    }

    return CallNextHookEx(g_hook, code, wParam, lParam);
}

static void tray_update(void)
{
    g_nid.uFlags = NIF_TIP;
    lstrcpynW(
        g_nid.szTip,
        g_enabled ? L"PasteLineCounter - Aktif" : L"PasteLineCounter - Nonaktif",
        ARRAYSIZE(g_nid.szTip)
    );
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

static void tray_add(void)
{
    ZeroMemory(&g_nid, sizeof(g_nid));
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = g_hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    g_nid.uCallbackMessage = WM_APP + 10;
    g_nid.hIcon = LoadIconW(NULL, IDI_APPLICATION);
    lstrcpynW(g_nid.szTip, L"PasteLineCounter - Aktif", ARRAYSIZE(g_nid.szTip));
    Shell_NotifyIconW(NIM_ADD, &g_nid);
}

static void tray_menu(void)
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
    DestroyMenu(menu);
}

static LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
        case WM_TIMER:
            if (wParam == ID_TIMER_PASTE) {
                KillTimer(hwnd, ID_TIMER_PASTE);
                g_countPending = FALSE;
                count_clipboard_now();
            }
            break;

        case WM_COMMAND:
            if (LOWORD(wParam) == ID_TRAY_EXIT) {
                DestroyWindow(hwnd);
            } else if (LOWORD(wParam) == ID_TRAY_TOGGLE) {
                g_enabled = !g_enabled;
                tray_update();
            }
            break;

        case WM_APP + 10:
            if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU) {
                tray_menu();
            }
            break;

        case WM_DESTROY:
            Shell_NotifyIconW(NIM_DELETE, &g_nid);
            if (g_hook) UnhookWindowsHookEx(g_hook);
            PostQuitMessage(0);
            break;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrev, PWSTR cmdLine, int nCmdShow)
{
    (void)hPrev; (void)cmdLine; (void)nCmdShow;
    g_hInst = hInstance;

    const wchar_t CLASS_NAME[] = L"PasteLineCounterHiddenWindow";

    WNDCLASSW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc = window_proc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hIcon = LoadIconW(NULL, IDI_APPLICATION);

    if (!RegisterClassW(&wc))
        return 1;

    g_hwnd = CreateWindowExW(
        0, CLASS_NAME, L"PasteLineCounter",
        0, 0, 0, 0, 0,
        HWND_MESSAGE, NULL, hInstance, NULL
    );

    if (!g_hwnd)
        return 2;

    tray_add();

    g_hook = SetWindowsHookExW(WH_KEYBOARD_LL, keyboard_proc, hInstance, 0);
    if (!g_hook) {
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        DestroyWindow(g_hwnd);
        return 3;
    }

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return 0;
}
