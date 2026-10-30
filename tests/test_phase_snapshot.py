"""Exercise the production snapshot callback and concurrent readers on guarded WSL."""
import fcntl
import os
from pathlib import Path
import shutil
import stat
import subprocess
import tempfile
import unittest

MODULE = Path(__file__).resolve().parents[1]
PROJECT = Path(os.environ["WOW_PROJECT_ROOT"]).resolve(strict=True)


class PhaseSnapshotTest(unittest.TestCase):
    def test_actual_callback_and_readers(self):
        subprocess.run([str(PROJECT / "scripts/wow-build-host-guard")], check=True)
        lock_fd = os.open("/var/lib/wow-worker/build.lock", os.O_RDONLY | os.O_NOFOLLOW | os.O_CLOEXEC)
        self.addCleanup(os.close, lock_fd)
        self.assertTrue(stat.S_ISREG(os.fstat(lock_fd).st_mode))
        fcntl.flock(lock_fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
        with tempfile.TemporaryDirectory(prefix="phase-snapshot-", dir=PROJECT / "work") as raw:
            work = Path(raw)
            include = work / "include"
            module = work / "module"
            include.mkdir()
            module.mkdir()
            for name in ("SharedPhaseSnapshot.cpp", "ProgressionSystem.h"):
                shutil.copyfile(MODULE / "src" / name, module / name)
            (include / "Config.h").write_text(r'''#pragma once
#include <string>
#include <unordered_map>
#include <cstdint>
using uint8 = std::uint8_t;
using uint32 = std::uint32_t;
using uint64 = std::uint64_t;
class ConfigMgr {
public:
    std::unordered_map<std::string, std::string> values;
    template<class T> T GetOption(std::string const& key, T fallback) const {
        auto it = values.find(key);
        if (it == values.end()) return fallback;
        if constexpr (std::is_same_v<T, bool>) return it->second == "true";
        else return it->second;
    }
};
extern ConfigMgr* sConfigMgr;
''')
            (include / "ScriptMgr.h").write_text(r'''#pragma once
#include <string>
class WorldScript {
public:
    explicit WorldScript(char const*) {}
    virtual ~WorldScript() = default;
    virtual void OnAfterConfigLoad(bool) {}
};
''')
            (include / "CryptoHash.h").write_text(r'''#pragma once
#include <array>
#include <cstdint>
#include <string>
namespace Acore::Crypto {
struct SHA256 {
    static std::array<std::uint8_t, 32> GetDigestOf(std::string const& value) {
        std::array<std::uint8_t, 32> out{};
        for (std::size_t i = 0; i < value.size(); ++i) out[i % out.size()] ^= static_cast<std::uint8_t>(value[i]);
        return out;
    }
};
}
''')
            (include / "Util.h").write_text(r'''#pragma once
#include <array>
#include <cstdint>
#include <string>
template<class T, std::size_t N> std::string ByteArrayToHexStr(std::array<T, N> const& bytes) {
    static constexpr char hex[] = "0123456789abcdef";
    std::string result; result.reserve(N * 2);
    for (T value : bytes) { auto b = static_cast<std::uint8_t>(value); result += hex[b >> 4]; result += hex[b & 15]; }
    return result;
}
''')
            (work / "main.cpp").write_text(r'''#include <type_traits>
#include <thread>
#include <atomic>
#include <vector>
#include <chrono>
#include "Config.h"
ConfigMgr config;
ConfigMgr* sConfigMgr = &config;
#include "SharedPhaseSnapshot.cpp"
#include <cassert>
int main() {
    for (auto const& name : ProgressionBracketsNames)
        config.values["ProgressionSystem.Bracket_" + name] = (name == "0" || name == "1_19") ? "true" : "false";
    config.values["ProgressionSystem.LoadScripts"] = "true";
    config.values["ProgressionSystem.LoadDatabase"] = "true";
    config.values["ProgressionSystem.SharedPhaseGate.Enable"] = "true";
    config.values["ProgressionSystem.SharedPhaseGate.CurrentPhase"] = "1_19";
    progression_system_shared_phase_snapshot_worldscript script;
    assert(ProgressionSystem::GetSharedPhaseSnapshot().revision == 0);
    script.OnAfterConfigLoad(false);
    auto a1 = ProgressionSystem::GetSharedPhaseSnapshot();
    assert(a1.enabled && a1.policyValid && a1.phaseLevelCeiling == 19 && a1.revision == 1);
    script.OnAfterConfigLoad(true);
    auto same = ProgressionSystem::GetSharedPhaseSnapshot();
    assert(same.revision == 2 && same.policyConfigDigest == a1.policyConfigDigest);
    config.values["ProgressionSystem.SharedPhaseGate.CurrentPhase"] = "invalid";
    script.OnAfterConfigLoad(true);
    auto invalid = ProgressionSystem::GetSharedPhaseSnapshot();
    assert(invalid.enabled && !invalid.policyValid && invalid.phaseLevelCeiling == 0 && invalid.revision == 3);
    config.values["ProgressionSystem.SharedPhaseGate.CurrentPhase"] = "1_19";
    script.OnAfterConfigLoad(true);
    auto a2 = ProgressionSystem::GetSharedPhaseSnapshot();
    assert(a2.enabled && a2.policyValid && a2.phaseLevelCeiling == 19 && a2.revision == 4);
    assert(a1.phaseId == a2.phaseId && a1.policyConfigDigest == a2.policyConfigDigest);
    assert(a1.revision != a2.revision);
    assert(a1.policyConfigDigest != invalid.policyConfigDigest);
    std::atomic<bool> start{false}, done{false};
    std::atomic<int> ready{0};
    std::atomic<int> observations[4]{};
    std::vector<std::thread> readers;
    for (int i = 0; i < 4; ++i) readers.emplace_back([&, i] {
        ++ready;
        while (!start.load()) std::this_thread::yield();
        do {
            auto snapshot = ProgressionSystem::GetSharedPhaseSnapshot();
            assert(snapshot.enabled && snapshot.revision >= 4 && snapshot.phaseId == "1_19");
            if (snapshot.policyValid) {
                assert(snapshot.phaseLevelCeiling == 19 && snapshot.policyConfigDigest == a1.policyConfigDigest);
            } else {
                assert(snapshot.phaseLevelCeiling == 0 && snapshot.policyConfigDigest == invalid.policyConfigDigest);
            }
            if (snapshot.revision > 4) ++observations[i];
        } while (!done.load());
    });
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (ready.load() != 4 && std::chrono::steady_clock::now() < deadline) std::this_thread::yield();
    assert(ready.load() == 4);
    start.store(true);
    int published = 0;
    bool observed = false;
    do {
        config.values["ProgressionSystem.SharedPhaseGate.CurrentPhase"] = (published % 2) ? "1_19" : "invalid";
        script.OnAfterConfigLoad(true);
        ++published;
        observed = true;
        for (auto& count : observations) observed = observed && count.load() > 0;
        std::this_thread::yield();
    } while ((published < 1000 || !observed) && std::chrono::steady_clock::now() < deadline);
    assert(published >= 1000 && observed);
    done.store(true);
    for (auto& reader : readers) reader.join();
    assert(ProgressionSystem::GetSharedPhaseSnapshot().revision == 4 + published);

}
''')
            binary = work / "phase-snapshot-test"
            subprocess.run(["c++", "-std=c++17", "-pthread", "-I", str(include), "-I", str(module),
                            str(work / "main.cpp"), "-o", str(binary)], check=True, timeout=120)
            subprocess.run([str(binary)], check=True, timeout=30)


if __name__ == "__main__":
    unittest.main()
