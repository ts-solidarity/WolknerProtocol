#pragma once

#include "ProcessMem.h"

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

struct VariableEntry {
    std::string name;
    // Address of the dict entry's value slot — i.e., the 8-byte location that
    // holds the pointer to the current LuaNumber. Stable across game state
    // changes (Boehm GC is non-moving and the dict's entry array doesn't move).
    // Reading the value takes two hops: (1) read 8 bytes at valueSlotAddr to
    // get the current LuaNumber address, (2) read 8 bytes at LuaNumber+16.
    uint64_t valueSlotAddr = 0;
};

class Scanner {
public:
    using ProgressFn = std::function<void(const std::string&)>;

    explicit Scanner(ProcessMem& mem, ProgressFn progress = {});

    // Detect the Language.Lua.LuaNumber type pointer by clustering heap candidates
    // that look like [type_ptr, monitor, double small-integer].
    std::optional<uint64_t> detectLuaNumberTypePtr();

    // Enumerate every live variable: dict entries where key→System.String|LuaString
    // with a known prefix, and value→LuaNumber.
    std::unordered_map<std::string, VariableEntry> enumerate(uint64_t luaNumberTypePtr);

    double readValue(const VariableEntry& e) const;
    bool writeValue(const VariableEntry& e, double v);

private:
    ProcessMem& mem_;
    ProgressFn progress_;
    void report(const std::string& msg);

    // Helpers
    std::vector<uint64_t> scanBytes(const std::vector<Region>& regions,
                                    const std::vector<uint8_t>& needle);
    std::optional<std::string> readCsharpString(uint64_t addr, int maxChars = 128) const;
};
