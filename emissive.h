#include "logger.h"
#include "main.h" // Tambahkan header utama yang menyimpan namespace ivm
#pragma once
#include "common.h"

// Material emissive "asli" game: material lensa lampu kendaraan ditandai saat dibaca dari DFF (lewat warna ajaib SA),
// lalu saat material itu dipakai menggambar, emission-nya diset lewat emu_glMaterialfv(GL_EMISSION).
typedef void (*EmuMaterialfvFn)(unsigned face, unsigned pname, const float* params);
enum { EM_SIG_NONE = 0, EM_SIG_LEFT = 1, EM_SIG_RIGHT = 2, EM_SIG_HAZARD = 3 };

void EmisInit(EmuMaterialfvFn fn);

// ---- thread utama ----
void EmisNoteHeadlight(void* veh);                     // dari DoHeadLightBeam (lampu depan menyala)
void EmisVehicleState(void* veh, int sig, bool blink); // sekali per kendaraan per frame (setelah PreRender)
void EmisFrame();                                      // sekali per frame: buang state kadaluarsa
void EmisSetCurrentClump(void* clump);                 // dari CVehicleModelInfo::SetEditableMaterials

// ---- siklus material ----
void EmisMaterialRead(void* material);                 // setelah RpMaterialStreamRead
void EmisMaterialClone(const void* src, void* dst);   // setelah RpMaterialClone
void EmisMaterialDestroy(void* material);              // sebelum RpMaterialDestroy

// ---- saat menggambar ----
void EmisAfterMaterialProps(const void* material);     // setelah _rwOpenGLLightsSetMaterialProperties

void EmisLogStats();
