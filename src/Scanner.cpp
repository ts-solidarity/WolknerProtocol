#include "Scanner.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <future>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {
constexpr size_t CHUNK = 16 * 1024 * 1024;
const std::array<const char*, 4> kPrefixes = {
    "BaseGame.", "RiziaDLC.", "GameCondition.", "SuzVar."
};

static std::vector<uint8_t> encodeUtf16LE(const std::string& s) {
    std::vector<uint8_t> out;
    out.reserve(s.size() * 2);
    for (char c : s) {
        out.push_back((uint8_t)c);
        out.push_back(0);
    }
    return out;
}
} // namespace

Scanner::Scanner(ProcessMem& mem, ProgressFn progress)
    : mem_(mem), progress_(std::move(progress)) {}

void Scanner::report(const std::string& msg) { if (progress_) progress_(msg); }

static void scanRegionForNeedle(const ProcessMem& mem, const Region& r,
                                 const std::vector<uint8_t>& needle,
                                 std::vector<uint64_t>& out) {
    std::vector<uint8_t> buf(CHUNK);
    uint64_t base = r.start;
    uint64_t remaining = r.size();
    while (remaining > 0) {
        size_t toRead = (size_t)std::min<uint64_t>(CHUNK, remaining);
        buf.resize(toRead);
        if (!mem.readInto(base, buf.data(), toRead)) break;
        const uint8_t* hay = buf.data();
        size_t haySize = toRead;
        const uint8_t* ned = needle.data();
        size_t nedSize = needle.size();
        const uint8_t* p = hay;
        while (haySize >= nedSize) {
            const void* found = std::memchr(p, ned[0], haySize - nedSize + 1);
            if (!found) break;
            const uint8_t* q = (const uint8_t*)found;
            if (std::memcmp(q, ned, nedSize) == 0) {
                out.push_back(base + (q - hay));
            }
            size_t advance = (q - p) + 1;
            p += advance;
            haySize -= advance;
        }
        base += toRead;
        remaining -= toRead;
    }
}

std::vector<uint64_t> Scanner::scanBytes(const std::vector<Region>& regions,
                                          const std::vector<uint8_t>& needle) {
    std::vector<uint64_t> hits;
    if (needle.empty()) return hits;

    // Parallelize across regions
    unsigned nThreads = std::max(1u, std::thread::hardware_concurrency());
    std::vector<std::vector<uint64_t>> per(regions.size());
    std::atomic<size_t> next(0);
    std::vector<std::thread> workers;
    for (unsigned t = 0; t < nThreads; ++t) {
        workers.emplace_back([&]{
            for (;;) {
                size_t i = next.fetch_add(1);
                if (i >= regions.size()) break;
                scanRegionForNeedle(mem_, regions[i], needle, per[i]);
            }
        });
    }
    for (auto& w : workers) w.join();
    for (auto& v : per) hits.insert(hits.end(), v.begin(), v.end());
    return hits;
}

std::optional<std::string> Scanner::readCsharpString(uint64_t addr, int maxChars) const {
    uint8_t hdr[20];
    if (!mem_.readInto(addr, hdr, 20)) return std::nullopt;
    int32_t length;
    std::memcpy(&length, hdr + 16, 4);
    if (length < 0 || length > maxChars) return std::nullopt;
    std::vector<uint8_t> chars(length * 2);
    if (!mem_.readInto(addr + 20, chars.data(), chars.size())) return std::nullopt;
    // Convert UTF-16LE → UTF-8 (ASCII-only fast path; our keys are ASCII)
    std::string out;
    out.reserve(length);
    for (int i = 0; i < length; ++i) {
        uint16_t wc = chars[i*2] | (chars[i*2+1] << 8);
        if (wc >= 0x20 && wc < 0x80) out.push_back((char)wc);
        else return std::nullopt;  // keys are ASCII-only; bail
    }
    return out;
}

