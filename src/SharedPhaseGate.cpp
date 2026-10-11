/*
 * Copyright (C) 2016+ AzerothCore <https://www.azerothcore.org>
 *
 * The bracket SQL controls which world data is loaded.  This script is the
 * action-side authority for the first shared phase: every player on the realm
 * is held at level 19 and future maps, quests, spells, items, dungeons and
 * battlegrounds are denied consistently.  The gate deliberately fails closed
 * when it is explicitly configured but the exact snapshot is not valid.
 */

#include "Config.h"
#include "AreaDefines.h"
#include "AuctionHouseMgr.h"
#include "Item.h"
#include "ItemTemplate.h"
#include "LFGMgr.h"
#include "MapMgr.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "ProgressionSystem.h"
#include "QuestDef.h"
#include "ScriptMgr.h"
#include "SpellInfo.h"
#include "SpellMgr.h"

#include <algorithm>
#include <set>

namespace
{
constexpr uint8 FIRST_PHASE_LEVEL_CEILING = 19;
constexpr uint32 FIRST_PHASE_PROFESSION_CEILING = 150;
constexpr uint32 MAP_DEEPRUN_TRAM = 369;

bool IsGateConfigured()
{
    return sConfigMgr->GetOption<bool>("ProgressionSystem.SharedPhaseGate.Enable", false);
}

bool HasAdminBypass(Player const* player)
{
    // Playerbot accounts use a non-player account type for their service
    // account.  Never let that implementation detail bypass the shared gate.
    return player && player->IsGameMaster() && player->GetSession() && !player->GetSession()->IsBot() &&
           sConfigMgr->GetOption<bool>("ProgressionSystem.SharedPhaseGate.AdminBypass", false);
}

bool HasAdminBypass(AccountTypes security, bool isBot)
{
    return !isBot && security >= SEC_MODERATOR &&
           sConfigMgr->GetOption<bool>("ProgressionSystem.SharedPhaseGate.AdminBypass", false);
}

bool IsPrivileged(Player const* player) { return HasAdminBypass(player); }

bool IsPrivileged(AccountTypes security, bool isBot) { return HasAdminBypass(security, isBot); }

bool IsGateReady()
{
    if (!IsGateConfigured())
        return true;

    ProgressionSystem::SharedPhaseSnapshot const snapshot = ProgressionSystem::GetSharedPhaseSnapshot();
    return snapshot.enabled && snapshot.policyValid && snapshot.phaseId == "1_19" &&
           snapshot.phaseLevelCeiling == FIRST_PHASE_LEVEL_CEILING;
}

bool IsAllowedOutlandStarterZone(uint32 zoneId)
{
    switch (zoneId)
    {
        case AREA_EVERSONG_WOODS:
        case AREA_GHOSTLANDS:
        case AREA_SILVERMOON_CITY:
        case AREA_AZUREMYST_ISLE:
        case AREA_BLOODMYST_ISLE:
        case AREA_THE_EXODAR:
            return true;
        default:
            return false;
    }
}

bool IsAllowedPhaseMap(uint32 mapId)
{
    switch (mapId)
    {
        // The two classic continents and the transport map are always part of
        // the first phase.
        case MAP_EASTERN_KINGDOMS:
        case MAP_KALIMDOR:
        case MAP_DEEPRUN_TRAM:

        // The first five classic dungeons are the only instance maps loaded by
        // Bracket_1_19 in the exact upstream progression SQL.
        case MAP_SHADOWFANG_KEEP:
        case MAP_DEADMINES:
        case MAP_WAILING_CAVERNS:
        case MAP_BLACKFATHOM_DEEPS:
        case MAP_RAGEFIRE_CHASM:

        // Warsong Gulch is the only battleground admitted by the phase policy.
        case MAP_WARSONG_GULCH:
            return true;

        // The TBC map is only used for the two racial starter regions.  A zone
        // is required so a generic map-530 teleport can never become a bypass.
        case MAP_OUTLAND:
            return true;
        default:
            return false;
    }
}

bool IsAllowedPhaseDestination(uint32 mapId, float x, float y, float z)
{
    if (!IsAllowedPhaseMap(mapId))
        return false;

    if (mapId != MAP_OUTLAND)
        return true;

    uint32 const zoneId = sMapMgr->GetZoneId(PHASEMASK_NORMAL, mapId, x, y, z);
    return zoneId != 0 && IsAllowedOutlandStarterZone(zoneId);
}

bool IsAllowedItem(ItemTemplate const* item)
{
    if (!item)
        return false;

    if (item->RequiredLevel > FIRST_PHASE_LEVEL_CEILING ||
        item->RequiredSkillRank > FIRST_PHASE_PROFESSION_CEILING)
        return false;

    // ItemTemplate::Map is zero for most ordinary items.  When it is set, it
    // is a useful provenance fence for expansion/dungeon-only item templates.
    return !item->Map || (item->Map != MAP_OUTLAND && IsAllowedPhaseMap(item->Map));
}

bool IsAllowedSpell(uint32 spellId)
{
    if (!spellId)
        return true;

    SpellInfo const* spell = sSpellMgr->GetSpellInfo(spellId);
    if (!spell || spell->SpellLevel > FIRST_PHASE_LEVEL_CEILING ||
        spell->BaseLevel > FIRST_PHASE_LEVEL_CEILING)
        return false;

    SkillLineAbilityMapBounds bounds = sSpellMgr->GetSkillLineAbilityMapBounds(spellId);
    for (auto itr = bounds.first; itr != bounds.second; ++itr)
        if (itr->second && itr->second->MinSkillLineRank > FIRST_PHASE_PROFESSION_CEILING)
            return false;

    return true;
}

bool IsAllowedQuest(Quest const* quest)
{
    if (!quest || quest->HasFlag(QUEST_FLAGS_TBC | QUEST_FLAGS_RAID) ||
        quest->HasFlag(QUEST_FLAGS_UNAVAILABLE) || quest->GetMinLevel() > FIRST_PHASE_LEVEL_CEILING ||
        quest->GetQuestLevel() > FIRST_PHASE_LEVEL_CEILING)
        return false;

    auto allowedItem = [](uint32 itemId)
    {
        return !itemId || IsAllowedItem(sObjectMgr->GetItemTemplate(itemId));
    };

    for (uint32 itemId : quest->ItemDrop)
        if (!allowedItem(itemId))
            return false;
    for (uint32 itemId : quest->RewardChoiceItemId)
        if (!allowedItem(itemId))
            return false;
    for (uint32 itemId : quest->RewardItemId)
        if (!allowedItem(itemId))
            return false;

    if ((quest->GetSrcItemId() && !IsAllowedItem(sObjectMgr->GetItemTemplate(quest->GetSrcItemId()))) ||
        !IsAllowedSpell(quest->GetSrcSpell()) || !IsAllowedSpell(quest->GetRewSpell()))
        return false;

    int32 rewardSpell = quest->GetRewSpellCast();
    return rewardSpell <= 0 || IsAllowedSpell(static_cast<uint32>(rewardSpell));
}

class progression_system_shared_phase_gate final : public PlayerScript
{
public:
    progression_system_shared_phase_gate()
        : PlayerScript("progression_system_shared_phase_gate",
        {
            PLAYERHOOK_GET_PROGRESSION_LEVEL_CEILING,
            PLAYERHOOK_CAN_PROGRESS,
            PLAYERHOOK_CAN_LEARN_PROGRESSION_SPELL,
            PLAYERHOOK_CAN_LOAD_FROM_DB,
            PLAYERHOOK_ON_BEFORE_TELEPORT,
            PLAYERHOOK_CAN_STORE_PROGRESSION_ITEM,
            PLAYERHOOK_CAN_PLACE_AUCTION_BID,
            PLAYERHOOK_CAN_CREATE_PROGRESSION_AUCTION,
            PLAYERHOOK_CAN_JOIN_IN_BATTLEGROUND_QUEUE,
            PLAYERHOOK_CAN_JOIN_IN_ARENA_QUEUE,
            PLAYERHOOK_CAN_BATTLEFIELD_PORT,
            PLAYERHOOK_CAN_SELL_ITEM,
            PLAYERHOOK_CAN_SEND_MAIL,
            PLAYERHOOK_CAN_JOIN_LFG,
            PLAYERHOOK_CAN_ENTER_MAP,
            PLAYERHOOK_CAN_INIT_TRADE,
            PLAYERHOOK_CAN_SET_TRADE_ITEM,
            PLAYERHOOK_CAN_ACCEPT_QUEST,
            PLAYERHOOK_CAN_PROGRESS_QUEST_OBJECTIVE,
            PLAYERHOOK_CAN_REWARD_QUEST,
            PLAYERHOOK_ON_BEFORE_QUEST_COMPLETE,
            PLAYERHOOK_ON_CAN_UPDATE_SKILL,
            PLAYERHOOK_GET_PROGRESSION_SKILL_CEILING,
            PLAYERHOOK_CAN_SET_PROGRESSION_SKILL,
            PLAYERHOOK_CAN_EQUIP_ITEM,
            PLAYERHOOK_CAN_USE_ITEM,
            PLAYERHOOK_CAN_SAVE_EQUIP_NEW_ITEM,
            PLAYERHOOK_CAN_AREA_EXPLORE_AND_OUTDOOR
        })
    {
    }

