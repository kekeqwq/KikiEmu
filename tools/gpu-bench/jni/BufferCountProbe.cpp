#include <jni.h>

#include <android/native_window_jni.h>
#include <vndk/window.h>

extern "C" JNIEXPORT jint JNICALL
Java_com_kiki_gpubench_GpuBenchActivity_nativeSetBufferCount(JNIEnv* env, jclass,
                                                               jobject surface, jint count) {
    if (surface == nullptr || count < 3 || count > 5) {
        return -1;
    }

    ANativeWindow* window = ANativeWindow_fromSurface(env, surface);
    if (window == nullptr) {
        return -1;
    }

    const int result = ANativeWindow_setBufferCount(window, static_cast<size_t>(count));
    ANativeWindow_release(window);
    return result;
}
