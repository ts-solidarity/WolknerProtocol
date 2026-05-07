#include "ProcessMem.h"

#include <cstring>
#include <cstdio>

#ifdef _WIN32
// ============== Windows backend (Win32 API) ==============
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>

// fd_ unused on Windows — we treat the HANDLE as the equivalent. Stash it in
// a member-shaped void* via reinterpret_cast through fd_'s storage. Simpler
// to add a parallel HANDLE member — but to avoid header churn we stick the
// HANDLE in fd_'s slot via casting. fd_ is an int, so we keep a separate
// global-style mapping. Cleaner: use a single static thread-local handle.
// Actually the cleanest is to add a HANDLE member. Doing that:
namespace { struct WinHandle { HANDLE h = nullptr; }; }
static WinHandle& winHandleOf(int fd) {
    static WinHandle h;
    (void)fd;
    return h;
}

ProcessMem::ProcessMem() = default;
ProcessMem::~ProcessMem() { detach(); }

// Convert wide string (Windows native) to UTF-8 std::string.
static std::string wcharToUtf8(const wchar_t* w) {
    if (!w) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string out((size_t)(len - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, out.data(), len, nullptr, nullptr);
    return out;
}

std::optional<int> ProcessMem::findPidByName(const std::string& name) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return std::nullopt;
    PROCESSENTRY32W pe{}; pe.dwSize = sizeof(pe);
    std::optional<int> out;
    if (Process32FirstW(snap, &pe)) {
        do {
            if (name == wcharToUtf8(pe.szExeFile)) {
                out = (int)pe.th32ProcessID;
                break;
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return out;
}

bool ProcessMem::attach(int pid) {
    detach();
    pid_ = pid;
    HANDLE h = OpenProcess(
        PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_VM_OPERATION |
            PROCESS_QUERY_INFORMATION,
        FALSE, (DWORD)pid);
    if (!h) {
        err_ = "OpenProcess failed: " + std::to_string(GetLastError());
        pid_ = -1;
        return false;
    }
    winHandleOf(0).h = h;
    fd_ = 1;  // marker for isAttached()
    if (!loadMaps()) { detach(); return false; }
    return true;
}

void ProcessMem::detach() {
    if (winHandleOf(0).h) { CloseHandle(winHandleOf(0).h); winHandleOf(0).h = nullptr; }
    fd_ = -1;
    pid_ = -1;
    regions_.clear();
}

bool ProcessMem::isAttached() const { return fd_ >= 0; }

std::vector<Region> ProcessMem::readableRegions() const {
    std::vector<Region> out;
    for (auto& r : regions_) if (r.readable()) out.push_back(r);
    return out;
}
std::vector<Region> ProcessMem::writableRegions() const {
    std::vector<Region> out;
    for (auto& r : regions_) if (r.writable()) out.push_back(r);
    return out;
}

bool ProcessMem::loadMaps() {
    regions_.clear();
    HANDLE h = winHandleOf(0).h;
    if (!h) return false;

    SYSTEM_INFO si{}; GetSystemInfo(&si);
    uintptr_t addr = (uintptr_t)si.lpMinimumApplicationAddress;
    uintptr_t end  = (uintptr_t)si.lpMaximumApplicationAddress;

    while (addr < end) {
        MEMORY_BASIC_INFORMATION mbi{};
        SIZE_T got = VirtualQueryEx(h, (LPCVOID)addr, &mbi, sizeof(mbi));
        if (got == 0) break;
        if (mbi.State == MEM_COMMIT) {
            DWORD p = mbi.Protect & 0xFF;
            bool readable =
                p == PAGE_READONLY || p == PAGE_READWRITE ||
                p == PAGE_EXECUTE_READ || p == PAGE_EXECUTE_READWRITE ||
                p == PAGE_WRITECOPY || p == PAGE_EXECUTE_WRITECOPY;
            bool writable =
                p == PAGE_READWRITE || p == PAGE_EXECUTE_READWRITE ||
                p == PAGE_WRITECOPY || p == PAGE_EXECUTE_WRITECOPY;
            bool guarded = (mbi.Protect & PAGE_GUARD) || (mbi.Protect & PAGE_NOACCESS);
            if (readable && !guarded) {
                Region r;
                r.start = (uint64_t)mbi.BaseAddress;
                r.end   = (uint64_t)mbi.BaseAddress + (uint64_t)mbi.RegionSize;
                r.perms = std::string(readable ? "r" : "-") + (writable ? "w" : "-");
                r.path  = "";
                regions_.push_back(r);
            }
        }
        if (mbi.RegionSize == 0) break;
        addr = (uintptr_t)mbi.BaseAddress + (uintptr_t)mbi.RegionSize;
    }
    return !regions_.empty();
}

std::vector<uint8_t> ProcessMem::read(uint64_t addr, size_t n) const {
    std::vector<uint8_t> buf(n);
    SIZE_T got = 0;
    if (!ReadProcessMemory(winHandleOf(0).h, (LPCVOID)addr, buf.data(), n, &got)) {
        err_ = "ReadProcessMemory: " + std::to_string(GetLastError());
        return {};
    }
    buf.resize(got);
    return buf;
}

bool ProcessMem::readInto(uint64_t addr, void* buf, size_t n) const {
    SIZE_T got = 0;
    if (!ReadProcessMemory(winHandleOf(0).h, (LPCVOID)addr, buf, n, &got)) return false;
    return got == n;
}

bool ProcessMem::write(uint64_t addr, const void* buf, size_t n) {
    SIZE_T wrote = 0;
    if (!WriteProcessMemory(winHandleOf(0).h, (LPVOID)addr, buf, n, &wrote)) {
        err_ = "WriteProcessMemory: " + std::to_string(GetLastError());
        return false;
    }
    return wrote == n;
}

#else
// ============== Linux backend (/proc/<pid>/mem + pread/pwrite) ==============
#include <cerrno>
#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#include <regex>
#include <sys/uio.h>
#include <unistd.h>

ProcessMem::ProcessMem() = default;

ProcessMem::~ProcessMem() { detach(); }

std::optional<int> ProcessMem::findPidByName(const std::string& name) {
    DIR* d = opendir("/proc");
    if (!d) return std::nullopt;
    std::optional<int> out;
    while (auto* e = readdir(d)) {
        if (!e->d_name[0] || e->d_name[0] < '0' || e->d_name[0] > '9') continue;
        std::string p = "/proc/";
        p += e->d_name;
        p += "/comm";
        std::ifstream f(p);
        std::string comm;
        if (f && std::getline(f, comm) && comm == name) {
            out = std::atoi(e->d_name);
            break;
        }
    }
    closedir(d);
    return out;
}

bool ProcessMem::attach(int pid) {
    detach();
    pid_ = pid;
    std::string path = "/proc/" + std::to_string(pid) + "/mem";
    fd_ = ::open(path.c_str(), O_RDWR);
    if (fd_ < 0) {
        err_ = "open " + path + ": " + std::strerror(errno);
        pid_ = -1;
        return false;
    }
    if (!loadMaps()) { detach(); return false; }
    return true;
}

void ProcessMem::detach() {
    if (fd_ >= 0) { ::close(fd_); fd_ = -1; }
    pid_ = -1;
    regions_.clear();
}

bool ProcessMem::isAttached() const { return fd_ >= 0; }

std::vector<Region> ProcessMem::readableRegions() const {
    std::vector<Region> out;
    for (auto& r : regions_) if (r.readable()) out.push_back(r);
    return out;
}

std::vector<Region> ProcessMem::writableRegions() const {
    std::vector<Region> out;
    for (auto& r : regions_) if (r.writable()) out.push_back(r);
    return out;
}

static const std::regex kMapsRe(
    R"(([0-9a-f]+)-([0-9a-f]+)\s+(\S+)\s+\S+\s+\S+\s+\S+\s*(.*))");

bool ProcessMem::loadMaps() {
    regions_.clear();
    std::ifstream f("/proc/" + std::to_string(pid_) + "/maps");
    if (!f) { err_ = "open maps failed"; return false; }
    std::string line;
    while (std::getline(f, line)) {
        std::smatch m;
        if (!std::regex_match(line, m, kMapsRe)) continue;
        Region r;
        r.start = std::stoull(m[1].str(), nullptr, 16);
        r.end = std::stoull(m[2].str(), nullptr, 16);
        r.perms = m[3].str();
        r.path = m[4].str();
        while (!r.path.empty() && (r.path.back() == ' ' || r.path.back() == '\t'))
            r.path.pop_back();
        if (r.path == "[vvar]" || r.path == "[vsyscall]" || r.path == "[vdso]") continue;
        if (r.start >= r.end) continue;
        regions_.push_back(r);
    }
    return true;
}

std::vector<uint8_t> ProcessMem::read(uint64_t addr, size_t n) const {
    std::vector<uint8_t> buf(n);
    ssize_t got = ::pread(fd_, buf.data(), n, (off_t)addr);
    if (got < 0) {
        err_ = std::string("pread: ") + std::strerror(errno);
        return {};
    }
    buf.resize(got);
    return buf;
}

bool ProcessMem::readInto(uint64_t addr, void* buf, size_t n) const {
    ssize_t got = ::pread(fd_, buf, n, (off_t)addr);
    return got == (ssize_t)n;
}

bool ProcessMem::write(uint64_t addr, const void* buf, size_t n) {
    ssize_t wrote = ::pwrite(fd_, buf, n, (off_t)addr);
    if (wrote != (ssize_t)n) {
        err_ = std::string("pwrite: ") + std::strerror(errno);
        return false;
    }
    return true;
}
#endif
