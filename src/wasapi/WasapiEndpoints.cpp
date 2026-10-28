#include "afm/wasapi/WasapiEndpoints.h"

#include "afm/core/Denormals.h"

#include <audioclient.h>
#include <mmdeviceapi.h>
#include <mmreg.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <future>

namespace afm::wasapi {

namespace {

// Interface/class ids declared locally so the build doesn't depend on uuid.lib or on
// a particular SDK's __uuidof support.
constexpr CLSID kClsidMMDeviceEnumerator = {0xBCDE0395, 0xE52F, 0x467C, {0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E}};
constexpr IID kIidMMDeviceEnumerator = {0xA95664D2, 0x9614, 0x4F35, {0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6}};
constexpr IID kIidAudioClient = {0x1CB9AD4C, 0xDBFA, 0x4C32, {0xB1, 0x78, 0xC2, 0xF5, 0x68, 0xA7, 0x03, 0xB2}};
constexpr IID kIidAudioCaptureClient = {0xC8ADBD64, 0xE71E, 0x48A0, {0xA4, 0xDE, 0x18, 0x5C, 0x39, 0x5C, 0xD3, 0x17}};
constexpr IID kIidAudioRenderClient = {0xF294ACFC, 0x3146, 0x4483, {0xA7, 0xBF, 0xAD, 0xDC, 0xA7, 0xC2, 0x60, 0xE2}};
constexpr GUID kSubtypePcm = {0x00000001, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71}};
constexpr GUID kSubtypeFloat = {0x00000003, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71}};
constexpr PROPERTYKEY kFriendlyName = {{0xA45C254E, 0xDF1C, 0x4EFD, {0x80, 0x20, 0x67, 0xD1, 0x46, 0xA8, 0x50, 0xE0}}, 14};

enum class SampleKind { Int16, Int24, Int32, Float32 };

const char* kindName(SampleKind k) {
    switch (k) {
    case SampleKind::Int16: return "Int16";
    case SampleKind::Int24: return "Int24";
    case SampleKind::Int32: return "Int32";
    case SampleKind::Float32: return "Float32";
    }
    return "?";
}

std::uint32_t bytesOf(SampleKind k) { return k == SampleKind::Int16 ? 2 : k == SampleKind::Int24 ? 3 : 4; }

win::ComPtr<IMMDeviceEnumerator> createEnumerator() {
    win::ComPtr<IMMDeviceEnumerator> e;
    CoCreateInstance(kClsidMMDeviceEnumerator, nullptr, CLSCTX_ALL, kIidMMDeviceEnumerator, e.putVoid());
    return e;
}

// ---- Sample conversion (device interleaved <-> engine planar) -------------------------

template <typename Read>
void deinterleaveWith(Read read, const BYTE* src, std::uint32_t stride, std::uint32_t devCh, std::uint32_t frames,
                      float* const* lanes, std::uint32_t outCh) {
    for (std::uint32_t ch = 0; ch < outCh; ++ch) {
        const std::uint32_t srcCh = std::min(ch, devCh - 1);
        const BYTE* p = src + srcCh * (stride / devCh);
        float* out = lanes[ch];
        for (std::uint32_t f = 0; f < frames; ++f, p += stride) out[f] = read(p);
    }
}

void deinterleave(SampleKind kind, const BYTE* src, std::uint32_t devCh, std::uint32_t frames, float* const* lanes,
                  std::uint32_t outCh) {
    const std::uint32_t stride = bytesOf(kind) * devCh;
    switch (kind) {
    case SampleKind::Int16:
        deinterleaveWith([](const BYTE* p) { std::int16_t v; std::memcpy(&v, p, 2); return v * (1.0f / 32768.0f); },
                         src, stride, devCh, frames, lanes, outCh);
        break;
    case SampleKind::Int24:
        deinterleaveWith(
            [](const BYTE* p) {
                const auto v = static_cast<std::int32_t>((static_cast<std::uint32_t>(p[0]) << 8) |
                                                         (static_cast<std::uint32_t>(p[1]) << 16) |
                                                         (static_cast<std::uint32_t>(p[2]) << 24));
                return static_cast<float>(v >> 8) * (1.0f / 8388608.0f);
            },
            src, stride, devCh, frames, lanes, outCh);
        break;
    case SampleKind::Int32:
        deinterleaveWith(
            [](const BYTE* p) { std::int32_t v; std::memcpy(&v, p, 4); return static_cast<float>(v) * (1.0f / 2147483648.0f); },
            src, stride, devCh, frames, lanes, outCh);
        break;
    case SampleKind::Float32:
        deinterleaveWith([](const BYTE* p) { float v; std::memcpy(&v, p, 4); return v; }, src, stride, devCh, frames,
                         lanes, outCh);
        break;
    }
}

