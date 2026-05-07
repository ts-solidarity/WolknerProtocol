// Standalone CLI to validate Scanner without the Qt GUI.
#include "ProcessMem.h"
#include "Scanner.h"

#include <chrono>
#include <cstdio>
#include <string>

int main() {
    auto pid = ProcessMem::findPidByName("Suzerain.exe");
    if (!pid) { std::printf("Suzerain.exe not running\n"); return 1; }
    std::printf("PID %d\n", *pid);

    ProcessMem mem;
    if (!mem.attach(*pid)) {
        std::printf("attach failed: %s\n", mem.lastError().c_str());
        return 2;
    }

    auto t0 = std::chrono::steady_clock::now();
    Scanner scanner(mem, [](const std::string& m){ std::printf("  %s\n", m.c_str()); });

    auto tp = scanner.detectLuaNumberTypePtr();
    if (!tp) { std::printf("no luanumber tp\n"); return 3; }
    std::printf("LuaNumber type_ptr = 0x%lx\n", (unsigned long)*tp);

    auto vars = scanner.enumerate(*tp);
    auto t1 = std::chrono::steady_clock::now();
    double secs = std::chrono::duration<double>(t1 - t0).count();
    std::printf("\nDiscovered %zu variables in %.2f s\n", vars.size(), secs);

    // Show a few known keys
    for (auto& k : {"BaseGame.GovernmentBudget", "BaseGame.PersonalWealth",
                    "BaseGame.Sordland_HUDStat_GovernmentBudget_Max",
                    "BaseGame.Country_Unrest", "BaseGame.Economy"}) {
        auto it = vars.find(k);
        if (it == vars.end()) std::printf("  %-60s = NOT FOUND\n", k);
        else std::printf("  %-60s = %g\n", k, scanner.readValue(it->second));
    }
    return 0;
}