std::optional<uint64_t> Scanner::detectLuaNumberTypePtr() {
    report("Detecting LuaNumber class pointer...");
    auto wregions = mem_.writableRegions();
    std::unordered_map<uint64_t, int> counts;
    std::vector<uint8_t> buf(CHUNK);
    size_t seen = 0;
    const size_t seenCap = 200000;

    for (const auto& r : wregions) {
        if (seen > seenCap) break;
        uint64_t base = r.start;
        uint64_t remaining = r.size();
        while (remaining > 0 && seen < seenCap) {
            size_t toRead = (size_t)std::min<uint64_t>(CHUNK, remaining);
            buf.resize(toRead);
            if (!mem_.readInto(base, buf.data(), toRead)) break;
            // Walk 8-byte aligned positions, offset >= 16 so we can read preceding type_ptr
            for (size_t off = 16; off + 8 <= toRead; off += 8) {
                double d;
                std::memcpy(&d, buf.data() + off, 8);
                if (!std::isfinite(d) || d == 0.0) continue;
                if (std::fabs(d) > 1e6) continue;
                double frac;
                double ipart;
                frac = std::modf(d, &ipart);
                if (frac != 0.0) continue;
                uint64_t tp;
                std::memcpy(&tp, buf.data() + off - 16, 8);
                if (tp < 0x100000 || tp > 0x0000800000000000ULL) continue;
                counts[tp]++;
                seen++;
                if (seen >= seenCap) break;
            }
            base += toRead;
            remaining -= toRead;
        }
    }
    if (counts.empty()) return std::nullopt;
    auto it = std::max_element(counts.begin(), counts.end(),
                               [](const auto& a, const auto& b){ return a.second < b.second; });
    return it->first;
}

static bool hasKnownPrefix(const std::string& s) {
    for (const char* p : kPrefixes) {
        size_t plen = std::strlen(p);
        if (s.size() >= plen && s.compare(0, plen, p) == 0) return true;
    }
    return false;
}

