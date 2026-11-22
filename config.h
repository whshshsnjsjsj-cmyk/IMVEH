#pragma once
#include <stdint.h>
#include <stddef.h>
// Semua nilai yang kemungkinan perlu disesuaikan ada di sini.
namespace cfg {
// ---- input ----
constexpr int kTouchDown = 1, kTouchUp = 3;               // cocokkan dengan log "Touch type=..."

// ---- tata letak objek game (arm64, TEBAKAN - verifikasi lewat log) ----
constexpr size_t kMatrixPtrOffset = 0x18;                 // CPlaceable::m_matrix
constexpr size_t kClumpPtrOffset  = 0x20;                 // CEntity::m_pRwObject (RpClump*, type==2)
constexpr size_t kRpMaterialColorOffset = 8;              // RpMaterial::color (RwRGBA)
constexpr size_t kRpMaterialRefOffset   = 36;             // RpMaterial::refCount (int16)

// ---- material lampu bawaan SA (warna "ajaib" di DFF kendaraan) ----
// slot: 0=depan-kiri, 1=depan-kanan, 2=belakang-kiri, 3=belakang-kanan
constexpr uint8_t kLampColors[4][3] = {{255, 175, 0}, {0, 255, 200}, {185, 255, 0}, {255, 60, 0}};
constexpr bool kSwapSides = false;                        // true jika kiri/kanan terbalik di game

// ---- geometri lampu sein 3D (perkiraan, meter) ----
constexpr float kHalfWidth = 0.90f, kHalfLength = 2.20f, kLampHeight = 0.30f;

// ---- sein NPC (heuristik dari laju putar/yaw) ----
constexpr float kNpcYawOn = 0.18f;     // rad/s untuk mulai berkedip
constexpr float kNpcYawOff = 0.06f;    // rad/s di bawah ini dianggap lurus kembali
constexpr float kNpcMinSpeed = 1.5f;   // m/s
constexpr int   kNpcHoldMs = 1200;     // tetap berkedip setelah belokan selesai
constexpr int   kNpcMaxLights = 12;    // batas AddLight NPC per frame
constexpr float kNpcLightRange = 45.f; // hanya NPC sedekat ini yang diberi cahaya titik

// ---- lampu depan volumetrik ----
constexpr int   kMaxBeams = 4;
constexpr float kBeamRange = 28.f;     // jangkauan kerucut (m)
constexpr float kBeamCosHalf = 0.93f;  // cos(setengah sudut kerucut) ~ 21 derajat
constexpr float kBeamMaxCamDist = 60.f;
constexpr int   kVolDownscale = 2;     // resolusi ray-march = layar / nilai ini
constexpr float kVolDensity = 0.035f;  // naikkan = kabut cahaya lebih pekat
constexpr float kVolGain = 1.0f;
constexpr float kVolMaxDist = 45.f;
}
