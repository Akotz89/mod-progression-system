/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license: https://github.com/azerothcore/azerothcore-wotlk/blob/master/LICENSE-AGPL3
 */

#include <cstddef>

#include "CryptoHash.h"
#include "ProgressionSystem.h"
#include "Util.h"

#include <string>

namespace
{
constexpr char FIRST_PHASE_ID[] = "1_19";
constexpr uint8 FIRST_PHASE_LEVEL_CEILING = 19;

ProgressionSystem::SharedPhaseSnapshot CurrentSnapshot;

void RefreshSharedPhaseSnapshot()
{
    bool const loadScripts = sConfigMgr->GetOption<bool>("ProgressionSystem.LoadScripts", true);
    bool const loadDatabase = sConfigMgr->GetOption<bool>("ProgressionSystem.LoadDatabase", true);
    bool const gateEnabled = sConfigMgr->GetOption<bool>("ProgressionSystem.SharedPhaseGate.Enable", false);
    std::string const configuredPhase = sConfigMgr->GetOption<std::string>(
        "ProgressionSystem.SharedPhaseGate.CurrentPhase", "");

    std::string canonicalConfig = "schema=1\nload_scripts=" + std::to_string(loadScripts) +
        "\nload_database=" + std::to_string(loadDatabase) +
        "\nshared_phase_gate_enabled=" + std::to_string(gateEnabled) +
        "\nshared_phase_current_phase=" + configuredPhase + "\n";
    bool exactFirstPhaseProfile = true;

    for (std::string const& bracketName : ProgressionBracketsNames)
    {
        bool const bracketEnabled = sConfigMgr->GetOption<bool>(
            "ProgressionSystem.Bracket_" + bracketName, false);
        bool const bracketExpected = bracketName == "0" || bracketName == FIRST_PHASE_ID;

        canonicalConfig += "bracket_" + bracketName + "=" + std::to_string(bracketEnabled) + "\n";
        exactFirstPhaseProfile = exactFirstPhaseProfile && bracketEnabled == bracketExpected;
    }

    // A bracket selection is only an observation. The shared gate must be
    // explicitly enabled and name the phase it claims to enforce before any
    // action hook is permitted to rely on this snapshot.
    bool const exactGateProfile = gateEnabled && configuredPhase == FIRST_PHASE_ID;
    CurrentSnapshot.enabled = loadScripts && loadDatabase && gateEnabled;
    CurrentSnapshot.policyValid = CurrentSnapshot.enabled && exactGateProfile && exactFirstPhaseProfile;
    CurrentSnapshot.phaseId = FIRST_PHASE_ID;
    CurrentSnapshot.phaseLevelCeiling = CurrentSnapshot.policyValid ? FIRST_PHASE_LEVEL_CEILING : 0;
    CurrentSnapshot.policyConfigDigest = ByteArrayToHexStr(Acore::Crypto::SHA256::GetDigestOf(canonicalConfig));
}

class progression_system_shared_phase_snapshot_worldscript final : public WorldScript
{
public:
    progression_system_shared_phase_snapshot_worldscript()
        : WorldScript("progression_system_shared_phase_snapshot_worldscript")
    {
    }

    void OnAfterConfigLoad(bool /*reload*/) override
    {
        RefreshSharedPhaseSnapshot();
    }
};
}

ProgressionSystem::SharedPhaseSnapshot ProgressionSystem::GetSharedPhaseSnapshot()
{
    return CurrentSnapshot;
}

void AddSC_progression_system_shared_phase_snapshot()
{
    new progression_system_shared_phase_snapshot_worldscript();
}
