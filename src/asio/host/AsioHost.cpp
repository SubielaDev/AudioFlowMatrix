#include "AsioHost.h"

#include "afm/asio/AsioSampleConvert.h"
#include "afm/core/AudioTypes.h"
#include "afm/engine/AudioEngine.h"

#include <cmath>
#include <cstring>

namespace afm::asio {

namespace {

constexpr wchar_t kOwnDriverName[] = L"AudioFlow Matrix ASIO";

std::string driverError(IASIO* driver, const char* what) {
    char msg[128] = {};
    driver->getErrorMessage(msg);
    std::string s = what;
    if (msg[0] != '\0') {
        s += ": ";
        s += msg;
    }
    return s;
}

} // namespace

AsioHost* AsioHost::s_active = nullptr;

std::vector<DriverEntry> listDrivers() {
    std::vector<DriverEntry> drivers;
    HKEY root = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\ASIO", 0, KEY_READ | KEY_WOW64_64KEY, &root) != ERROR_SUCCESS) {
        return drivers;
    }
    wchar_t keyName[256];
    for (DWORD i = 0;; ++i) {
        DWORD len = 256;
        if (RegEnumKeyExW(root, i, keyName, &len, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) {
            break;
        }
        if (lstrcmpiW(keyName, kOwnDriverName) == 0) {
            continue;  // never host ourselves
        }
        wchar_t clsidText[64] = {};
        DWORD bytes = sizeof(clsidText);
        if (RegGetValueW(root, keyName, L"CLSID", RRF_RT_REG_SZ, nullptr, clsidText, &bytes) != ERROR_SUCCESS) {
            continue;
        }
        DriverEntry entry;
        if (FAILED(CLSIDFromString(clsidText, &entry.clsid))) {
            continue;
        }
        entry.name = win::toUtf8(keyName);
        drivers.push_back(std::move(entry));
    }
    RegCloseKey(root);
    return drivers;
}

AsioHost::~AsioHost() { unload(); }

bool AsioHost::load(const DriverEntry& entry, HWND window, double preferredSampleRate, std::string& error) {
    unload();

    IASIO* driver = nullptr;
    const HRESULT hr = CoCreateInstance(entry.clsid, nullptr, CLSCTX_INPROC_SERVER, entry.clsid,
                                        reinterpret_cast<void**>(&driver));
    if (FAILED(hr) || driver == nullptr) {
        error = "Could not instantiate ASIO driver '" + entry.name + "' (" + win::hresultMessage(hr) + ")";
        return false;
    }
    if (driver->init(window) != ASIOTrue) {
        error = driverError(driver, ("ASIO driver '" + entry.name + "' failed to initialise").c_str());
        driver->Release();
        return false;
    }
    driver_ = driver;
    name_ = entry.name;

    if (preferredSampleRate > 0.0) {
        ASIOSampleRate current = 0.0;
        driver_->getSampleRate(&current);
        if (std::fabs(current - preferredSampleRate) > 0.5 && driver_->canSampleRate(preferredSampleRate) == ASE_OK) {
            driver_->setSampleRate(preferredSampleRate);
        }
    }

    if (!readFormat(error)) {
        unload();
        return false;
    }
    return true;
}

bool AsioHost::readFormat(std::string& error) {
    long numIn = 0;
    long numOut = 0;
    if (driver_->getChannels(&numIn, &numOut) != ASE_OK) {
        error = driverError(driver_, "getChannels failed");
        return false;
    }
    long minSize = 0, maxSize = 0, preferred = 0, granularity = 0;
    if (driver_->getBufferSize(&minSize, &maxSize, &preferred, &granularity) != ASE_OK || preferred <= 0) {
        error = driverError(driver_, "getBufferSize failed");
        return false;
    }
    if (preferred > static_cast<long>(kMaxBlockFrames)) {
        error = "Driver buffer size " + std::to_string(preferred) + " exceeds the supported maximum of " +
                std::to_string(kMaxBlockFrames) + " samples";
        return false;
    }
    ASIOSampleRate rate = 0.0;
    if (driver_->getSampleRate(&rate) != ASE_OK || rate <= 0.0) {
        error = driverError(driver_, "getSampleRate failed");
        return false;
    }

    sampleRate_ = rate;
    blockFrames_ = static_cast<std::uint32_t>(preferred);

    inputs_.clear();
    outputs_.clear();
    auto readChannels = [&](long count, bool isInput, std::vector<ChannelDesc>& out) -> bool {
        for (long ch = 0; ch < count; ++ch) {
            ASIOChannelInfo info{};
            info.channel = ch;
            info.isInput = isInput ? ASIOTrue : ASIOFalse;
            if (driver_->getChannelInfo(&info) != ASE_OK) {
                error = driverError(driver_, "getChannelInfo failed");
                return false;
            }
            if (!isSupportedSampleType(info.type)) {
                error = "Unsupported ASIO sample type " + std::to_string(info.type);
                return false;
            }
            info.name[sizeof(info.name) - 1] = '\0';
            out.push_back({info.name, info.type});
        }
        return true;
    };
    if (!readChannels(numIn, true, inputs_) || !readChannels(numOut, false, outputs_)) {
        return false;
    }
    if (inputs_.size() > kMaxInputs || outputs_.size() > kMaxOutputs) {
        error = "Driver exposes more channels than supported";
        return false;
    }
    return true;
}

void AsioHost::unload() {
    stop();
    if (driver_ != nullptr) {
        driver_->Release();
        driver_ = nullptr;
    }
    name_.clear();
    inputs_.clear();
    outputs_.clear();
}

bool AsioHost::start(AudioEngine& engine, std::string& error) {
    if (driver_ == nullptr) {
        error = "No ASIO driver loaded";
        return false;
    }
    stop();

    const auto numIn = static_cast<long>(inputs_.size());
    const auto numOut = static_cast<long>(outputs_.size());
    const long total = numIn + numOut;

    bufferInfos_.assign(static_cast<std::size_t>(total), ASIOBufferInfo{});
    types_.assign(static_cast<std::size_t>(total), ASIOSTInt32LSB);
    for (long i = 0; i < total; ++i) {
        const bool isInput = i < numIn;
        bufferInfos_[i].isInput = isInput ? ASIOTrue : ASIOFalse;
        bufferInfos_[i].channelNum = isInput ? i : i - numIn;
        types_[i] = isInput ? inputs_[i].type : outputs_[i - numIn].type;
    }

    // All scratch memory is allocated here, never in the callback.
    scratch_ = AlignedBuffer<float, kSimdAlignment>(static_cast<std::size_t>(total) * blockFrames_);
    inLanes_.assign(static_cast<std::size_t>(numIn), nullptr);
    outLanes_.assign(static_cast<std::size_t>(numOut), nullptr);
    for (long i = 0; i < numIn; ++i) inLanes_[i] = scratch_.data() + static_cast<std::size_t>(i) * blockFrames_;
    for (long o = 0; o < numOut; ++o) outLanes_[o] = scratch_.data() + static_cast<std::size_t>(numIn + o) * blockFrames_;

    engine_ = &engine;
    s_active = this;
    callbackTable_.bufferSwitch = &AsioHost::onBufferSwitch;
    callbackTable_.sampleRateDidChange = &AsioHost::onSampleRateChanged;
    callbackTable_.asioMessage = &AsioHost::onAsioMessage;
    callbackTable_.bufferSwitchTimeInfo = &AsioHost::onBufferSwitchTimeInfo;

    if (driver_->createBuffers(bufferInfos_.data(), total, static_cast<long>(blockFrames_), &callbackTable_) != ASE_OK) {
        error = driverError(driver_, "createBuffers failed");
        s_active = nullptr;
        return false;
    }
    buffersCreated_ = true;

    driver_->getLatencies(&inputLatency_, &outputLatency_);
    postOutput_ = driver_->outputReady() == ASE_OK;

    if (driver_->start() != ASE_OK) {
        error = driverError(driver_, "ASIO start failed");
        stop();
        return false;
    }
    running_ = true;
    return true;
}

void AsioHost::stop() {
    if (driver_ == nullptr) {
        return;
    }
    if (running_) {
        driver_->stop();
        running_ = false;
    }
    if (buffersCreated_) {
        driver_->disposeBuffers();
        buffersCreated_ = false;
    }
    if (s_active == this) {
        s_active = nullptr;
    }
    engine_ = nullptr;
}

void AsioHost::openControlPanel() {
    if (driver_ != nullptr) {
        driver_->controlPanel();
    }
}

void AsioHost::process(long index) noexcept {
    const auto numIn = static_cast<long>(inLanes_.size());
    const auto numOut = static_cast<long>(outLanes_.size());
    const auto frames = static_cast<long>(blockFrames_);

    for (long i = 0; i < numIn; ++i) {
        toFloat(types_[i], bufferInfos_[i].buffers[index], inLanes_[i], frames);
    }

    engine_->process(ConstAudioBlock{inLanes_.data(), static_cast<std::uint32_t>(numIn), blockFrames_},
                     AudioBlock{outLanes_.data(), static_cast<std::uint32_t>(numOut), blockFrames_});

    for (long o = 0; o < numOut; ++o) {
        fromFloat(types_[numIn + o], outLanes_[o], bufferInfos_[numIn + o].buffers[index], frames);
    }
    if (postOutput_) {
        driver_->outputReady();
    }
    callbacks_.store(callbacks_.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
}

void AsioHost::onBufferSwitch(long index, ASIOBool) {
    if (AsioHost* self = s_active; self != nullptr && self->engine_ != nullptr) {
        self->process(index);
    }
}

ASIOTime* AsioHost::onBufferSwitchTimeInfo(ASIOTime* params, long index, ASIOBool directProcess) {
    onBufferSwitch(index, directProcess);
    return params;
}

void AsioHost::onSampleRateChanged(ASIOSampleRate) {
    if (AsioHost* self = s_active) {
        self->resetRequested_.store(true);
    }
}

long AsioHost::onAsioMessage(long selector, long value, void*, double*) {
    AsioHost* self = s_active;
    switch (selector) {
    case kAsioSelectorSupported:
        return (value == kAsioEngineVersion || value == kAsioResetRequest || value == kAsioBufferSizeChange ||
                value == kAsioResyncRequest || value == kAsioLatenciesChanged || value == kAsioSupportsTimeInfo ||
                value == kAsioOverload)
                   ? 1
                   : 0;
    case kAsioEngineVersion:
        return 2;
    case kAsioResetRequest:
    case kAsioBufferSizeChange:
        if (self != nullptr) self->resetRequested_.store(true);
        return 1;
    case kAsioResyncRequest:
    case kAsioLatenciesChanged:
        return 1;
    case kAsioSupportsTimeInfo:
        return 1;
    case kAsioOverload:
        if (self != nullptr) {
            self->overloads_.store(self->overloads_.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
        }
        return 0;
    default:
        return 0;
    }
}

} // namespace afm::asio