template <typename Write>
void interleaveWith(Write write, const float* const* lanes, std::uint32_t inCh, std::uint32_t frames, BYTE* dst,
                    std::uint32_t stride, std::uint32_t devCh) {
    const std::uint32_t sampleBytes = stride / devCh;
    for (std::uint32_t c = 0; c < devCh; ++c) {
        BYTE* p = dst + c * sampleBytes;
        // Mono sources feed both front channels; channels beyond the source are silent.
        const float* in = c < inCh ? lanes[c] : (inCh == 1 && c == 1 ? lanes[0] : nullptr);
        for (std::uint32_t f = 0; f < frames; ++f, p += stride) write(p, in ? in[f] : 0.0f);
    }
}

void interleave(SampleKind kind, const float* const* lanes, std::uint32_t inCh, std::uint32_t frames, BYTE* dst,
                std::uint32_t devCh) {
    const std::uint32_t stride = bytesOf(kind) * devCh;
    switch (kind) {
    case SampleKind::Int16:
        interleaveWith([](BYTE* p, float x) {
            const auto v = static_cast<std::int16_t>(std::clamp(x, -1.0f, 1.0f) * 32767.0f);
            std::memcpy(p, &v, 2);
        }, lanes, inCh, frames, dst, stride, devCh);
        break;
    case SampleKind::Int24:
        interleaveWith([](BYTE* p, float x) {
            const auto v = static_cast<std::int32_t>(std::clamp(x, -1.0f, 1.0f) * 8388607.0f);
            p[0] = static_cast<BYTE>(v);
            p[1] = static_cast<BYTE>(v >> 8);
            p[2] = static_cast<BYTE>(v >> 16);
        }, lanes, inCh, frames, dst, stride, devCh);
        break;
    case SampleKind::Int32:
        interleaveWith([](BYTE* p, float x) {
            const auto v = static_cast<std::int32_t>(std::clamp(static_cast<double>(x), -1.0, 1.0) * 2147483647.0);
            std::memcpy(p, &v, 4);
        }, lanes, inCh, frames, dst, stride, devCh);
        break;
    case SampleKind::Float32:
        interleaveWith([](BYTE* p, float x) { std::memcpy(p, &x, 4); }, lanes, inCh, frames, dst, stride, devCh);
        break;
    }
}

// ---- Format negotiation ----------------------------------------------------------------

struct Negotiated {
    SampleKind kind = SampleKind::Float32;
    std::uint32_t channels = 0;
    double rate = 0.0;
    bool exclusive = false;
};

bool parseFormat(const WAVEFORMATEX* f, Negotiated& out) {
    bool isFloat = f->wFormatTag == WAVE_FORMAT_IEEE_FLOAT;
    bool isPcm = f->wFormatTag == WAVE_FORMAT_PCM;
    if (f->wFormatTag == WAVE_FORMAT_EXTENSIBLE && f->cbSize >= 22) {
        const auto* x = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(f);
        isFloat = IsEqualGUID(x->SubFormat, kSubtypeFloat);
        isPcm = IsEqualGUID(x->SubFormat, kSubtypePcm);
    }
    if (isFloat && f->wBitsPerSample == 32) out.kind = SampleKind::Float32;
    else if (isPcm && f->wBitsPerSample == 16) out.kind = SampleKind::Int16;
    else if (isPcm && f->wBitsPerSample == 24) out.kind = SampleKind::Int24;
    else if (isPcm && f->wBitsPerSample == 32) out.kind = SampleKind::Int32;  // includes 24-in-32 (left-justified)
    else return false;
    out.channels = f->nChannels;
    out.rate = f->nSamplesPerSec;
    return out.channels > 0;
}

WAVEFORMATEXTENSIBLE makeFormat(SampleKind kind, std::uint16_t validBits, std::uint16_t channels, DWORD rate) {
    WAVEFORMATEXTENSIBLE x{};
    const auto container = static_cast<WORD>(bytesOf(kind) * 8);
    x.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    x.Format.nChannels = channels;
    x.Format.nSamplesPerSec = rate;
    x.Format.wBitsPerSample = container;
    x.Format.nBlockAlign = static_cast<WORD>(channels * container / 8);
    x.Format.nAvgBytesPerSec = rate * x.Format.nBlockAlign;
    x.Format.cbSize = 22;
    x.Samples.wValidBitsPerSample = validBits;
    x.dwChannelMask = channels == 1 ? SPEAKER_FRONT_CENTER : channels == 2 ? (SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT) : 0;
    x.SubFormat = kind == SampleKind::Float32 ? kSubtypeFloat : kSubtypePcm;
    return x;
}

} // namespace

