#include "afm/midi/MidiInput.h"

#include "afm/platform/Win.h"

#include <windows.h>

#include <mmsystem.h>

namespace afm::midi {

namespace {

std::string mmError(MMRESULT r) {
    wchar_t text[MAXERRORLENGTH] = {};
    midiInGetErrorTextW(r, text, MAXERRORLENGTH);
    return win::toUtf8(text) + " (MMRESULT " + std::to_string(r) + ")";
}

} // namespace

std::vector<MidiInput::Device> MidiInput::list() {
    std::vector<Device> devices;
    const UINT count = midiInGetNumDevs();
    for (UINT id = 0; id < count; ++id) {
        MIDIINCAPSW caps{};
        if (midiInGetDevCapsW(id, &caps, sizeof(caps)) == MMSYSERR_NOERROR) {
            devices.push_back({id, win::toUtf8(caps.szPname)});
        }
    }
    return devices;
}

bool MidiInput::open(const Device& device, Handler handler, void* user, std::string& error) {
    close();
    handler_ = handler;
    user_ = user;
    HMIDIIN handle = nullptr;
    MMRESULT r = midiInOpen(&handle, device.id, reinterpret_cast<DWORD_PTR>(&MidiInput::callback),
                            reinterpret_cast<DWORD_PTR>(this), CALLBACK_FUNCTION);
    if (r != MMSYSERR_NOERROR) {
        error = "Could not open MIDI input '" + device.name + "': " + mmError(r);
        return false;
    }
    r = midiInStart(handle);
    if (r != MMSYSERR_NOERROR) {
        midiInClose(handle);
        error = "Could not start MIDI input '" + device.name + "': " + mmError(r);
        return false;
    }
    handle_ = handle;
    name_ = device.name;
    return true;
}

void MidiInput::close() {
    if (handle_ == nullptr) return;
    auto* handle = static_cast<HMIDIIN>(handle_);
    midiInStop(handle);
    midiInReset(handle);
    midiInClose(handle);
    handle_ = nullptr;
    name_.clear();
}

void __stdcall MidiInput::callback(void*, unsigned msg, std::uintptr_t instance, std::uintptr_t param1,
                                   std::uintptr_t param2) {
    if (msg != MIM_DATA) return;  // open/close notifications, SysEx and errors are ignored
    const auto* self = reinterpret_cast<const MidiInput*>(instance);
    if (self->handler_ != nullptr) {
        self->handler_(self->user_, static_cast<std::uint32_t>(param1), static_cast<std::uint32_t>(param2));
    }
}

} // namespace afm::midi
