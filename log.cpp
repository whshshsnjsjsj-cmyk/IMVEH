#include "log.h"
#include <android/log.h>
#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

static FILE* g_file = nullptr;
static bool g_tried = false;
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;

static void mkdirs(const char* path) {
    char tmp[512]; snprintf(tmp, sizeof tmp, "%s", path);
    for (char* p = tmp + 1; *p; ++p)
        if (*p == '/') { *p = 0; mkdir(tmp, 0775); *p = '/'; }
    mkdir(tmp, 0775);
}

static bool tryOpen(const char* dir, const char* name, char* usedOut, size_t usedSize) {
    char path[600];
    if (dir) { mkdirs(dir); snprintf(path, sizeof path, "%s/%s", dir, name); }
    else snprintf(path, sizeof path, "%s", name);
    g_file = fopen(path, "w");
    if (g_file) snprintf(usedOut, usedSize, "%s", path);
    return g_file != nullptr;
}

static void openLogFile() {
    // nama paket proses ini (mis. com.rockstargames.gtasa atau versi klon)
    char pkg[200] = "com.rockstargames.gtasa";
    if (FILE* f = fopen("/proc/self/cmdline", "r")) {
        char b[200] = {0};
        if (fread(b, 1, sizeof b - 1, f) > 0 && b[0]) snprintf(pkg, sizeof pkg, "%s", b);
        fclose(f);
    }
    char dir1[300], dir2[300], used[600] = {0};
    snprintf(dir1, sizeof dir1, "/storage/emulated/0/Android/data/%s/files", pkg);
    snprintf(dir2, sizeof dir2, "/sdcard/Android/data/%s/files", pkg);
    bool ok = tryOpen(dir1, "IMVEH.log", used, sizeof used) ||
              tryOpen(dir2, "IMVEH.log", used, sizeof used) ||
              tryOpen("/storage/emulated/0/Download", "IMVEH.log", used, sizeof used) ||
              tryOpen("/storage/emulated/0", "IMVEH.log", used, sizeof used);
    if (ok) {
        __android_log_print(ANDROID_LOG_INFO, "ImprovedVehicleMobile", "File log: %s (paket %s)", used, pkg);
        fprintf(g_file, "Paket: %s\nFile log: %s\n", pkg, used); fflush(g_file);
    } else {
        __android_log_print(ANDROID_LOG_INFO, "ImprovedVehicleMobile", "Gagal membuat file log (errno=%d, paket %s)", errno, pkg);
    }
}

void ImLog(const char* fmt, ...) {
    char buf[512];
    va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
    __android_log_print(ANDROID_LOG_INFO, "ImprovedVehicleMobile", "%s", buf);

    pthread_mutex_lock(&g_mu);
    if (!g_file && !g_tried) { g_tried = true; openLogFile(); }
    if (g_file) { fprintf(g_file, "%s\n", buf); fflush(g_file); }
    pthread_mutex_unlock(&g_mu);
}