    void OnPlayerGetProgressionLevelCeiling(Player* player, uint8& ceiling) override
    {
        if (!IsGateConfigured() || IsPrivileged(player))
            return;

        ceiling = IsGateReady() ? std::min(ceiling, FIRST_PHASE_LEVEL_CEILING) : 0;
    }

    bool OnPlayerCanProgress(Player* player) override
    {
        return !IsGateConfigured() || IsPrivileged(player) || IsGateReady();
    }

    bool OnPlayerCanLearnProgressionSpell(Player* player, uint32 spellId) override
    {
        if (!IsGateConfigured() || IsPrivileged(player))
            return true;
        return IsGateReady() && IsAllowedSpell(spellId);
    }

    bool OnPlayerCanLoadFromDB(ObjectGuid /*guid*/, std::string const& /*name*/, uint8 /*race*/, uint8 classId,
        uint8 level, uint32 mapId, float x, float y, float z, AccountTypes security, bool isBot) override
    {
        if (!IsGateConfigured() || IsPrivileged(security, isBot))
            return true;
        if (!IsGateReady() || level > FIRST_PHASE_LEVEL_CEILING || classId == CLASS_DEATH_KNIGHT)
            return false;
        return IsAllowedPhaseDestination(mapId, x, y, z);
    }

    bool OnPlayerBeforeTeleport(Player* player, uint32 mapId, float x, float y, float z, float /*orientation*/,
        uint32 /*options*/, Unit* /*target*/) override
    {
        return !IsGateConfigured() || IsPrivileged(player) ||
               (IsGateReady() && IsAllowedPhaseDestination(mapId, x, y, z));
    }

