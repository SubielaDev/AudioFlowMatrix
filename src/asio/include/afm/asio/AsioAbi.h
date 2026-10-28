#pragma once

// Binary interface of Steinberg ASIO 2.x drivers on 64-bit Windows.
//
// This is an independent declaration of the driver ABI (COM-style vtable, structs and
// constants) so the project builds without the ASIO SDK. Layouts follow the SDK's
// 4-byte packing; `long` is 32 bits on Windows (LLP64) for both MSVC and MinGW.

#include <objbase.h>
#include <unknwn.h>

namespace afm::asio {

using ASIOBool = long;
using ASIOError = long;
using ASIOSampleRate = double;
using ASIOSampleType = long;

inline constexpr ASIOBool ASIOFalse = 0;
inline constexpr ASIOBool ASIOTrue = 1;

enum : ASIOError {
    ASE_OK = 0,
    ASE_SUCCESS = 0x3f4847a0,
    ASE_NotPresent = -1000,
    ASE_HWMalfunction,
    ASE_InvalidParameter,
    ASE_InvalidMode,
    ASE_SPNotAdvancing,
    ASE_NoClock,
    ASE_NoMemory,
};

enum : ASIOSampleType {
    ASIOSTInt16MSB = 0,
    ASIOSTInt24MSB = 1,
    ASIOSTInt32MSB = 2,
    ASIOSTFloat32MSB = 3,
    ASIOSTFloat64MSB = 4,
    ASIOSTInt32MSB16 = 8,
    ASIOSTInt32MSB18 = 9,
    ASIOSTInt32MSB20 = 10,
    ASIOSTInt32MSB24 = 11,
    ASIOSTInt16LSB = 16,
    ASIOSTInt24LSB = 17,
    ASIOSTInt32LSB = 18,
    ASIOSTFloat32LSB = 19,
    ASIOSTFloat64LSB = 20,
    ASIOSTInt32LSB16 = 24,
    ASIOSTInt32LSB18 = 25,
    ASIOSTInt32LSB20 = 26,
    ASIOSTInt32LSB24 = 27,
};

// asioMessage() selectors.
enum : long {
    kAsioSelectorSupported = 1,
    kAsioEngineVersion,
    kAsioResetRequest,
    kAsioBufferSizeChange,
    kAsioResyncRequest,
    kAsioLatenciesChanged,
    kAsioSupportsTimeInfo,
    kAsioSupportsTimeCode,
    kAsioMMCCommand,
    kAsioSupportsInputMonitor,
    kAsioSupportsInputGain,
    kAsioSupportsInputMeter,
    kAsioSupportsOutputGain,
    kAsioSupportsOutputMeter,
    kAsioOverload,
};

// AsioTimeInfo::flags
enum : unsigned long {
    kSystemTimeValid = 1u << 0,
    kSamplePositionValid = 1u << 1,
    kSampleRateValid = 1u << 2,
    kSpeedValid = 1u << 3,
    kSampleRateChanged = 1u << 4,
    kClockSourceChanged = 1u << 5,
};

#pragma pack(push, 4)

// 64-bit values split in two 32-bit halves, high word first.
struct ASIOSamples {
    unsigned long hi;
    unsigned long lo;
};

struct ASIOTimeStamp {
    unsigned long hi;
    unsigned long lo;
};

struct ASIOClockSource {
    long index;
    long associatedChannel;
    long associatedGroup;
    ASIOBool isCurrentSource;
    char name[32];
};

struct ASIOChannelInfo {
    long channel;
    ASIOBool isInput;
    ASIOBool isActive;
    long channelGroup;
    ASIOSampleType type;
    char name[32];
};

struct ASIOBufferInfo {
    ASIOBool isInput;
    long channelNum;
    void* buffers[2];
};

struct AsioTimeInfo {
    double speed;
    ASIOTimeStamp systemTime;
    ASIOSamples samplePosition;
    ASIOSampleRate sampleRate;
    unsigned long flags;
    char reserved[12];
};

struct ASIOTimeCode {
    double speed;
    ASIOSamples timeCodeSamples;
    unsigned long flags;
    char future[64];
};

struct ASIOTime {
    long reserved[4];
    AsioTimeInfo timeInfo;
    ASIOTimeCode timeCode;
};

struct ASIOCallbacks {
    void (*bufferSwitch)(long doubleBufferIndex, ASIOBool directProcess);
    void (*sampleRateDidChange)(ASIOSampleRate sRate);
    long (*asioMessage)(long selector, long value, void* message, double* opt);
    ASIOTime* (*bufferSwitchTimeInfo)(ASIOTime* params, long doubleBufferIndex, ASIOBool directProcess);
};

#pragma pack(pop)

static_assert(sizeof(ASIOChannelInfo) == 52);
static_assert(sizeof(ASIOBufferInfo) == 24);
static_assert(sizeof(AsioTimeInfo) == 48);

// The driver object. Hosts create it with CoCreateInstance(clsid, ..., clsid, &ptr):
// the driver's CLSID doubles as its interface id. No virtual destructor on purpose:
// it would add vtable slots and break the COM layout.
struct IASIO : public IUnknown {
    virtual ASIOBool init(void* sysHandle) = 0;
    virtual void getDriverName(char* name) = 0;  // up to 32 chars
    virtual long getDriverVersion() = 0;
    virtual void getErrorMessage(char* string) = 0;  // up to 124 chars
    virtual ASIOError start() = 0;
    virtual ASIOError stop() = 0;
    virtual ASIOError getChannels(long* numInputChannels, long* numOutputChannels) = 0;
    virtual ASIOError getLatencies(long* inputLatency, long* outputLatency) = 0;
    virtual ASIOError getBufferSize(long* minSize, long* maxSize, long* preferredSize, long* granularity) = 0;
    virtual ASIOError canSampleRate(ASIOSampleRate sampleRate) = 0;
    virtual ASIOError getSampleRate(ASIOSampleRate* sampleRate) = 0;
    virtual ASIOError setSampleRate(ASIOSampleRate sampleRate) = 0;
    virtual ASIOError getClockSources(ASIOClockSource* clocks, long* numSources) = 0;
    virtual ASIOError setClockSource(long reference) = 0;
    virtual ASIOError getSamplePosition(ASIOSamples* sPos, ASIOTimeStamp* tStamp) = 0;
    virtual ASIOError getChannelInfo(ASIOChannelInfo* info) = 0;
    virtual ASIOError createBuffers(ASIOBufferInfo* bufferInfos, long numChannels, long bufferSize,
                                    ASIOCallbacks* callbacks) = 0;
    virtual ASIOError disposeBuffers() = 0;
    virtual ASIOError controlPanel() = 0;
    virtual ASIOError future(long selector, void* opt) = 0;
    virtual ASIOError outputReady() = 0;
};

inline unsigned long long toUInt64(ASIOSamples s) noexcept {
    return (static_cast<unsigned long long>(s.hi) << 32) | s.lo;
}

inline ASIOSamples toAsioSamples(unsigned long long v) noexcept {
    return {static_cast<unsigned long>(v >> 32), static_cast<unsigned long>(v & 0xFFFFFFFFull)};
}

inline ASIOTimeStamp toAsioTimeStamp(unsigned long long nanoseconds) noexcept {
    return {static_cast<unsigned long>(nanoseconds >> 32), static_cast<unsigned long>(nanoseconds & 0xFFFFFFFFull)};
}

} // namespace afm::asio
