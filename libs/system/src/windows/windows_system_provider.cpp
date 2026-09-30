#include "nexus/system/system_provider.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

// winsock2.h must precede windows.h; iphlpapi.h pulls in netioapi.h (GetIfTable2),
// whose declarations need the winsock address types.
// clang-format off
#include <winsock2.h>
#include <ws2ipdef.h>

#include <windows.h>

#include <iphlpapi.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <winternl.h>
// clang-format on

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace nexus::system {

namespace {

using u64 = std::uint64_t;

struct SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION_ {
    LARGE_INTEGER IdleTime;
    LARGE_INTEGER KernelTime; // includes IdleTime
    LARGE_INTEGER UserTime;
    LARGE_INTEGER DpcTime;
    LARGE_INTEGER InterruptTime;
    ULONG InterruptCount;
};

constexpr auto kSystemProcessorPerformanceInformation = static_cast<SYSTEM_INFORMATION_CLASS>(8);

u64 to_u64(FILETIME ft) {
    ULARGE_INTEGER v;
    v.LowPart = ft.dwLowDateTime;
    v.HighPart = ft.dwHighDateTime;
    return v.QuadPart;
}

u64 system_time_100ns() {
    FILETIME ft{};
    ::GetSystemTimeAsFileTime(&ft);
    return to_u64(ft);
}

std::string narrow(const wchar_t* text) {
    if (text == nullptr || text[0] == L'\0') {
        return {};
    }
    const int needed = ::WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (needed <= 1) {
        return {};
    }
    std::string out(static_cast<std::size_t>(needed - 1), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), needed, nullptr, nullptr);
    return out;
}

double ratio(u64 numerator, u64 denominator) {
    if (denominator == 0) {
        return 0.0;
    }
    return std::clamp(static_cast<double>(numerator) / static_cast<double>(denominator), 0.0, 1.0);
}

class WindowsSystemProvider final : public SystemProvider {
public:
    WindowsSystemProvider() {
        SYSTEM_INFO info{};
        ::GetSystemInfo(&info);
        core_count_ = info.dwNumberOfProcessors == 0 ? 1 : info.dwNumberOfProcessors;
    }

    CpuLoad cpu_load() override;
    MemoryStatus memory_status() override;
    std::vector<DiskInfo> disks() override;
    std::vector<NetInterfaceInfo> network_interfaces() override;
    std::vector<ProcessInfo> processes() override;
    BatteryStatus battery() override;

private:
    std::vector<double> per_core_load();

    DWORD core_count_ = 1;

    bool have_prev_cpu_ = false;
    u64 prev_idle_ = 0;
    u64 prev_kernel_ = 0;
    u64 prev_user_ = 0;

    struct CoreTimes {
        u64 idle = 0;
        u64 kernel = 0;
        u64 user = 0;
    };
    std::vector<CoreTimes> prev_core_;
    bool prev_core_valid_ = false;

    std::unordered_map<std::uint32_t, u64> prev_proc_total_;
    bool prev_proc_valid_ = false;
    u64 prev_proc_sample_time_ = 0;
};

CpuLoad WindowsSystemProvider::cpu_load() {
    CpuLoad out;

    FILETIME idle{};
    FILETIME kernel{};
    FILETIME user{};
    if (::GetSystemTimes(&idle, &kernel, &user) != 0) {
        const u64 i = to_u64(idle);
        const u64 k = to_u64(kernel);
        const u64 u = to_u64(user);
        if (have_prev_cpu_) {
            const u64 di = i - prev_idle_;
            const u64 total = (k - prev_kernel_) + (u - prev_user_);
            out.total = ratio(total > di ? total - di : 0, total);
        }
        prev_idle_ = i;
        prev_kernel_ = k;
        prev_user_ = u;
        have_prev_cpu_ = true;
    }

    out.per_core = per_core_load();
    return out;
}

std::vector<double> WindowsSystemProvider::per_core_load() {
    const std::size_t count = core_count_;
    std::vector<double> result(count, 0.0);

    std::vector<SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION_> buffer(count);
    ULONG returned = 0;
    const NTSTATUS status = ::NtQuerySystemInformation(
        kSystemProcessorPerformanceInformation, buffer.data(),
        static_cast<ULONG>(buffer.size() * sizeof(buffer[0])), &returned);
    if (status != 0) {
        return result;
    }

    if (prev_core_.size() != count) {
        prev_core_.assign(count, CoreTimes{});
        prev_core_valid_ = false;
    }

    for (std::size_t c = 0; c < count; ++c) {
        const u64 idle = static_cast<u64>(buffer[c].IdleTime.QuadPart);
        const u64 kernel = static_cast<u64>(buffer[c].KernelTime.QuadPart);
        const u64 user = static_cast<u64>(buffer[c].UserTime.QuadPart);
        if (prev_core_valid_) {
            const u64 di = idle - prev_core_[c].idle;
            const u64 total = (kernel - prev_core_[c].kernel) + (user - prev_core_[c].user);
            result[c] = ratio(total > di ? total - di : 0, total);
        }
        prev_core_[c] = CoreTimes{idle, kernel, user};
    }
    prev_core_valid_ = true;
    return result;
}

MemoryStatus WindowsSystemProvider::memory_status() {
    MEMORYSTATUSEX ms{};
    ms.dwLength = sizeof(ms);
    MemoryStatus out;
    if (::GlobalMemoryStatusEx(&ms) == 0) {
        return out;
    }
    out.total_bytes = ms.ullTotalPhys;
    out.available_bytes = ms.ullAvailPhys;
    out.used_bytes = ms.ullTotalPhys - ms.ullAvailPhys;
    out.used_fraction = ratio(out.used_bytes, ms.ullTotalPhys);
    out.commit_limit_bytes = ms.ullTotalPageFile;
    out.commit_used_bytes = ms.ullTotalPageFile - ms.ullAvailPageFile;
    return out;
}

std::vector<DiskInfo> WindowsSystemProvider::disks() {
    std::vector<DiskInfo> out;
    const DWORD mask = ::GetLogicalDrives();
    for (DWORD bit = 0; bit < 26; ++bit) {
        if ((mask & (1UL << bit)) == 0) {
            continue;
        }
        std::array<wchar_t, 4> root{static_cast<wchar_t>(L'A' + bit), L':', L'\\', L'\0'};
        const UINT type = ::GetDriveTypeW(root.data());
        if (type != DRIVE_FIXED && type != DRIVE_REMOTE) {
            continue;
        }
        ULARGE_INTEGER free_to_caller{};
        ULARGE_INTEGER total{};
        ULARGE_INTEGER free_total{};
        if (::GetDiskFreeSpaceExW(root.data(), &free_to_caller, &total, &free_total) == 0) {
            continue;
        }
        DiskInfo disk;
        disk.mount = narrow(root.data());
        std::array<wchar_t, MAX_PATH + 1> label{};
        if (::GetVolumeInformationW(root.data(), label.data(), MAX_PATH, nullptr, nullptr, nullptr,
                                    nullptr, 0) != 0) {
            disk.volume_label = narrow(label.data());
        }
        disk.total_bytes = total.QuadPart;
        disk.free_bytes = free_total.QuadPart;
        out.push_back(std::move(disk));
    }
    return out;
}

std::vector<NetInterfaceInfo> WindowsSystemProvider::network_interfaces() {
    std::vector<NetInterfaceInfo> out;
    PMIB_IF_TABLE2 table = nullptr;
    if (::GetIfTable2(&table) != NO_ERROR || table == nullptr) {
        return out;
    }
    for (ULONG i = 0; i < table->NumEntries; ++i) {
        const MIB_IF_ROW2& row = table->Table[i];
        if (row.Type == IF_TYPE_SOFTWARE_LOOPBACK) {
            continue;
        }
        NetInterfaceInfo iface;
        iface.name = narrow(row.Alias);
        iface.description = narrow(row.Description);
        iface.up = row.OperStatus == IfOperStatusUp;
        iface.bytes_sent = row.OutOctets;
        iface.bytes_received = row.InOctets;
        iface.link_speed_bps = row.TransmitLinkSpeed;
        out.push_back(std::move(iface));
    }
    ::FreeMibTable(table);
    return out;
}

std::vector<ProcessInfo> WindowsSystemProvider::processes() {
    std::vector<ProcessInfo> out;

    const HANDLE snapshot = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return out;
    }

    const u64 now = system_time_100ns();
    const u64 wall_delta = prev_proc_sample_time_ != 0 ? now - prev_proc_sample_time_ : 0;
    const u64 capacity = wall_delta * core_count_;

    std::unordered_map<std::uint32_t, u64> current;

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (::Process32FirstW(snapshot, &entry) != 0) {
        do {
            ProcessInfo info;
            info.pid = entry.th32ProcessID;
            info.name = narrow(entry.szExeFile);
            info.thread_count = entry.cntThreads;

            const HANDLE process =
                ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID);
            if (process != nullptr) {
                PROCESS_MEMORY_COUNTERS counters{};
                if (::GetProcessMemoryInfo(process, &counters, sizeof(counters)) != 0) {
                    info.working_set_bytes = counters.WorkingSetSize;
                }
                FILETIME created{};
                FILETIME exited{};
                FILETIME kernel{};
                FILETIME user{};
                if (::GetProcessTimes(process, &created, &exited, &kernel, &user) != 0) {
                    const u64 busy = to_u64(kernel) + to_u64(user);
                    current[info.pid] = busy;
                    if (prev_proc_valid_ && capacity > 0) {
                        const auto prev = prev_proc_total_.find(info.pid);
                        if (prev != prev_proc_total_.end() && busy >= prev->second) {
                            info.cpu_fraction = ratio(busy - prev->second, capacity);
                        }
                    }
                }
                ::CloseHandle(process);
            }
            out.push_back(std::move(info));
        } while (::Process32NextW(snapshot, &entry) != 0);
    }
    ::CloseHandle(snapshot);

    prev_proc_total_ = std::move(current);
    prev_proc_sample_time_ = now;
    prev_proc_valid_ = true;
    return out;
}

BatteryStatus WindowsSystemProvider::battery() {
    BatteryStatus out;
    SYSTEM_POWER_STATUS status{};
    if (::GetSystemPowerStatus(&status) == 0) {
        return out;
    }
    out.on_ac_power = status.ACLineStatus == 1;

    constexpr BYTE kNoBattery = 128;
    constexpr BYTE kCharging = 8;
    constexpr BYTE kUnknownFlag = 255;

    if (status.BatteryFlag != kUnknownFlag && (status.BatteryFlag & kNoBattery) == 0) {
        out.present = true;
        out.charging = (status.BatteryFlag & kCharging) != 0;
        if (status.BatteryLifePercent != 255) {
            out.charge_fraction =
                std::clamp(static_cast<double>(status.BatteryLifePercent) / 100.0, 0.0, 1.0);
        }
        if (status.BatteryLifeTime != 0xFFFFFFFFUL) {
            out.time_remaining = std::chrono::seconds(status.BatteryLifeTime);
        }
    }
    return out;
}

} // namespace

std::unique_ptr<SystemProvider> make_system_provider() {
    return std::make_unique<WindowsSystemProvider>();
}

} // namespace nexus::system
