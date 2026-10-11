/* SPDX-License-Identifier: GPL-3.0-only */
#include <aaudio/AAudio.h>
#include <jni.h>
#include <android/log.h>
#include <atomic>
#include <chrono>
#include <thread>
#include <mutex>
#include <vector>
#include <array>
#include <string>
#include <cstdio>
#include <unistd.h>
#include <sys/resource.h>
#include "core.h"

namespace {
struct Event { int kind, id, value; };
std::mutex commands, display;
std::array<Event, 2048> queue;
unsigned head = 0, tail = 0;
std::atomic<bool> running{false}, panic{false};
std::atomic<int> rate{0}, xruns{0}, audioError{0}, sampleResult{0}, saveError{0};
std::thread worker;
std::array<uint32_t, 240*240> pixels{};
std::vector<uint8_t> pendingSample;
unsigned sampleSlot;
std::string storePath;
bool initialized = false;

void flush() {
    if (!sloop_storage_dirty()) return;
    std::string temp = storePath + ".tmp";
    FILE *f = fopen(temp.c_str(), "wb");
    if (!f) { saveError = 1; return; }
    bool ok = fwrite(sloop_storage(), 1, SLOOP_STORAGE_SIZE, f) == SLOOP_STORAGE_SIZE;
    if (ok) ok = fflush(f) == 0 && fsync(fileno(f)) == 0;
    if (fclose(f)) ok = false;
    if (ok && rename(temp.c_str(), storePath.c_str()) == 0) {
        sloop_storage_clean(); saveError = 0;
    } else { unlink(temp.c_str()); saveError = 1; }
}
void enqueue(int kind, int id, int value) {
    std::lock_guard<std::mutex> lock(commands);
    if (head-tail == queue.size()) {
        head = tail = 0; panic = true; // Never leave a note on because a release was dropped.
    }
    queue[head++ % queue.size()] = {kind, id, value};
}
void drain() {
    std::array<Event, 2048> events;
    unsigned count = 0;
    std::vector<uint8_t> sample;
    unsigned slot = 0;
    {
        std::lock_guard<std::mutex> lock(commands);
        while (tail != head) events[count++] = queue[tail++ % queue.size()];
        if (!pendingSample.empty()) { sample.swap(pendingSample); slot = sampleSlot; }
    }
    if (panic.exchange(false)) sloop_event(SLOOP_PANIC, 0, 0);
    for (unsigned i=0; i<count; ++i) sloop_event(events[i].kind, events[i].id, events[i].value);
    if (!sample.empty()) sampleResult = sloop_import_sample(slot, sample.data(), sample.size()) ? 1 : -1;
}
AAudioStream *openAudio() {
    AAudioStreamBuilder *b = nullptr;
    AAudioStream *s = nullptr;
    aaudio_result_t rc = AAudio_createStreamBuilder(&b);
    if (rc != AAUDIO_OK) { audioError = rc; return nullptr; }
    AAudioStreamBuilder_setDirection(b, AAUDIO_DIRECTION_OUTPUT);
    AAudioStreamBuilder_setFormat(b, AAUDIO_FORMAT_PCM_FLOAT);
    AAudioStreamBuilder_setChannelCount(b, 2);
    AAudioStreamBuilder_setPerformanceMode(b, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    AAudioStreamBuilder_setSharingMode(b, AAUDIO_SHARING_MODE_SHARED);
    rc = AAudioStreamBuilder_openStream(b, &s);
    AAudioStreamBuilder_delete(b);
    if (rc != AAUDIO_OK) { audioError = rc; return nullptr; }
    rate = AAudioStream_getSampleRate(s);
    if (rate < 8000 || rate > 192000) { AAudioStream_close(s); audioError = AAUDIO_ERROR_INVALID_RATE; return nullptr; }
    // One worker also runs firmware UI; leave enough queued audio for those passes.
    AAudioStream_setBufferSizeInFrames(s, 6 * AAudioStream_getFramesPerBurst(s));
    rc = AAudioStream_requestStart(s);
    if (rc != AAUDIO_OK) { AAudioStream_close(s); audioError = rc; return nullptr; }
    audioError = 0;
    return s;
}
void run() {
    setpriority(PRIO_PROCESS, 0, -16);
    if (!initialized) {
        std::vector<uint8_t> image(SLOOP_STORAGE_SIZE);
        FILE *f = fopen(storePath.c_str(), "rb");
        bool ok = f && fread(image.data(), 1, image.size(), f) == image.size() && fgetc(f) == EOF;
        if (f) fclose(f);
        sloop_init(ok ? image.data() : nullptr); initialized = true;
    }
    static auto epoch = std::chrono::steady_clock::now();
    uint32_t lastUI = 0, lastSave = 0;
    // Streaming linear interpolation preserves the 44.1 kHz engine timeline across writes.
    // Source phase persists across output chunks and resets only when an output stream reopens.
    std::array<float, 256*2> input;
    unsigned inputPos = 256;
    auto next = [&](float *frame) {
        if (inputPos == 256) { sloop_render(input.data(), 256); inputPos = 0; }
        frame[0] = input[2*inputPos]; frame[1] = input[2*inputPos+1]; ++inputPos;
    };
    std::array<float, 256*2> output;
    AAudioStream *stream = nullptr;
    float a[2], b[2]; double phase = 0;
    while (running) {
        drain();
        uint32_t now = (uint32_t)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-epoch).count();
        if (now-lastUI >= 16) {
            sloop_tick(now);
            std::array<uint32_t, 240*240> frame;
            sloop_pixels(frame.data());
            { std::lock_guard<std::mutex> lock(display); pixels = frame; }
            lastUI = now;
        }
        if (!sloop_playing() && now-lastSave > 2500) { flush(); lastSave = now; }
        if (!stream) {
            stream = openAudio();
            if (!stream) { std::this_thread::sleep_for(std::chrono::milliseconds(100)); continue; }
            next(a); next(b); phase = 0;
        }
        const double step = 44100.0 / rate.load();
        for (unsigned i=0; i<256; i++) {
            output[2*i] = a[0] + (b[0]-a[0])*(float)phase;
            output[2*i+1] = a[1] + (b[1]-a[1])*(float)phase;
            phase += step;
            while (phase >= 1) { a[0]=b[0]; a[1]=b[1]; next(b); phase-=1; }
        }
        unsigned sent=0;
        while (sent<256 && running) {
            aaudio_result_t n = AAudioStream_write(stream, output.data()+2*sent, 256-sent, 100000000);
            if (n < 0) { audioError=n; AAudioStream_close(stream); stream=nullptr; break; }
            if (n == 0) continue;
            sent += (unsigned)n;
        }
        if (stream) xruns = AAudioStream_getXRunCount(stream);
    }
    if (stream) { AAudioStream_requestStop(stream); AAudioStream_close(stream); }
    drain(); sloop_event(SLOOP_PANIC, 0, 0);
    sloop_event(SLOOP_STOP, 0, 0);
    float silence[512]; sloop_render(silence, 256);
    sloop_checkpoint(); flush(); rate = 0;
}
}
extern "C" JNIEXPORT void JNICALL Java_org_sloop_android_Native_start(JNIEnv *e, jclass, jstring path) {
    if (running) return;
    const char *p = e->GetStringUTFChars(path, nullptr); storePath = p; e->ReleaseStringUTFChars(path, p);
    running = true; worker = std::thread(run);
}
extern "C" JNIEXPORT void JNICALL Java_org_sloop_android_Native_stop(JNIEnv *, jclass) {
    running = false; if (worker.joinable()) worker.join();
}
extern "C" JNIEXPORT void JNICALL Java_org_sloop_android_Native_event(JNIEnv *, jclass, jint k, jint id, jint v) { enqueue(k,id,v); }
extern "C" JNIEXPORT void JNICALL Java_org_sloop_android_Native_frame(JNIEnv *e, jclass, jintArray out) {
    if (e->GetArrayLength(out) < 240*240) return;
    std::lock_guard<std::mutex> lock(display);
    e->SetIntArrayRegion(out, 0, pixels.size(), (const jint *)pixels.data());
}
extern "C" JNIEXPORT jstring JNICALL Java_org_sloop_android_Native_status(JNIEnv *e, jclass) {
    char s[160]; snprintf(s,sizeof s,"%d Hz · underruns %d%s%s%s",rate.load(),xruns.load(),
        audioError.load() ? " · audio reconnecting" : "", saveError.load() ? " · save failed" : "",
        sampleResult.load()==-1 ? " · sample rejected (stop playback first)" : sampleResult.load()==1 ? " · sample loaded" : "");
    return e->NewStringUTF(s);
}
extern "C" JNIEXPORT void JNICALL Java_org_sloop_android_Native_sample(JNIEnv *e, jclass, jint slot, jbyteArray data) {
    auto size=e->GetArrayLength(data); if (size<512 || size>81920 || slot<0 || slot>3) { sampleResult=-1; return; }
    std::vector<uint8_t> bytes(size); e->GetByteArrayRegion(data,0,size,(jbyte *)bytes.data());
    std::lock_guard<std::mutex> lock(commands); pendingSample.swap(bytes); sampleSlot=slot; sampleResult=0;
}

extern "C" JNIEXPORT jboolean JNICALL Java_org_sloop_android_Native_restore(JNIEnv *e, jclass, jbyteArray data) {
    if (running || e->GetArrayLength(data) != SLOOP_STORAGE_SIZE) return JNI_FALSE;
    std::vector<uint8_t> image(SLOOP_STORAGE_SIZE);
    e->GetByteArrayRegion(data,0,image.size(),(jbyte *)image.data());
    // Keep restore atomic on disk, including an image with empty project slots.
    std::string temp = storePath + ".tmp";
    FILE *f=fopen(temp.c_str(),"wb"); if (!f) return JNI_FALSE;
    bool ok=fwrite(image.data(),1,image.size(),f)==image.size();
    if (ok) ok=fflush(f)==0 && fsync(fileno(f))==0;
    if (fclose(f)) ok=false;
    if (!ok || rename(temp.c_str(),storePath.c_str())) { unlink(temp.c_str()); return JNI_FALSE; }
    sloop_init(image.data()); initialized = true;
    return JNI_TRUE;
}