std::unordered_map<std::string, VariableEntry>
Scanner::enumerate(uint64_t luaNumberTypePtr) {
    auto wregions = mem_.writableRegions();

    // Single pass approach:
    //   Pass 1 over writable memory: for each 8-byte position P,
    //     if mem[P:P+8] == luaNumberTypePtr → P is a LuaNumber object address.
    //   Collect set S of LuaNumber addrs.
    //   Pass 2 over writable memory: for each 8-byte position P,
    //     if mem[P:P+8] ∈ S → P is a dict value slot.
    //     Read mem[P-8:P] as key_ptr, resolve to a string, keep if prefix matches.
    //
    // Both passes parallelized across regions.

    report("Finding LuaNumber objects...");
    std::vector<uint64_t> luaNumberAddrs;
    {
        std::mutex mtx;
        unsigned nThreads = std::max(1u, std::thread::hardware_concurrency());
        std::atomic<size_t> next(0);
        std::vector<std::thread> workers;
        std::vector<std::vector<uint64_t>> per(wregions.size());
        for (unsigned t = 0; t < nThreads; ++t) {
            workers.emplace_back([&]{
                std::vector<uint8_t> buf(CHUNK);
                for (;;) {
                    size_t i = next.fetch_add(1);
                    if (i >= wregions.size()) break;
                    const auto& r = wregions[i];
                    uint64_t base = r.start;
                    uint64_t remaining = r.size();
                    auto& local = per[i];
                    while (remaining > 0) {
                        size_t toRead = (size_t)std::min<uint64_t>(CHUNK, remaining);
                        buf.resize(toRead);
                        if (!mem_.readInto(base, buf.data(), toRead)) break;
                        size_t aligned = toRead & ~(size_t)7;
                        for (size_t off = 0; off < aligned; off += 8) {
                            uint64_t v;
                            std::memcpy(&v, buf.data() + off, 8);
                            if (v == luaNumberTypePtr) local.push_back(base + off);
                        }
                        base += toRead;
                        remaining -= toRead;
                    }
                }
            });
        }
        for (auto& w : workers) w.join();
        for (auto& v : per) luaNumberAddrs.insert(luaNumberAddrs.end(), v.begin(), v.end());
    }
    std::unordered_set<uint64_t> luaNumberSet(luaNumberAddrs.begin(), luaNumberAddrs.end());
    report("Found " + std::to_string(luaNumberSet.size()) + " LuaNumber object candidates.");

    // Pass 2: find dict value slots that point to a LuaNumber object.
    report("Scanning for dict entries...");
    // Each candidate: (keyPtr, valueSlotAddr). The value slot is the address
    // *of the pointer* that points to the LuaNumber — stable across value
    // changes; the LuaNumber pointer itself isn't.
    std::vector<std::pair<uint64_t, uint64_t>> candidates;
    {
        std::mutex mtx;
        unsigned nThreads = std::max(1u, std::thread::hardware_concurrency());
        std::atomic<size_t> next(0);
        std::vector<std::thread> workers;
        for (unsigned t = 0; t < nThreads; ++t) {
            workers.emplace_back([&]{
                std::vector<uint8_t> buf(CHUNK);
                std::vector<std::pair<uint64_t,uint64_t>> local;
                for (;;) {
                    size_t i = next.fetch_add(1);
                    if (i >= wregions.size()) break;
                    const auto& r = wregions[i];
                    uint64_t base = r.start;
                    uint64_t remaining = r.size();
                    while (remaining > 0) {
                        size_t toRead = (size_t)std::min<uint64_t>(CHUNK, remaining);
                        buf.resize(toRead);
                        if (!mem_.readInto(base, buf.data(), toRead)) break;
                        size_t aligned = toRead & ~(size_t)7;
                        for (size_t off = 8; off + 8 <= aligned; off += 8) {
                            uint64_t v;
                            std::memcpy(&v, buf.data() + off, 8);
                            if (!luaNumberSet.count(v)) continue;
                            uint64_t k;
                            std::memcpy(&k, buf.data() + off - 8, 8);
                            if (k < 0x100000 || k > 0x0000800000000000ULL) continue;
                            uint64_t valueSlotAddr = base + off;  // where v is stored
                            local.emplace_back(k, valueSlotAddr);
                        }
                        base += toRead;
                        remaining -= toRead;
                    }
                }
                std::lock_guard<std::mutex> g(mtx);
                candidates.insert(candidates.end(), local.begin(), local.end());
            });
        }
        for (auto& w : workers) w.join();
    }
    report("Found " + std::to_string(candidates.size()) + " candidate dict entries.");

    // Resolve each candidate's key string. The key_ptr may point to:
    //   (a) a System.String directly (length at +16, chars at +20)
    //   (b) a LuaString wrapper (has Text pointer at +16 → System.String)
    // Cache resolutions by key_ptr; it's common to see the same key many times.
    std::unordered_map<std::string, VariableEntry> out;
    std::unordered_map<uint64_t, std::optional<std::string>> keyCache;

    auto resolveKey = [&](uint64_t keyPtr) -> std::optional<std::string> {
        auto it = keyCache.find(keyPtr);
        if (it != keyCache.end()) return it->second;
        // Try as System.String first
        auto direct = readCsharpString(keyPtr);
        if (direct && hasKnownPrefix(*direct)) { keyCache[keyPtr] = direct; return direct; }
        // Try LuaString wrapper: read ptr at keyPtr+16
        uint64_t inner;
        if (mem_.readInto(keyPtr + 16, &inner, 8) && inner >= 0x1000 && inner < 0x0000800000000000ULL) {
            auto via = readCsharpString(inner);
            if (via && hasKnownPrefix(*via)) { keyCache[keyPtr] = via; return via; }
        }
        keyCache[keyPtr] = std::nullopt;
        return std::nullopt;
    };

    for (auto& [keyPtr, valueSlotAddr] : candidates) {
        auto name = resolveKey(keyPtr);
        if (!name) continue;
        if (out.find(*name) != out.end()) continue;
        VariableEntry e;
        e.name = *name;
        e.valueSlotAddr = valueSlotAddr;
        out[*name] = e;
    }
    report("Discovered " + std::to_string(out.size()) + " live numeric variables.");
    return out;
}

double Scanner::readValue(const VariableEntry& e) const {
    // Two-hop read: value slot → current LuaNumber → double.
    uint64_t luaPtr = 0;
    if (!mem_.readInto(e.valueSlotAddr, &luaPtr, 8)) return std::nan("");
    if (luaPtr < 0x10000) return std::nan("");
    double d = 0.0;
    if (!mem_.readInto(luaPtr + 16, &d, 8)) return std::nan("");
    return d;
}

bool Scanner::writeValue(const VariableEntry& e, double v) {
    uint64_t luaPtr = 0;
    if (!mem_.readInto(e.valueSlotAddr, &luaPtr, 8)) return false;
    if (luaPtr < 0x10000) return false;
    return mem_.write(luaPtr + 16, &v, 8);
}
