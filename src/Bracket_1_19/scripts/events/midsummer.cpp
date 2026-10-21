/*
 * This file is part of the AzerothCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Affero General Public License as published by the
 * Free Software Foundation; either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "GameEventMgr.h"
#include "GameObjectAI.h"
#include "Group.h"
#include "LFGMgr.h"
#include "Player.h"
#include "ScriptedGossip.h"
#include "ScriptMgr.h"

#include "ProgressionSystem.h"

enum Misc
{

    SPELL_SUMMONING_RHYME_AURA = 45926,
    ACTION_START_EVENT         = -2574500,
    QUEST_SUMMON_AHUNE         = 11691,
    ITEM_MAGMA_TOTEM           = 34953,
    DATA_AHUNE_BUNNY           = 5,
    DATA_LUMA_SKYMOTHER        = 16,
    SPELL_CHILLING_AURA        = 46542
};

class go_ahune_ice_stone_1_19 : public GameObjectAI
{
public:
    go_ahune_ice_stone_1_19(GameObject* go) : GameObjectAI(go), _instance(go->GetInstanceScript()) { }

    bool GossipSelect(Player* player, uint32 /*sender*/, uint32 /*action*/) override
    {
        if (!player || _started || !_instance)
            return true;

        if (!sConfigMgr->GetOption<bool>("ProgressionSystem.Bracket_1_19", false) || player->GetLevel() < 65 ||
            !IsHolidayActive(HOLIDAY_FIRE_FESTIVAL))
            return true;

        if (sConfigMgr->GetOption<bool>("ProgressionSystem.SharedPhaseGate.Enable", false))
        {
            ProgressionSystem::SharedPhaseSnapshot const phase = ProgressionSystem::GetSharedPhaseSnapshot();
            if (!phase.enabled || !phase.policyValid || phase.phaseId != "1_19" || phase.phaseLevelCeiling != 19)
                return true;
        }

        // Not allowed in RDF/LFG
        if (player->GetGroup() && player->GetGroup()->isLFGGroup())
        {
            return true;
        }

        // Only allow summoning him in heroic
        if (!me->GetMap()->IsHeroic())
        {
            return true;
        }

        Creature* ahuneBunny = _instance->GetCreature(DATA_AHUNE_BUNNY);
        if (!ahuneBunny || !ahuneBunny->IsAlive() || !ahuneBunny->AI())
            return true;

        // Admit the native event once before consuming quest items or crediting
        // the summon. The world-thread callback schedules the existing event.
        _started = true;
        ClearGossipMenuFor(player);
        ahuneBunny->AI()->DoAction(ACTION_START_EVENT);
        player->DestroyItemCount(ITEM_MAGMA_TOTEM, 1, true, false);
        player->AreaExploredOrEventHappens(QUEST_SUMMON_AHUNE); //auto rewarded

        if (Creature* luma = _instance->GetCreature(DATA_LUMA_SKYMOTHER))
            luma->CastSpell(player, SPELL_SUMMONING_RHYME_AURA, true);
        CloseGossipMenuFor(player);
        me->Delete();
        return true;
    }

private:
    InstanceScript* _instance;
    bool _started = false;
};

class global_midsummer_tuning_script : public GlobalScript
{
public:
    global_midsummer_tuning_script() : GlobalScript("global_midsummer_tuning_script") { }

    void OnLoadSpellCustomAttr(SpellInfo* spellInfo) override
    {
        if (spellInfo->Id == SPELL_CHILLING_AURA) // Chilling Aura
        {
            spellInfo->Effects[EFFECT_0].BasePoints = 1000;
        }
    }
};

void AddSC_event_midsummer_1_19()
{
    RegisterGameObjectAI(go_ahune_ice_stone_1_19);
    new global_midsummer_tuning_script();
}
