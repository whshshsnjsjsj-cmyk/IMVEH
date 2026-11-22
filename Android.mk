LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := ImprovedVehicleMobile

# Otomatis mencari semua file .cpp dan .c di folder saat ini dan 1 folder di bawahnya (seperti src/ atau jni/)
ALL_CPP_FILES := $(wildcard $(LOCAL_PATH)/*.cpp $(LOCAL_PATH)/*/*.cpp)
ALL_C_FILES   := $(wildcard $(LOCAL_PATH)/*.c $(LOCAL_PATH)/*/*.c)

# Menggabungkan hasil pencarian dan menghapus prefix LOCAL_PATH agar tidak error
LOCAL_SRC_FILES := $(ALL_CPP_FILES:$(LOCAL_PATH)/%=%) \
                   $(ALL_C_FILES:$(LOCAL_PATH)/%=%)

# Library standar untuk modding (tambahkan yang lain di baris ini jika sebelumnya ada)
LOCAL_LDLIBS := -llog

include $(BUILD_SHARED_LIBRARY)