    bool OnPlayerCanStoreProgressionItem(Player* player, ItemTemplate const* item) override
    {
        return !IsGateConfigured() || IsPrivileged(player) ||
               (IsGateReady() && IsAllowedItem(item));
    }

    bool OnPlayerCanPlaceAuctionBid(Player* player, AuctionEntry* auction) override
    {
        return !IsGateConfigured() || IsPrivileged(player) ||
               (IsGateReady() && auction && IsAllowedItem(sObjectMgr->GetItemTemplate(auction->item_template)));
    }

    bool OnPlayerCanCreateProgressionAuction(Player* player, Item* item) override
    {
        return !IsGateConfigured() || IsPrivileged(player) ||
               (IsGateReady() && item && IsAllowedItem(item->GetTemplate()));
    }

    bool OnPlayerCanJoinInBattlegroundQueue(Player* player, ObjectGuid /*battlemasterGuid*/,
        BattlegroundTypeId bgTypeId, uint8 /*joinAsGroup*/, GroupJoinBattlegroundResult& /*err*/) override
    {
        return !IsGateConfigured() || IsPrivileged(player) ||
               (IsGateReady() && bgTypeId == BATTLEGROUND_WS);
    }

    bool OnPlayerCanJoinInArenaQueue(Player* player, ObjectGuid /*battlemasterGuid*/, uint8 /*arenaSlot*/,
        BattlegroundTypeId /*bgTypeId*/, uint8 /*joinAsGroup*/, uint8 /*isRated*/,
        GroupJoinBattlegroundResult& /*err*/) override
    {
        return !IsGateConfigured() || IsPrivileged(player);
    }

    bool OnPlayerCanBattleFieldPort(Player* player, uint8 /*arenaType*/, BattlegroundTypeId bgTypeId,
        uint8 /*action*/) override
    {
        return !IsGateConfigured() || IsPrivileged(player) ||
               (IsGateReady() && bgTypeId == BATTLEGROUND_WS);
    }

    bool OnPlayerCanSellItem(Player* player, Item* item, Creature* /*creature*/) override
    {
        return !IsGateConfigured() || IsPrivileged(player) ||
               (IsGateReady() && item && IsAllowedItem(item->GetTemplate()));
    }

    bool OnPlayerCanSendMail(Player* player, ObjectGuid /*receiverGuid*/, ObjectGuid /*mailbox*/,
        std::string& /*subject*/, std::string& /*body*/, uint32 /*money*/, uint32 /*cod*/, Item* item) override
    {
        return !IsGateConfigured() || IsPrivileged(player) ||
               (IsGateReady() && (!item || IsAllowedItem(item->GetTemplate())));
    }

