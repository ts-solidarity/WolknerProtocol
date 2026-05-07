#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

struct Region {
    uint64_t start;
    uint64_t end;
    std::string perms;
    std::string path;

    uint64_t size() const { return end - start; }
    bool readable() const { return perms.find('r') != std::string::npos; }
    bool writable() const { return perms.find('w') != std::string::npos; }
};

class ProcessMem {
public:
    ProcessMem();
    ~ProcessMem();

    static std::optional<int> findPidByName(const std::string& name);

    bool attach(int pid);
    void detach();
    bool isAttached() const;
    int pid() const { return pid_; }

    std::vector<Region> regions() const { return regions_; }
    std::vector<Region> readableRegions() const;
    std::vector<Region> writableRegions() const;

    // Read up to n bytes from addr. Returns actual bytes read (may be shorter).
    std::vector<uint8_t> read(uint64_t addr, size_t n) const;
    bool readInto(uint64_t addr, void* buf, size_t n) const;

    // Write n bytes. Returns true only on full successful write.
    bool write(uint64_t addr, const void* buf, size_t n);

    std::string lastError() const { return err_; }

private:
    bool loadMaps();

    int pid_ = -1;
    int fd_ = -1;
    std::vector<Region> regions_;
    mutable std::string err_;
};
