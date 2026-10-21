/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license: https://github.com/azerothcore/azerothcore-wotlk/blob/master/LICENSE-AGPL3
 */

#ifndef _PROGRESSION_SYSTEM_H_
#define _PROGRESSION_SYSTEM_H_

#include "Config.h"
#include "ScriptMgr.h"

#include <array>
#include <string>

namespace ProgressionSystem
{
struct SharedPhaseSnapshot
{
    bool enabled = false;
    bool policyValid = false;
    std::string phaseId;
    uint8 phaseLevelCeiling = 0;
    std::string policyConfigDigest;
};

// A read-only view of the exact upstream bracket configuration. The adapter
// does not authorize player actions and does not dispatch PlayerScript hooks.
[[nodiscard]] SharedPhaseSnapshot GetSharedPhaseSnapshot();
struct QuestSourcePolicyFacts
{
    bool templateKnown{false}, configured{false}, ready{false}, templateAllowed{false};
};
// Static source/template policy only; never an actor CanTake/Add/Reward result or script dispatch.
[[nodiscard]] QuestSourcePolicyFacts ReadQuestSourcePolicy(uint32 questId);
}

#define PROGRESSION_BRACKET_MAX 39
std::array<std::string, PROGRESSION_BRACKET_MAX> const ProgressionBracketsNames =
{
    "0",
    "1_19",
    "20_29",
    "30_39",
    "40_49",
    "50_59_1",
    "50_59_2",
    "60_1_1",
    "60_1_2",
    "60_2_1",
    "60_2_2",
    "60_3_1",
    "60_3_2",
    "60_3_3",
    "61_64",
    "65_69",
    "70_1_1",
    "70_1_2",
    "70_2_1",
    "70_2_2",
    "70_2_3",
    "70_3_1",
    "70_3_2",
    "70_4_1",
    "70_4_2",
    "70_5",
    "70_6_1",
    "70_6_2",
    "70_6_3",
    "71_74",
    "75_79",
    "80_1_1",
    "80_1_2",
    "80_1_3",
    "80_2",
    "80_3",
    "80_4_1",
    "80_4_2",
    "Custom"
};

#endif // _PROGRESSION_SYSTEM_H_
