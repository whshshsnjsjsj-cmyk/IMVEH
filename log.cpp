#include "log.h"
#include <android/log.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>

static FILE* g_file = nullptr;
static bool g_tried = false;
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;

void ImLog(const char* fmt, ...) {
    char buf[512];
    va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
    __android_log_print(ANDROID_LOG_INFO, "ImprovedVehicleMobile", "%s", buf);

    pthread_mutex_lock(&g_mu);
    if (!g_file && !g_tried) {
        g_tried = true;
        static const char* kPaths[] = {
            "/storage/emulated/0/Android/data/com.rockstargames.gtasa/files/IMVEH.log",
            "/sdcard/Android/data/com.rockstargames.gtasa/files/IMVEH.log"};
        for (const char* p : kPaths) { g_file = fopen(p, "w"); if (g_file) break; }
    }
    if (g_file) { fprintf(g_file, "%s\n", buf); fflush(g_file); }
    pthread_mutex_unlock(&g_mu);
}
