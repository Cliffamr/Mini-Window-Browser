# Checklist capture Veil Browser

Isi pada Windows tujuan. Simpan screenshot/video bukti. Ulangi dua urutan: **Veil dibuka sebelum capture** dan **capture dimulai sebelum Veil**.

## Identitas pengujian

- Veil commit/version:
- Windows edition/build:
- WebView2 Runtime:
- GPU/driver:
- Monitor count dan scaling:
- Aplikasi meeting/recording version:
- Mode capture:

## Gate awal

- [ ] Veil terbuka; bila `WDA_EXCLUDEFROMCAPTURE` gagal, aplikasi tidak tampil.
- [ ] Close menutup proses.
- [ ] Minimize bekerja.
- [ ] Top:on menjaga window di atas.
- [ ] Top:off membiarkan aplikasi lain menutupinya.
- [ ] Add tab berhenti pada lima tab.
- [ ] Address bar membuka HTTPS dan mencari teks.
- [ ] HTTP/file/data/javascript ditolak.
- [ ] Back, Next, Reload bekerja.
- [ ] Pass mengaktifkan opacity/click-through; hotkey `0` memulihkan kontrol.
- [ ] Hotkey `9` menyembunyikan/menampilkan window.
- [ ] Restart tidak mempertahankan cookie/login WebView.
- [ ] Popup, download, kamera, mikrofon, lokasi, notifikasi ditolak.

## Matriks aplikasi

Untuk setiap target, uji share satu window, share layar/display, dan bila tersedia mode capture khusus.

### Zoom

- [ ] Capture dimulai sesudah Veil: Veil tidak muncul; area tidak berubah menjadi kotak hitam.
- [ ] Capture dimulai sebelum Veil: Veil tidak muncul; tidak ada kotak hitam.
- [ ] Top:on/off, Pass, hide/show, close tetap bekerja.

### Google Meet (browser)

- [ ] Capture dimulai sesudah Veil: Veil tidak muncul.
- [ ] Capture dimulai sebelum Veil: Veil tidak muncul; tidak ada kotak hitam.
- [ ] Uji tab share dan entire-screen share secara terpisah.

### Microsoft Teams

- [ ] Capture dimulai sesudah Veil: Veil tidak muncul.
- [ ] Capture dimulai sebelum Veil: Veil tidak muncul; tidak ada kotak hitam.
- [ ] Uji window share dan screen share.

### Discord

- [ ] Capture dimulai sesudah Veil: Veil tidak muncul.
- [ ] Capture dimulai sebelum Veil: Veil tidak muncul; tidak ada kotak hitam.
- [ ] Uji screen share dan application share.

### OBS Studio

- [ ] Window Capture diuji; catat Capture Method.
- [ ] Display Capture diuji; catat hasil.
- [ ] Game Capture diuji hanya sebagai observasi; bukan perlindungan yang dijanjikan.
- [ ] Capture dimulai sebelum Veil; tidak ada kotak hitam.

## Batas wajib dicatat

- Capture card/kamera eksternal: **tidak terlindungi**.
- RDP/remote desktop: **tidak dijamin**.
- Driver/injection/nonstandard capture: **tidak dijamin**.
- Hasil satu mesin/aplikasi tidak membuktikan semua versi atau mode.
