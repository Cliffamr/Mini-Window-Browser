# Veil Browser

Veil Browser adalah browser overlay Windows ringan berbasis Win32 dan Microsoft Edge WebView2. Fork ini berasal dari [azurplain/Mini-Window-Browser](https://github.com/azurplain/Mini-Window-Browser) dan tetap GPL-3.0.

## Target

- Windows 10 2004+ atau Windows 11 x64.
- Browser native Win32/WebView2; bukan Electron.
- Address/search bar, Back, Next, Reload, close, minimize.
- Maksimal lima tab. Hanya tab aktif mempunyai WebView; tab lain menyimpan URL/judul sehingga resource tidur, tetapi state formulir/video tidak dipertahankan saat berpindah.
- Profil WebView sementara per proses; cookie/cache/login dihapus best-effort saat keluar.
- Always-on-top dapat dinyalakan/dimatikan.
- Opacity dan click-through melalui mode `Pass`; hotkey default `0` mengembalikan kontrol, hotkey default `9` menyembunyikan/menampilkan window.
- `WDA_EXCLUDEFROMCAPTURE` diterapkan dan dibaca kembali sebelum window ditampilkan. Aplikasi fail-closed bila Windows menolaknya.
- Navigasi top-level hanya HTTPS atau `about:blank`.
- Popup, download, DevTools, context menu, host objects, script dialogs, izin kamera/mikrofon/lokasi/notifikasi ditolak.
- Tidak memakai extension, sync, wallet, atau VPN.

## Batas perlindungan capture

`WDA_EXCLUDEFROMCAPTURE` membantu terhadap banyak API capture standar Windows. Ini **bukan** jaminan invisibilitas universal. Capture card, kamera eksternal, RDP/remote desktop, driver capture, atau metode nonstandar dapat tetap melihat window. Perilaku juga dapat berbeda antar versi Windows dan mode capture aplikasi.

Jangan gunakan build unsigned untuk rapat sensitif sebelum checklist runtime di bawah lulus pada mesin Windows tujuan.

## Build Windows x64

```powershell
nuget install packages.config -OutputDirectory packages -NonInteractive
msbuild '小窗.slnx' /t:Rebuild /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v143 /m /v:minimal /nologo
& './x64/Release/XiaoChuang.Tests.exe'
```

GitHub Actions non-publishing: `.github/workflows/ci.yml`.

## Lisensi

GPL-3.0. Distribusi binary wajib menyediakan corresponding source dan mempertahankan `LICENSE.txt`, `THIRD_PARTY_NOTICES.txt`, serta lisensi WebView2 SDK/WIL.
