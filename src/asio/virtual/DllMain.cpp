// COM in-process server plumbing for the virtual ASIO driver: class factory, DLL
// exports and (un)registration. Registration needs administrator rights:
//   regsvr32 AudioFlowMatrixASIO.dll

#include "VirtualAsioDriver.h"

#include <olectl.h>

#include <new>
#include <string>

namespace {

HMODULE g_module = nullptr;
std::atomic<long> g_lockCount{0};

using afm::vasio::VirtualAsioDriver;

class ClassFactory final : public IClassFactory {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override {
        if (object == nullptr) return E_POINTER;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IClassFactory)) {
            *object = static_cast<IClassFactory*>(this);
            return S_OK;
        }
        *object = nullptr;
        return E_NOINTERFACE;
    }
    // Static lifetime: reference counting is a no-op.
    ULONG STDMETHODCALLTYPE AddRef() override { return 2; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }

    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* outer, REFIID riid, void** object) override {
        if (object == nullptr) return E_POINTER;
        *object = nullptr;
        if (outer != nullptr) return CLASS_E_NOAGGREGATION;
        auto* driver = new (std::nothrow) VirtualAsioDriver();
        if (driver == nullptr) return E_OUTOFMEMORY;
        const HRESULT hr = driver->QueryInterface(riid, object);
        driver->Release();
        return hr;
    }

    HRESULT STDMETHODCALLTYPE LockServer(BOOL lock) override {
        if (lock) g_lockCount.fetch_add(1);
        else g_lockCount.fetch_sub(1);
        return S_OK;
    }
};

ClassFactory g_factory;

std::wstring clsidString() {
    wchar_t buf[64] = {};
    StringFromGUID2(afm::vasio::kClsid, buf, 64);
    return buf;
}

LSTATUS setString(HKEY root, const std::wstring& subKey, const wchar_t* valueName, const std::wstring& value) {
    HKEY key = nullptr;
    LSTATUS rc = RegCreateKeyExW(root, subKey.c_str(), 0, nullptr, 0, KEY_WRITE | KEY_WOW64_64KEY, nullptr, &key,
                                 nullptr);
    if (rc != ERROR_SUCCESS) return rc;
    rc = RegSetValueExW(key, valueName, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()),
                        static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
    return rc;
}

const std::wstring kAsioKey = L"SOFTWARE\\ASIO\\AudioFlow Matrix ASIO";

std::wstring clsidKey() { return L"SOFTWARE\\Classes\\CLSID\\" + clsidString(); }

} // namespace

extern "C" BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = instance;
        DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID* object) {
    if (object == nullptr) return E_POINTER;
    *object = nullptr;
    if (!IsEqualCLSID(rclsid, afm::vasio::kClsid)) return CLASS_E_CLASSNOTAVAILABLE;
    return g_factory.QueryInterface(riid, object);
}

STDAPI DllCanUnloadNow() {
    return (afm::vasio::g_objectCount.load() == 0 && g_lockCount.load() == 0) ? S_OK : S_FALSE;
}

STDAPI DllRegisterServer() {
    wchar_t path[MAX_PATH] = {};
    if (GetModuleFileNameW(g_module, path, MAX_PATH) == 0) return SELFREG_E_CLASS;

    const std::wstring name = L"AudioFlow Matrix ASIO";
    if (setString(HKEY_LOCAL_MACHINE, clsidKey(), nullptr, name) != ERROR_SUCCESS ||
        setString(HKEY_LOCAL_MACHINE, clsidKey() + L"\\InprocServer32", nullptr, path) != ERROR_SUCCESS ||
        setString(HKEY_LOCAL_MACHINE, clsidKey() + L"\\InprocServer32", L"ThreadingModel", L"Apartment") !=
            ERROR_SUCCESS ||
        setString(HKEY_LOCAL_MACHINE, kAsioKey, L"CLSID", clsidString()) != ERROR_SUCCESS ||
        setString(HKEY_LOCAL_MACHINE, kAsioKey, L"Description", name) != ERROR_SUCCESS) {
        return SELFREG_E_CLASS;
    }
    return S_OK;
}

STDAPI DllUnregisterServer() {
    RegDeleteKeyExW(HKEY_LOCAL_MACHINE, kAsioKey.c_str(), KEY_WOW64_64KEY, 0);
    RegDeleteTreeW(HKEY_LOCAL_MACHINE, clsidKey().c_str());
    return S_OK;
}
