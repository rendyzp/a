#Requires AutoHotkey v2.0
#SingleInstance Force
Persistent

; PasteLineCounter - lightweight tray helper for Ditto/Windows paste.
; Counts non-empty text lines after Ctrl+V or Shift+Insert.
; Lines containing only spaces/tabs are ignored.

A_IconTip := "PasteLineCounter"
A_TrayMenu.Delete()
A_TrayMenu.Add("Aktif", ToggleEnabled)
A_TrayMenu.Add("Keluar", (*) => ExitApp())
A_TrayMenu.Check("Aktif")

gEnabled := true

~^v::QueuePasteCount()
~+Insert::QueuePasteCount()

QueuePasteCount(*) {
    global gEnabled
    if !gEnabled
        return
    ; Ditto may need a short moment to place the selected item on the clipboard.
    ; The timer runs once, so there is no continuous clipboard polling.
    SetTimer(ReadAndNotify, -180)
}

ReadAndNotify(*) {
    text := A_Clipboard
    if !IsSet(text) || text = ""
        return

    ; Only text clipboard content is relevant. Non-text clipboard formats are ignored.
    if InStr(text, "`0")
        return

    text := StrReplace(text, "`r`n", "`n")
    text := StrReplace(text, "`r", "`n")
    count := 0
    for line in StrSplit(text, "`n") {
        ; Ignore empty lines and lines containing only spaces/tabs.
        if Trim(line, " `t") != ""
            count++
    }

    if count > 0
        TrayTip("Paste: " count " baris", "PasteLineCounter")
}

ToggleEnabled(*) {
    global gEnabled
    gEnabled := !gEnabled
    if gEnabled
        A_TrayMenu.Check("Aktif")
    else
        A_TrayMenu.Uncheck("Aktif")
}
