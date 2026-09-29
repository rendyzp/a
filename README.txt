PasteLineCounter - native Windows

Fungsi:
- Berjalan di system tray.
- Mendeteksi Ctrl+V dan Shift+Insert tanpa polling clipboard.
- Setelah paste, membaca clipboard sekali setelah jeda singkat.
- Menghitung baris yang memiliki karakter non-spasi.
- Baris kosong/spasi/tab tidak dihitung.
- Tidak mengubah Ditto.
- Menu tray: aktif/nonaktif sementara dan keluar.

Build:
1. Instal MSYS2 (Windows 10 64-bit atau lebih baru).
2. Buka MSYS2 UCRT64.
3. Jalankan:
   pacman -S mingw-w64-ucrt-x86_64-gcc
4. Masuk ke folder source dan jalankan:
   gcc -O2 -s -mwindows -municode -static-libgcc PasteLineCounter.c -o PasteLineCounter.exe -luser32 -lshell32

Catatan:
- Program mendeteksi shortcut paste secara global. Ia tidak mengubah input keyboard.
- Jika aplikasi target atau Ditto menggunakan metode paste selain Ctrl+V/Shift+Insert, shortcut tersebut tidak akan terdeteksi.
- Setelah EXE dibuat, EXE dapat dijalankan tanpa MSYS2.