    bool OnPlayerCanJoinLfg(Player* player, uint8 /*roles*/, std::set<uint32>& dungeons,
        std::string const& /*comment*/) override
    {
        if (!IsGateConfigured() || IsPrivileged(player))
            return true;
        if (!IsGateReady())
            return false;

        for (auto itr = dungeons.begin(); itr != dungeons.end();)
        {
            lfg::LFGDungeonData const* dungeon = sLFGMgr->GetLFGDungeon(*itr);
            if (!dungeon || !IsAllowedPhaseMap(dungeon->map))
                itr = dungeons.erase(itr);
            else
                ++itr;
        }
        return !dungeons.empty();
    }

    bool OnPlayerCanEnterMap(Player* player, MapEntry const* entry, InstanceTemplate const* /*instance*/,
        MapDifficulty const* /*mapDiff*/, bool /*loginCheck*/) override
    {
        if (!IsGateConfigured() || IsPrivileged(player))
            return true;
        if (!IsGateReady())
            return false;
        if (entry && IsAllowedPhaseMap(entry->MapID))
            return true;
        return false;
    }

    bool OnPlayerCanInitTrade(Player* player, Player* /*target*/) override
    {
        return !IsGateConfigured() || IsPrivileged(player) || IsGateReady();
    }

    bool OnPlayerCanSetTradeItem(Player* player, Item* tradedItem, uint8 /*tradeSlot*/) override
    {
        return !IsGateConfigured() || IsPrivileged(player) ||
               (IsGateReady() && tradedItem && IsAllowedItem(tradedItem->GetTemplate()));
    }

    bool OnPlayerCanAcceptQuest(Player* player, Quest const* quest) override
    {
        return !IsGateConfigured() || IsPrivileged(player) ||
               (IsGateReady() && player && player->GetLevel() <= FIRST_PHASE_LEVEL_CEILING && IsAllowedQuest(quest));
    }

    bool OnPlayerCanProgressQuestObjective(Player* player, Quest const* quest) override
    {
        return !IsGateConfigured() || IsPrivileged(player) ||
               (IsGateReady() && player && player->GetLevel() <= FIRST_PHASE_LEVEL_CEILING && IsAllowedQuest(quest));
    }

    bool OnPlayerCanRewardQuest(Player* player, Quest const* quest) override
    {
        return !IsGateConfigured() || IsPrivileged(player) ||
               (IsGateReady() && player && player->GetLevel() <= FIRST_PHASE_LEVEL_CEILING && IsAllowedQuest(quest));
    }

    bool OnPlayerBeforeQuestComplete(Player* player, uint32 questId) override
    {
        Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
        return OnPlayerCanProgressQuestObjective(player, quest);
    }

    bool OnPlayerCanUpdateSkill(Player* player, uint32 /*skillId*/) override
    {
        return !IsGateConfigured() || IsPrivileged(player) || IsGateReady();
    }

    void OnPlayerGetProgressionSkillCeiling(Player* player, uint32 skillId, uint32& ceiling) override
    {
        if (!IsGateConfigured() || IsPrivileged(player))
            return;

        if (!IsGateReady())
            ceiling = 0;
        else if (IsProfessionSkill(skillId))
            ceiling = std::min(ceiling, FIRST_PHASE_PROFESSION_CEILING);
    }

    bool OnPlayerCanSetProgressionSkill(Player* player, uint32 skillId, uint32 value, uint32 /*max*/,
        uint32 /*step*/) override
    {
        return !IsGateConfigured() || IsPrivileged(player) ||
               (IsGateReady() && (!IsProfessionSkill(skillId) || value <= FIRST_PHASE_PROFESSION_CEILING));
    }

    bool OnPlayerCanEquipItem(Player* player, uint8 /*slot*/, uint16& /*dest*/, Item* item, bool /*swap*/,
        bool /*notLoading*/) override
    {
        return !IsGateConfigured() || IsPrivileged(player) ||
               (IsGateReady() && item && IsAllowedItem(item->GetTemplate()));
    }

    bool OnPlayerCanUseItem(Player* player, ItemTemplate const* proto, InventoryResult& /*result*/) override
    {
        return !IsGateConfigured() || IsPrivileged(player) ||
               (IsGateReady() && IsAllowedItem(proto));
    }

    bool OnPlayerCanSaveEquipNewItem(Player* player, Item* item, uint16 /*pos*/, bool /*update*/) override
    {
        return !IsGateConfigured() || IsPrivileged(player) ||
               (IsGateReady() && item && IsAllowedItem(item->GetTemplate()));
    }

    bool OnPlayerCanAreaExploreAndOutdoor(Player* player) override
    {
        return !IsGateConfigured() || IsPrivileged(player) || IsGateReady();
    }
};
}

void AddSC_progression_system_shared_phase_gate()
{
    new progression_system_shared_phase_gate();
}