std::vector<DeviceInfo> listDevices(bool capture) {
    std::vector<DeviceInfo> devices;
    auto enumerator = createEnumerator();
    if (!enumerator) return devices;
    win::ComPtr<IMMDeviceCollection> collection;
    if (FAILED(enumerator->EnumAudioEndpoints(capture ? eCapture : eRender, DEVICE_STATE_ACTIVE, collection.put()))) {
        return devices;
    }
    UINT count = 0;
    collection->GetCount(&count);
    for (UINT i = 0; i < count; ++i) {
        win::ComPtr<IMMDevice> device;
        if (FAILED(collection->Item(i, device.put()))) continue;
        LPWSTR id = nullptr;
        if (FAILED(device->GetId(&id))) continue;
        DeviceInfo info;
        info.id = id;
        CoTaskMemFree(id);

        win::ComPtr<IPropertyStore> props;
        if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, props.put()))) {
            PROPVARIANT value;
            PropVariantInit(&value);
            if (SUCCEEDED(props->GetValue(kFriendlyName, &value)) && value.vt == VT_LPWSTR) {
                info.name = win::toUtf8(value.pwszVal);
            }
            PropVariantClear(&value);
        }
        if (info.name.empty()) info.name = win::toUtf8(info.id);
        devices.push_back(std::move(info));
    }
    return devices;
}

// ---- Device thread ------------------------------------------------------------------------

// Owns one WASAPI stream on a dedicated MMCSS thread. Initialisation happens on that
// thread (MTA); open() blocks until the format is known, the owner then sizes its
// ring/resampler from info() and calls run() with the real-time callback.
class WasapiStream {
public:
    using Callback = std::function<void(float* const* lanes, std::uint32_t channels, std::uint32_t frames)>;

    ~WasapiStream() { stop(); }

    bool open(bool capture, const StreamConfig& config, std::string& error) {
        capture_ = capture;
        config_ = config;
        stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        goEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        std::promise<bool> ready;
        auto readyFuture = ready.get_future();
        thread_ = std::thread([this, &ready] { threadMain(ready); });
        if (!readyFuture.get()) {
            error = error_;
            stop();
            return false;
        }
        return true;
    }

    // `endpointChannels` lanes of scratch are handed to the callback.
    void run(std::uint32_t endpointChannels, Callback callback) {
        endpointChannels_ = endpointChannels;
        callback_ = std::move(callback);
        SetEvent(goEvent_);
    }

    void stop() {
        if (stopEvent_ != nullptr) SetEvent(stopEvent_);
        if (thread_.joinable()) thread_.join();
        for (HANDLE* h : {&stopEvent_, &goEvent_}) {
            if (*h != nullptr) {
                CloseHandle(*h);
                *h = nullptr;
            }
        }
    }

    [[nodiscard]] const StreamInfo& info() const noexcept { return info_; }
    [[nodiscard]] bool lost() const noexcept { return lost_.load(std::memory_order_relaxed); }

private:
    bool fail(const std::string& message, HRESULT hr) {
        error_ = message + " (" + win::hresultMessage(hr) + ")";
        return false;
    }

    bool activate() {
        client_.reset();
        const HRESULT hr = device_->Activate(kIidAudioClient, CLSCTX_ALL, nullptr, client_.putVoid());
        return SUCCEEDED(hr) || fail("Could not activate audio client", hr);
    }

    bool initExclusive(const WAVEFORMATEX* format) {
        REFERENCE_TIME defaultPeriod = 0, minPeriod = 0;
        client_->GetDevicePeriod(&defaultPeriod, &minPeriod);
        REFERENCE_TIME period = minPeriod > 0 ? minPeriod : defaultPeriod;
        HRESULT hr = client_->Initialize(AUDCLNT_SHAREMODE_EXCLUSIVE, AUDCLNT_STREAMFLAGS_EVENTCALLBACK, period, period,
                                         format, nullptr);
        if (hr == AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED) {
            UINT32 frames = 0;
            client_->GetBufferSize(&frames);
            period = static_cast<REFERENCE_TIME>(10'000'000.0 * frames / format->nSamplesPerSec + 0.5);
            if (!activate()) return false;
            hr = client_->Initialize(AUDCLNT_SHAREMODE_EXCLUSIVE, AUDCLNT_STREAMFLAGS_EVENTCALLBACK, period, period,
                                     format, nullptr);
        }
        return SUCCEEDED(hr);
    }

