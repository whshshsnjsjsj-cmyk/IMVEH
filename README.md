# Improved Vehicle Mobile (v0.1, belum dikompilasi/diuji)

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
