/* hexter for Android - the engine behind an Oboe stream, driven from Kotlin over JNI
 *
 * Copyright (C) 2026 Keith Adler. GPL-2.0-or-later.
 *
 * One engine, one output stream. MIDI arrives on whatever thread Android delivers it on and is
 * queued in a lock-free ring; the audio callback drains the ring into hexter events and renders.
 * Everything the callback touches is either its own or an atomic.
 */
#include <jni.h>
#include <android/log.h>
#include <oboe/Oboe.h>
#include <atomic>
#include <cmath>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

extern "C" {
#include "hexter_engine.h"
}

#define LOG_TAG "hexter"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace {

/* A single-producer, single-consumer ring of MIDI messages. Sysex up to a full 32-voice bank
 * dump fits one slot; anything larger is dropped with a log line. */
struct MidiSlot {
    uint32_t len = 0;
    uint8_t  data[4608];
};

class MidiRing {
  public:
    static constexpr int kSlots = 64;
    bool push(const uint8_t *msg, size_t len) {
        if (len == 0 || len > sizeof(MidiSlot::data)) return false;
        int w = write_.load(std::memory_order_relaxed);
        int next = (w + 1) % kSlots;
        if (next == read_.load(std::memory_order_acquire)) return false;   // full
        slots_[w].len = (uint32_t)len;
        memcpy(slots_[w].data, msg, len);
        write_.store(next, std::memory_order_release);
        return true;
    }
    const MidiSlot *peek() {
        int r = read_.load(std::memory_order_relaxed);
        if (r == write_.load(std::memory_order_acquire)) return nullptr;
        return &slots_[r];
    }
    void pop() { read_.store((read_.load(std::memory_order_relaxed) + 1) % kSlots, std::memory_order_release); }
  private:
    MidiSlot slots_[kSlots];
    std::atomic<int> read_{0}, write_{0};
};

class Synth : public oboe::AudioStreamDataCallback, public oboe::AudioStreamErrorCallback {
  public:
    explicit Synth(int requested_rate) {
        oboe::AudioStreamBuilder b;
        b.setDirection(oboe::Direction::Output)
         ->setPerformanceMode(oboe::PerformanceMode::LowLatency)
         ->setSharingMode(oboe::SharingMode::Exclusive)
         ->setFormat(oboe::AudioFormat::Float)
         ->setChannelCount(oboe::ChannelCount::Mono)
         ->setDataCallback(this)
         ->setErrorCallback(this);
        if (requested_rate > 0) b.setSampleRate(requested_rate);
        oboe::Result r = b.openStream(stream_);
        if (r != oboe::Result::OK) {
            LOGE("open stream: %s", oboe::convertToText(r));
            return;
        }
        rate_ = stream_->getSampleRate();
        engine_ = hexter_engine_new((float)rate_);
        stream_->setBufferSizeInFrames(stream_->getFramesPerBurst() * 2);
        LOGI("stream %d Hz, burst %d, buffer %d, %s/%s", rate_, stream_->getFramesPerBurst(),
             stream_->getBufferSizeInFrames(),
             oboe::convertToText(stream_->getSharingMode()), oboe::convertToText(stream_->getPerformanceMode()));
        stream_->requestStart();
    }
    ~Synth() override {
        if (stream_) { stream_->stop(); stream_->close(); }
        if (engine_) hexter_engine_free(engine_);
    }
    bool ok() const { return engine_ != nullptr; }
    hexter_engine_t *engine() { return engine_; }
    int rate() const { return rate_; }
    bool midi(const uint8_t *msg, size_t len) { return ring_.push(msg, len); }
    float peak() { return peak_.exchange(0.0f); }

    oboe::DataCallbackResult onAudioReady(oboe::AudioStream *, void *audio, int32_t frames) override {
        auto *out = static_cast<float *>(audio);
        uint32_t n = 0;
        while (n < kMaxEvents) {
            const MidiSlot *s = ring_.peek();
            if (!s) break;
            if (hexter_event_from_midi(s->data, s->len, 0, &events_[n]) && events_[n].type != HEXTER_EV_NONE) {
                if (events_[n].type == HEXTER_EV_SYSEX) {            // keep the bytes alive through render()
                    memcpy(sysex_[n % kSysexSlots], s->data, s->len);
                    events_[n].data = sysex_[n % kSysexSlots];
                }
                n++;
            }
            ring_.pop();
        }
        hexter_engine_render(engine_, out, (uint32_t)frames, events_, n);
        float p = 0.0f;
        for (int32_t i = 0; i < frames; i++) { float a = std::fabs(out[i]); if (a > p) p = a; }
        float cur = peak_.load(std::memory_order_relaxed);
        if (p > cur) peak_.store(p, std::memory_order_relaxed);
        return oboe::DataCallbackResult::Continue;
    }
    void onErrorAfterClose(oboe::AudioStream *, oboe::Result r) override {
        LOGE("stream closed: %s", oboe::convertToText(r));
    }

