PasteLineCounter - portable tray helper

Fungsi:
- Berjalan di tray Windows.
- Memantau hanya Ctrl+V dan Shift+Insert, bukan clipboard terus-menerus.
- Setelah paste, menghitung baris teks yang tidak kosong.
- Baris yang hanya berisi spasi/tab tidak dihitung.
- Tidak mengubah Ditto.

Build tanpa Visual Studio:
1. Upload folder ini ke repository GitHub.
2. Buka tab Actions.
3. Pilih "Build PasteLineCounter".
4. Klik "Run workflow".
5. Setelah selesai, buka hasil run dan download artifact "PasteLineCounter-portable".
6. Extract ZIP dan jalankan PasteLineCounter.exe.

Catatan:
- Program ini memerlukan Windows.
- Tidak perlu instalasi; EXE hasil build bersifat portable.
- Jika tidak ingin notifikasi, klik kanan ikon tray dan nonaktifkan "Aktif".
