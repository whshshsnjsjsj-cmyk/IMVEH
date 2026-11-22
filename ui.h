#pragma once
// Lapisan tombol modern. Digambar lewat API 2D bawaan game (RenderWare Im2D),
// dipanggil dari hook Render2dStuff di thread utama - bukan GL mentah.
enum ButtonId { BTN_LEFT = 0, BTN_HAZARD, BTN_RIGHT, BTN_LIGHTS, BTN_COUNT };

// Fungsi bawaan libGTASA.so yang dipakai untuk menggambar.
struct UiGfx {
    float (*nearScreenZ)();                              // RwIm2DGetNearScreenZ (opsional)
    int   (*rsSet)(int state, void* value);              // RwRenderStateSet     (disarankan)
    int   (*rsGet)(int state, void* value);              // RwRenderStateGet     (disarankan)
    int   (*renderPrimitive)(int prim, void* verts, int count);   // RwIm2DRenderPrimitive (wajib)
};

bool UiInit(const UiGfx& gfx);   // false = tidak bisa menggambar
// Panggil tiap frame dari Render2dStuff. visible=false -> tombol memudar lalu berhenti digambar.
void UiDraw(int screenW, int screenH, const bool active[BTN_COUNT], bool visible);
bool UiShown();                  // true bila tombol cukup terlihat untuk menerima sentuhan

// Aman dipanggil dari thread sentuhan.
int  UiHitTest(int x, int y, int screenW, int screenH);
void UiSetPressed(int id, bool pressed);
