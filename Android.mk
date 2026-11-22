LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)
LOCAL_MODULE    := ImprovedVehicleMobile
LOCAL_SRC_FILES := main.cpp ui.cpp log.cpp common.cpp symscan.cpp volumetric.cpp emissive.cpp
# Implementasi kelas Config milik AML (dibutuhkan oleh makro MYMODCFG)
LOCAL_SRC_FILES += include/mod/config.cpp
# Letakkan header AML di ./include/mod/amlmod.h (dari repo AndroidModLoader)
LOCAL_C_INCLUDES := $(LOCAL_PATH)/include
LOCAL_CPPFLAGS  := -std=c++17 -O2 -fno-exceptions -fno-rtti
LOCAL_LDLIBS    := -llog -ldl -lGLESv3 -lEGL
include $(BUILD_SHARED_LIBRARY)
