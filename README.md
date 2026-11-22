# Improved Vehicle Mobile (v0.2, belum diuji di game)

Build (NDK r25+):
1. Salin header AML dari repo AndroidModLoader ke `include/mod/` (amlmod.h, logger.h, dst.).
2. `ndk-build NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=Android.mk NDK_APPLICATION_MK=Application.mk`
3. Hasil `libs/arm64-v8a/libImprovedVehicleMobile.so` -> taruh di folder mod AML (`.../files/mods/`).

Verifikasi (lihat `adb logcat -s ImprovedVehicleMobile`):
- Semua baris `Hook ...: ok`. Kalau ada "TIDAK ditemukan", versi libGTASA Anda tidak punya simbol itu.
- `Touch type=... id=...` -> cocokkan kode DOWN/UP dengan `kTouchDown/kTouchUp` di main.cpp.
- Jika muncul "Matrix kendaraan tidak valid" -> sesuaikan `kMatrixPtrOffset`.
- Jika "UI butuh GLES3" -> tombol tidak digambar.

## Build otomatis di GitHub (Actions)
1. Buat repo baru, lalu unggah **isi** folder `IMVEH` (bukan folder `IMVEH` itu sendiri) ke root repo, sehingga `Android.mk` berada di root.
2. Push ke branch `main`/`master`. Workflow `.github/workflows/build.yml` otomatis:
   - mengambil header AML dari repo AndroidModLoader,
   - membangun dengan NDK bawaan runner (`ndk-build`, arm64-v8a),
   - mengunggah `libImprovedVehicleMobile.so` sebagai artefak (tab **Actions** -> run terbaru -> **Artifacts**).
3. Untuk membuat Release: `git tag v0.1.0 && git push origin v0.1.0` — file `.so` ikut terlampir di Release.
4. Bisa juga dijalankan manual lewat **Actions -> Build -> Run workflow**.

## Perubahan v0.2 (tombol)
- Tombol sekarang digambar dari hook `Render2dStuff` memakai API 2D bawaan game (RenderWare `RwIm2DRenderPrimitive`), bukan GL mentah.
  Versi 0.1 memakai `eglGetCurrentContext()` dan kemungkinan besar selalu null di thread utama, sehingga tombol tidak pernah tampil.
- Tampilan: kaca gelap + kilau, bingkai tipis, glow berdenyut, sein berupa 3 chevron yang menyapu searah belokan, lampu depan dengan sinar yang memanjang/memendek, animasi muncul bertahap, efek tekan (spring) dan riak.
- Log juga ditulis ke file (bisa dibaca dari HP tanpa adb):
  `/storage/emulated/0/Android/data/com.rockstargames.gtasa/files/IMVEH.log`
  Cari baris: `UiInit ... siap`, `Render2dStuff berjalan`, `Kendaraan pemain terdeteksi`, `Tombol digambar pertama kali`.

## Asumsi yang perlu dicek di game
- Layout `RwIm2DVertex` dianggap 28 byte (x,y,z,rhw,warna,u,v). Jika tampilan kacau/tidak muncul, kirim isi IMVEH.log.
- Jika warna merah/biru tertukar, ubah `kColorAbgr` di `ui.cpp` menjadi `true`.
