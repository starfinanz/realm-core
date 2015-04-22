LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

TESTS := $(wildcard ../test_*.cpp ../large_tests/*.cpp ../util/*.cpp ../../src/realm/*.cpp ../../src/realm/util/*.cpp ../../src/realm/impl/*.cpp)
SOURCES := $(filter-out %_tool.cpp,$(TESTS:%=../%))

LOCAL_MODULE     := native-activity
LOCAL_SRC_FILES  := $(SOURCES) main.cpp
LOCAL_C_INCLUDES := ../../src
LOCAL_CFLAGS     := -std=c++11 -DREALM_HAVE_CONFIG
LOCAL_LDLIBS     := -llog -landroid
LOCAL_LDFLAGS    += -L$(LOCAL_PATH)/../../../android-lib
LOCAL_STATIC_LIBRARIES += android_native_app_glue

include $(BUILD_SHARED_LIBRARY)

$(call import-module,android/native_app_glue)