    bool negotiate() {
        WAVEFORMATEX* mix = nullptr;
        HRESULT hr = client_->GetMixFormat(&mix);
        if (FAILED(hr)) return fail("GetMixFormat failed", hr);
        Negotiated shared;
        const bool mixOk = parseFormat(mix, shared);
        const auto mixChannels = static_cast<std::uint16_t>(mix->nChannels);
        const DWORD mixRate = mix->nSamplesPerSec;

        if (config_.exclusive) {
            const DWORD rates[] = {static_cast<DWORD>(config_.engineRate), mixRate, 48000, 44100, 96000};
            const std::uint16_t channelOptions[] = {mixChannels, 2, 1};
            struct Kind { SampleKind kind; std::uint16_t validBits; };
            const Kind kinds[] = {{SampleKind::Int32, 24}, {SampleKind::Int32, 32}, {SampleKind::Int24, 24},
                                  {SampleKind::Int16, 16}, {SampleKind::Float32, 32}};
            for (DWORD rate : rates) {
                for (std::uint16_t ch : channelOptions) {
                    for (const Kind& k : kinds) {
                        const WAVEFORMATEXTENSIBLE f = makeFormat(k.kind, k.validBits, ch, rate);
                        if (client_->IsFormatSupported(AUDCLNT_SHAREMODE_EXCLUSIVE, &f.Format, nullptr) != S_OK) continue;
                        if (initExclusive(&f.Format)) {
                            negotiated_ = {k.kind, ch, static_cast<double>(rate), true};
                            CoTaskMemFree(mix);
                            return true;
                        }
                        if (!activate()) {
                            CoTaskMemFree(mix);
                            return false;
                        }
                    }
                }
            }
        }

        // Shared mode: the device keeps running at the Windows mixer format.
        if (!mixOk) {
            CoTaskMemFree(mix);
            error_ = "Unsupported device mix format";
            return false;
        }
        hr = client_->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK, 0, 0, mix, nullptr);
        CoTaskMemFree(mix);
        if (FAILED(hr)) return fail("Could not open the device", hr);
        negotiated_ = shared;
        negotiated_.exclusive = false;
        return true;
    }

    bool setup() {
        auto enumerator = createEnumerator();
        if (!enumerator) return fail("Could not create the device enumerator", E_FAIL);
        HRESULT hr = enumerator->GetDevice(config_.deviceId.c_str(), device_.put());
        if (FAILED(hr)) return fail("Device not found", hr);
        if (!activate() || !negotiate()) return false;

        hr = client_->GetBufferSize(&bufferFrames_);
        if (FAILED(hr)) return fail("GetBufferSize failed", hr);
        audioEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        hr = client_->SetEventHandle(audioEvent_);
        if (FAILED(hr)) return fail("SetEventHandle failed", hr);
        hr = capture_ ? client_->GetService(kIidAudioCaptureClient, captureClient_.putVoid())
                      : client_->GetService(kIidAudioRenderClient, renderClient_.putVoid());
        if (FAILED(hr)) return fail("GetService failed", hr);

        std::uint32_t period = bufferFrames_;
        if (!negotiated_.exclusive) {
            REFERENCE_TIME defaultPeriod = 0, minPeriod = 0;
            client_->GetDevicePeriod(&defaultPeriod, &minPeriod);
            period = static_cast<std::uint32_t>(std::lround(defaultPeriod * negotiated_.rate / 10'000'000.0));
        }
        info_.exclusive = negotiated_.exclusive;
        info_.deviceRate = negotiated_.rate;
        info_.deviceChannels = negotiated_.channels;
        info_.periodFrames = period;
        char text[160];
        std::snprintf(text, sizeof(text), "%s @ %.0f Hz, %u ch, %s, %.2f ms", kindName(negotiated_.kind),
                      negotiated_.rate, negotiated_.channels, negotiated_.exclusive ? "exclusive" : "shared",
                      period * 1000.0 / negotiated_.rate);
        info_.format = text;
        return true;
    }

    void threadMain(std::promise<bool>& ready) {
        const HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        const bool ok = setup();
        ready.set_value(ok);  // `ready` must not be touched after this line
        if (ok) {
            const HANDLE start[2] = {goEvent_, stopEvent_};
            if (WaitForMultipleObjects(2, start, FALSE, INFINITE) == WAIT_OBJECT_0) {
                stream();
            }
        }
        captureClient_.reset();
        renderClient_.reset();
        client_.reset();
        device_.reset();
        if (audioEvent_ != nullptr) {
            CloseHandle(audioEvent_);
            audioEvent_ = nullptr;
        }
        if (SUCCEEDED(co)) CoUninitialize();
    }

    void stream() {
        const win::ScopedMmcss mmcss;
        const ScopedFlushDenormals ftz;

        // Scratch for one full device buffer, planar.
        std::vector<float> scratch(static_cast<std::size_t>(bufferFrames_) * endpointChannels_);
        std::vector<float*> lanes(endpointChannels_);
        for (std::uint32_t ch = 0; ch < endpointChannels_; ++ch) lanes[ch] = scratch.data() + ch * bufferFrames_;

        if (!capture_) {
            BYTE* data = nullptr;
            if (SUCCEEDED(renderClient_->GetBuffer(bufferFrames_, &data))) {
                renderClient_->ReleaseBuffer(bufferFrames_, AUDCLNT_BUFFERFLAGS_SILENT);
            }
        }
        if (FAILED(client_->Start())) {
            lost_.store(true);
            return;
        }

        const HANDLE waits[2] = {stopEvent_, audioEvent_};
        const std::uint32_t devCh = negotiated_.channels;
        for (;;) {
            const DWORD r = WaitForMultipleObjects(2, waits, FALSE, 2000);
            if (r == WAIT_OBJECT_0) break;
            if (r != WAIT_OBJECT_0 + 1) continue;  // timeout: device stalled, keep waiting

            HRESULT hr = S_OK;
            if (capture_) {
                UINT32 packet = 0;
                while (SUCCEEDED(hr = captureClient_->GetNextPacketSize(&packet)) && packet > 0) {
                    BYTE* data = nullptr;
                    UINT32 frames = 0;
                    DWORD flags = 0;
                    hr = captureClient_->GetBuffer(&data, &frames, &flags, nullptr, nullptr);
                    if (FAILED(hr)) break;
                    const std::uint32_t n = std::min<std::uint32_t>(frames, bufferFrames_);
                    if ((flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0) {
                        for (float* lane : lanes) std::memset(lane, 0, n * sizeof(float));
                    } else {
                        deinterleave(negotiated_.kind, data, devCh, n, lanes.data(), endpointChannels_);
                    }
                    captureClient_->ReleaseBuffer(frames);
                    callback_(lanes.data(), endpointChannels_, n);
                }
            } else {
                UINT32 frames = bufferFrames_;
                if (!negotiated_.exclusive) {
                    UINT32 padding = 0;
                    hr = client_->GetCurrentPadding(&padding);
                    frames = SUCCEEDED(hr) ? bufferFrames_ - padding : 0;
                }
                BYTE* data = nullptr;
                if (frames > 0 && SUCCEEDED(hr = renderClient_->GetBuffer(frames, &data))) {
                    callback_(lanes.data(), endpointChannels_, frames);
                    interleave(negotiated_.kind, lanes.data(), endpointChannels_, frames, data, devCh);
                    renderClient_->ReleaseBuffer(frames, 0);
                }
            }
            if (hr == AUDCLNT_E_DEVICE_INVALIDATED) {
                lost_.store(true);
                break;
            }
        }
        client_->Stop();
    }

    bool capture_ = true;
    StreamConfig config_;
    std::string error_;
    StreamInfo info_;
    Negotiated negotiated_;
    std::uint32_t endpointChannels_ = 0;
    Callback callback_;

    std::thread thread_;
    HANDLE stopEvent_ = nullptr;
    HANDLE goEvent_ = nullptr;
    HANDLE audioEvent_ = nullptr;
    std::atomic<bool> lost_{false};

    win::ComPtr<IMMDevice> device_;
    win::ComPtr<IAudioClient> client_;
    win::ComPtr<IAudioCaptureClient> captureClient_;
    win::ComPtr<IAudioRenderClient> renderClient_;
    UINT32 bufferFrames_ = 0;
};