  private:
    static constexpr uint32_t kMaxEvents = 256;
    static constexpr uint32_t kSysexSlots = 8;
    std::shared_ptr<oboe::AudioStream> stream_;
    hexter_engine_t *engine_ = nullptr;
    int rate_ = 0;
    MidiRing ring_;
    hexter_event_t events_[kMaxEvents];
    uint8_t sysex_[kSysexSlots][sizeof(MidiSlot::data)];
    std::atomic<float> peak_{0.0f};
};

std::mutex g_lock;
std::unique_ptr<Synth> g_synth;

}  // namespace

extern "C" {

JNIEXPORT jboolean JNICALL Java_org_keithadler_hexter_HexterEngine_nativeStart(JNIEnv *, jobject, jint rate) {
    std::lock_guard<std::mutex> l(g_lock);
    g_synth = std::make_unique<Synth>(rate);
    if (!g_synth->ok()) { g_synth.reset(); return JNI_FALSE; }
    return JNI_TRUE;
}

JNIEXPORT void JNICALL Java_org_keithadler_hexter_HexterEngine_nativeStop(JNIEnv *, jobject) {
    std::lock_guard<std::mutex> l(g_lock);
    g_synth.reset();
}

JNIEXPORT jint JNICALL Java_org_keithadler_hexter_HexterEngine_nativeSampleRate(JNIEnv *, jobject) {
    std::lock_guard<std::mutex> l(g_lock);
    return g_synth ? g_synth->rate() : 0;
}

/* returns patches loaded; on failure a negative number and the message is logged */
JNIEXPORT jint JNICALL Java_org_keithadler_hexter_HexterEngine_nativeLoadBank(JNIEnv *env, jobject, jbyteArray bytes, jstring hint) {
    std::lock_guard<std::mutex> l(g_lock);
    if (!g_synth) return -1;
    jsize len = env->GetArrayLength(bytes);
    std::vector<uint8_t> buf((size_t)len);
    env->GetByteArrayRegion(bytes, 0, len, reinterpret_cast<jbyte *>(buf.data()));
    const char *h = hint ? env->GetStringUTFChars(hint, nullptr) : nullptr;
    char *err = nullptr;
    int n = hexter_engine_load_bank_memory(g_synth->engine(), buf.data(), buf.size(), h ? h : "bank.syx", 0, &err);
    if (h) env->ReleaseStringUTFChars(hint, h);
    if (n <= 0) { LOGE("load bank: %s", err ? err : "unknown error"); free(err); return -2; }
    hexter_engine_select_program(g_synth->engine(), 0);
    return n;
}

JNIEXPORT jstring JNICALL Java_org_keithadler_hexter_HexterEngine_nativeProgramName(JNIEnv *env, jobject, jint program) {
    std::lock_guard<std::mutex> l(g_lock);
    char name[12] = {0};
    if (g_synth) hexter_engine_get_program_name(g_synth->engine(), program, name);
    return env->NewStringUTF(name);
}

JNIEXPORT void JNICALL Java_org_keithadler_hexter_HexterEngine_nativeSelectProgram(JNIEnv *, jobject, jint program) {
    std::lock_guard<std::mutex> l(g_lock);
    if (g_synth) hexter_engine_select_program(g_synth->engine(), program);
}

JNIEXPORT jint JNICALL Java_org_keithadler_hexter_HexterEngine_nativeProgram(JNIEnv *, jobject) {
    std::lock_guard<std::mutex> l(g_lock);
    return g_synth ? hexter_engine_get_program(g_synth->engine()) : 0;
}

JNIEXPORT void JNICALL Java_org_keithadler_hexter_HexterEngine_nativeSetVolume(JNIEnv *, jobject, jfloat db) {
    std::lock_guard<std::mutex> l(g_lock);
    if (g_synth) hexter_engine_set_volume(g_synth->engine(), db);
}

JNIEXPORT jint JNICALL Java_org_keithadler_hexter_HexterEngine_nativeSetPolyphony(JNIEnv *, jobject, jint voices) {
    std::lock_guard<std::mutex> l(g_lock);
    return g_synth ? hexter_engine_set_polyphony(g_synth->engine(), voices) : 0;
}

/* any thread: a complete MIDI message (channel voice, or F0..F7 sysex) */
JNIEXPORT jboolean JNICALL Java_org_keithadler_hexter_HexterEngine_nativeMidi(JNIEnv *env, jobject, jbyteArray bytes, jint offset, jint count) {
    if (!g_synth || count <= 0) return JNI_FALSE;
    uint8_t buf[4608];
    if (count > (jint)sizeof(buf)) return JNI_FALSE;
    env->GetByteArrayRegion(bytes, offset, count, reinterpret_cast<jbyte *>(buf));
    return g_synth->midi(buf, (size_t)count) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jfloat JNICALL Java_org_keithadler_hexter_HexterEngine_nativePeak(JNIEnv *, jobject) {
    return g_synth ? g_synth->peak() : 0.0f;
}

JNIEXPORT jint JNICALL Java_org_keithadler_hexter_HexterEngine_nativeActiveVoices(JNIEnv *, jobject) {
    std::lock_guard<std::mutex> l(g_lock);
    return g_synth ? hexter_engine_get_active_voices(g_synth->engine()) : 0;
}

}  // extern "C"
