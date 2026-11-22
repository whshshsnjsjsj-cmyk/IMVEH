#pragma once
// Log ke logcat DAN ke file (agar bisa dibaca dari HP tanpa adb):
//   /storage/emulated/0/Android/data/com.rockstargames.gtasa/files/IMVEH.log
void ImLog(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