namespace {

// Target ring fill (in ring frames) for a producer writing bursts of `producerBurst` and
// a consumer reading bursts of `consumerBurst`: both bursts plus half the larger one as
// jitter margin.
std::size_t targetFill(double producerBurst, double consumerBurst) {
    return static_cast<std::size_t>(std::ceil(producerBurst + consumerBurst + std::max(producerBurst, consumerBurst) / 2 + 16));
}

} // namespace

// ---- Input endpoint ---------------------------------------------------------------------

std::shared_ptr<WasapiInputEndpoint> WasapiInputEndpoint::open(const StreamConfig& config, std::string& error) {
    std::shared_ptr<WasapiInputEndpoint> ep(new WasapiInputEndpoint());
    ep->label_ = config.label;
    ep->stream_ = std::make_unique<WasapiStream>();
    if (!ep->stream_->open(true, config, error)) {
        return nullptr;
    }
    const StreamInfo& info = ep->stream_->info();
    ep->channels_ = std::min<std::uint32_t>(info.deviceChannels, 2);

    // Ring frames are device frames. One engine block consumes block * ratio of them.
    const double engineBlockInDeviceFrames = config.engineBlockFrames * info.deviceRate / config.engineRate;
    const std::size_t target = targetFill(info.periodFrames, engineBlockInDeviceFrames);
    ep->ring_ = std::make_unique<AudioRingBuffer>(ep->channels_, target * 4 + 4096);
    ep->resampler_.prepare(ep->channels_, info.deviceRate, config.engineRate, target, kMaxBlockFrames);

    AudioRingBuffer* ring = ep->ring_.get();
    ep->stream_->run(ep->channels_, [ring](float* const* lanes, std::uint32_t channels, std::uint32_t frames) {
        ring->write(ConstAudioBlock{lanes, channels, frames});
    });
    return ep;
}

