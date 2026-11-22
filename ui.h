#pragma once
// Lapisan tombol modern (digambar langsung dengan GLES3, rounded-rect SDF).
enum ButtonId { BTN_LEFT = 0, BTN_HAZARD, BTN_RIGHT, BTN_LIGHTS, BTN_COUNT };

// Panggil hanya dari thread render (context EGL aktif).
void UiDraw(int screenW, int screenH, const bool active[BTN_COUNT], bool blinkOn);

// Aman dipanggil dari thread sentuhan. Return id tombol atau -1.
int  UiHitTest(int x, int y, int screenW, int screenH);
void UiSetPressed(int id, bool pressed);
