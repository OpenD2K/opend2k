// Android backend of platform/http_client.hpp: java.net.HttpURLConnection in org.opend2k.HttpBridge
// (android/app/src/main/java), called through JNI from the download's worker thread; the body comes
// back through the native callbacks below. Same rules as the other backends (see http_client.hpp).
#include "platform/http_backends.hpp"

#include <SDL3/SDL.h>
#include <jni.h>

#include <vector>

namespace d2k::platform::http::native {

namespace {

struct Ctx {
    const Callbacks* cb;
    const std::atomic<bool>* cancel;
    Response* r;
    std::vector<char> buf;
};

// org.opend2k.HttpBridge through the activity's class loader (a worker thread's FindClass only sees
// the system classes); cached as a global reference.
jclass bridgeClass(JNIEnv* env) {
    static jclass cls = nullptr;
    if (cls) return cls;
    jobject activity = static_cast<jobject>(SDL_GetAndroidActivity());
    if (!activity) return nullptr;
    jclass actCls = env->GetObjectClass(activity);
    jmethodID getLoader = env->GetMethodID(actCls, "getClassLoader", "()Ljava/lang/ClassLoader;");
    jobject loader = getLoader ? env->CallObjectMethod(activity, getLoader) : nullptr;
    jclass loaderCls = env->FindClass("java/lang/ClassLoader");
    jmethodID loadClass = env->GetMethodID(loaderCls, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
    jstring name = env->NewStringUTF("org.opend2k.HttpBridge");
    jobject c = loader && loadClass ? env->CallObjectMethod(loader, loadClass, name) : nullptr;
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        c = nullptr;
    }
    if (c) cls = static_cast<jclass>(env->NewGlobalRef(c));
    env->DeleteLocalRef(name);
    if (c) env->DeleteLocalRef(c);
    if (loader) env->DeleteLocalRef(loader);
    env->DeleteLocalRef(loaderCls);
    env->DeleteLocalRef(actCls);
    env->DeleteLocalRef(activity);
    return cls;
}

}  // namespace

const char* name() { return "HttpURLConnection"; }

bool available(std::string* why) {
    JNIEnv* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
    if (env && bridgeClass(env)) return true;
    if (why) *why = "the app's HTTP bridge class is missing";
    return false;
}

Response get(const Request& req, const Callbacks& cb, const std::atomic<bool>& cancel) {
    Response r;
    JNIEnv* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());  // (attaches this thread)
    jclass cls = env ? bridgeClass(env) : nullptr;
    jmethodID get = cls ? env->GetStaticMethodID(cls, "get", "(JLjava/lang/String;JIILjava/lang/String;[I)Ljava/lang/String;") : nullptr;
    if (!get) {
        if (env && env->ExceptionCheck()) env->ExceptionClear();
        r.error = "the app's HTTP bridge class is missing";
        return r;
    }
    Ctx ctx{&cb, &cancel, &r, {}};
    jstring url = env->NewStringUTF(req.url.c_str());
    jstring ua = env->NewStringUTF(userAgent().c_str());
    jintArray status = env->NewIntArray(1);
    auto res = static_cast<jstring>(env->CallStaticObjectMethod(cls, get, jlong(reinterpret_cast<std::intptr_t>(&ctx)), url,
                                                                jlong(req.resumeFrom), jint(req.connectTimeoutSec * 1000),
                                                                jint(req.stallTimeoutSec * 1000), ua, status));
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        r.error = "HTTP bridge exception";
    } else if (res) {
        const char* s = env->GetStringUTFChars(res, nullptr);
        r.error = s ? s : "";
        env->ReleaseStringUTFChars(res, s);
        env->DeleteLocalRef(res);
    }
    jint st = 0;
    env->GetIntArrayRegion(status, 0, 1, &st);
    if (r.status == 0) r.status = int(st);
    env->DeleteLocalRef(status);
    env->DeleteLocalRef(ua);
    env->DeleteLocalRef(url);
    r.cancelled = r.error == "cancelled";
    r.ok = r.error.empty();
    return r;
}

}  // namespace d2k::platform::http::native

// ---- JNI callbacks from org.opend2k.HttpBridge -------------------------------------------------

using d2k::platform::http::native::Ctx;

extern "C" JNIEXPORT jboolean JNICALL Java_org_opend2k_HttpBridge_nativeOnStart(JNIEnv*, jclass, jlong ctx, jint status,
                                                                               jboolean partial, jlong length) {
    auto* c = reinterpret_cast<Ctx*>(static_cast<std::intptr_t>(ctx));
    c->r->status = int(status);
    c->r->partial = partial;
    c->r->contentLength = std::int64_t(length);
    return !c->cb->onStart || c->cb->onStart(int(status), partial, std::int64_t(length)) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL Java_org_opend2k_HttpBridge_nativeOnData(JNIEnv* env, jclass, jlong ctx, jbyteArray data,
                                                                              jint n) {
    auto* c = reinterpret_cast<Ctx*>(static_cast<std::intptr_t>(ctx));
    if (n <= 0) return JNI_TRUE;
    c->buf.resize(std::size_t(n));
    env->GetByteArrayRegion(data, 0, n, reinterpret_cast<jbyte*>(c->buf.data()));
    return !c->cb->onData || c->cb->onData(c->buf.data(), std::size_t(n)) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL Java_org_opend2k_HttpBridge_nativeCancelled(JNIEnv*, jclass, jlong ctx) {
    return reinterpret_cast<Ctx*>(static_cast<std::intptr_t>(ctx))->cancel->load() ? JNI_TRUE : JNI_FALSE;
}