WasapiInputEndpoint::~WasapiInputEndpoint() {
    if (stream_) stream_->stop();
}

std::string WasapiInputEndpoint::channelName(std::uint32_t ch) const {
    if (channels_ == 1) return label_;
    return label_ + (ch == 0 ? " L" : ch == 1 ? " R" : " " + std::to_string(ch + 1));
}

void WasapiInputEndpoint::pull(AudioBlock dst) noexcept { resampler_.process(*ring_, dst); }

const StreamInfo& WasapiInputEndpoint::info() const noexcept { return stream_->info(); }

bool WasapiInputEndpoint::deviceLost() const noexcept { return stream_->lost(); }

// ---- Output endpoint --------------------------------------------------------------------

std::shared_ptr<WasapiOutputEndpoint> WasapiOutputEndpoint::open(const StreamConfig& config, std::string& error) {
    std::shared_ptr<WasapiOutputEndpoint> ep(new WasapiOutputEndpoint());
    ep->label_ = config.label;
    ep->stream_ = std::make_unique<WasapiStream>();
    if (!ep->stream_->open(false, config, error)) {
        return nullptr;
    }
    const StreamInfo& info = ep->stream_->info();
    ep->channels_ = std::min<std::uint32_t>(info.deviceChannels, 2);

    // Ring frames are engine frames. One device period consumes period * ratio of them.
    const double periodInEngineFrames = info.periodFrames * config.engineRate / info.deviceRate;
    const std::size_t target = targetFill(config.engineBlockFrames, periodInEngineFrames);
    ep->ring_ = std::make_unique<AudioRingBuffer>(ep->channels_, target * 4 + 4096);
    // Shared-mode render may ask for up to a whole device buffer at once.
    const auto maxDeviceFrames = static_cast<std::uint32_t>(info.periodFrames * 4 + 4096);
    ep->resampler_.prepare(ep->channels_, config.engineRate, info.deviceRate, target, maxDeviceFrames);

    WasapiOutputEndpoint* self = ep.get();
    ep->stream_->run(ep->channels_, [self](float* const* lanes, std::uint32_t channels, std::uint32_t frames) {
        self->resampler_.process(*self->ring_, AudioBlock{lanes, channels, frames});
    });
    return ep;
}

WasapiOutputEndpoint::~WasapiOutputEndpoint() {
    if (stream_) stream_->stop();
}

std::string WasapiOutputEndpoint::channelName(std::uint32_t ch) const {
    if (channels_ == 1) return label_;
    return label_ + (ch == 0 ? " L" : ch == 1 ? " R" : " " + std::to_string(ch + 1));
}

void WasapiOutputEndpoint::push(ConstAudioBlock src) noexcept { ring_->write(src); }

const StreamInfo& WasapiOutputEndpoint::info() const noexcept { return stream_->info(); }

bool WasapiOutputEndpoint::deviceLost() const noexcept { return stream_->lost(); }

} // namespace afm::wasapi
