#include "nexus/module/storage/recycle.hpp"

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <objbase.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <string>
#include <windows.h>

namespace nexus::module::storage {

namespace {

std::string narrow(const std::filesystem::path& path) {
    const std::u8string bytes = path.u8string();
    return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

struct ComScope {
    HRESULT hr = E_FAIL;
    bool owned = false;

    ComScope() {
        hr = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        owned = SUCCEEDED(hr);
        if (hr == RPC_E_CHANGED_MODE) {
            hr = S_OK; // already initialised on this thread in another mode
        }
    }
    ~ComScope() {
        if (owned) {
            ::CoUninitialize();
        }
    }
    ComScope(const ComScope&) = delete;
    ComScope& operator=(const ComScope&) = delete;
};

} // namespace

RecycleResult recycle_to_bin(std::span<const std::filesystem::path> paths) {
    RecycleResult result;
    result.requested = paths.size();
    if (paths.empty()) {
        return result;
    }

    const ComScope com;
    if (FAILED(com.hr)) {
        result.error = "CoInitializeEx failed";
        for (const auto& p : paths) {
            result.failed.push_back(narrow(p));
        }
        return result;
    }

    IFileOperation* op = nullptr;
    HRESULT hr = ::CoCreateInstance(CLSID_FileOperation, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&op));
    if (FAILED(hr) || op == nullptr) {
        result.error = "IFileOperation unavailable";
        for (const auto& p : paths) {
            result.failed.push_back(narrow(p));
        }
        return result;
    }

    op->SetOperationFlags(FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOFX_RECYCLEONDELETE |
                          FOFX_EARLYFAILURE);

    std::vector<std::size_t> queued;
    for (std::size_t i = 0; i < paths.size(); ++i) {
        const std::wstring native = paths[i].wstring();
        IShellItem* item = nullptr;
        hr = ::SHCreateItemFromParsingName(native.c_str(), nullptr, IID_PPV_ARGS(&item));
        if (FAILED(hr) || item == nullptr) {
            result.failed.push_back(narrow(paths[i]));
            continue;
        }
        hr = op->DeleteItem(item, nullptr);
        item->Release();
        if (FAILED(hr)) {
            result.failed.push_back(narrow(paths[i]));
        } else {
            queued.push_back(i);
        }
    }

    if (!queued.empty()) {
        hr = op->PerformOperations();
        BOOL aborted = FALSE;
        op->GetAnyOperationsAborted(&aborted);
        if (FAILED(hr) || aborted) {
            // Can't tell which items survived; treat the queued set as failed.
            for (const std::size_t i : queued) {
                result.failed.push_back(narrow(paths[i]));
            }
        } else {
            result.recycled = queued.size();
        }
    }

    op->Release();
    return result;
}

} // namespace nexus::module::storage

#else // !_WIN32

namespace nexus::module::storage {

RecycleResult recycle_to_bin(std::span<const std::filesystem::path> paths) {
    RecycleResult result;
    result.requested = paths.size();
    result.error = "recycle bin not implemented on this platform";
    for (const auto& p : paths) {
        result.failed.push_back(p.generic_string());
    }
    return result;
}

} // namespace nexus::module::storage

#endif
