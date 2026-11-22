LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)
LOCAL_MODULE    := ImprovedVehicleMobile
LOCAL_SRC_FILES := main.cpp ui.cpp
# Letakkan header AML di ./include/mod/amlmod.h (dari repo AndroidModLoader)
LOCAL_C_INCLUDES := $(LOCAL_PATH)/include
LOCAL_CPPFLAGS  := -std=c++17 -O2 -fno-exceptions -fno-rtti -fvisibility=hidden
LOCAL_LDLIBS    := -llog -ldl -lGLESv3 -lEGL
include $(BUILD_SHARED_LIBRARY)
