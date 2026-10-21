/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license: https://github.com/azerothcore/azerothcore-wotlk/blob/master/LICENSE-AGPL3
 */

#include "ProgressionSystem.h"
#include "DBUpdater.h"
#include "Tokenize.h"
#include "StringConvert.h"
#include "ProgressionUpdateManifest.h"
#include "Field.h"

inline std::vector<std::string> GetDatabaseDirectories(std::string const& folderName)
{
    std::vector<std::string> directories;

    std::string const path = "/modules/mod-progression-system/src/Bracket_";
    for (std::string const& bracketName : ProgressionBracketsNames)
    {
        if (!(sConfigMgr->GetOption<bool>("ProgressionSystem.Bracket_" + bracketName, false)))
        {
            continue;
        }

        std::string bracketPath = path + bracketName + "/sql/" + folderName;
        // A database directory is expected only when it is part of the
        // compiled SQL plan. Its absence on disk is rejected by the updater.
        for (auto const& file : ProgressionManifest::Files)
            if (("/" + file.path).compare(0, bracketPath.size() + 1, bracketPath + "/") == 0)
            {
                directories.push_back(std::move(bracketPath));
                break;
            }
    }

    return directories;
}

class ProgressionSystemLoadingDBUpdates : public DatabaseScript
{
public:
    ProgressionSystemLoadingDBUpdates() : DatabaseScript("ProgressionSystemLoadingDBUpdates") {}

    void OnAfterDatabasesLoaded(uint32 updateFlags) override
    {
        // Registration is unconditional: disabling loading/updates cannot hide
        // an ambiguous partial import left by an earlier startup.
        if (!StrictProgression::StartupEnabled(
            [](std::string const& database, char const* sql) -> std::optional<uint64>
            {
                QueryResult result;
                if (database == "auth") result = LoginDatabase.Query(sql);
                else if (database == "characters") result = CharacterDatabase.Query(sql);
                else result = WorldDatabase.Query(sql);
                if (!result || result->GetRowCount() != 1) return std::nullopt;
                return result->Fetch()[0].Get<uint64>();
            }, [] { return sConfigMgr->GetOption<bool>("ProgressionSystem.LoadDatabase", true); }))
            return;

        LOG_INFO("server.server", "Loading mod-progression-system updates...");

        // ReapplyUpdates is never an authorization to delete/replay history,
        // including when configured as zero. Strict hashes are always checked.
        if (sConfigMgr->GetOption<bool>("ProgressionSystem.ReapplyUpdates", false))
            throw UpdateException("ProgressionSystem.ReapplyUpdates is unsafe; use an audited restore/adoption instead");

        // Validate the entire optional list before any updater or deletion runs.
        std::vector<uint32> attunements;
        std::string disabled = sConfigMgr->GetOption<std::string>("ProgressionSystem.DisabledAttunements", "");
        if (!disabled.empty())
        {
            if (!DBUpdater<WorldDatabaseConnection>::IsEnabled(updateFlags))
                throw UpdateException("DisabledAttunements requires world updates enabled");
            if (disabled.front() == ',' || disabled.back() == ',' || disabled.find(",,") != std::string::npos)
                throw UpdateException("Malformed DisabledAttunements list");
            for (auto const& token : Acore::Tokenize(disabled, ',', false))
            {
                std::string text(token);
                if (text.empty() || text.find_first_not_of("0123456789") != std::string::npos)
                    throw UpdateException("Malformed DisabledAttunements ID");
                auto id = Acore::StringTo<uint32>(text);
                if (!id || !*id)
                    throw UpdateException("Invalid DisabledAttunements ID");
                attunements.push_back(*id);
            }
        }
        bool adoption = sConfigMgr->GetOption<bool>("ProgressionSystem.AuditedLegacyAdoption", false);
        if (DBUpdater<LoginDatabaseConnection>::IsEnabled(updateFlags))
        {
            auto dirs = GetDatabaseDirectories("auth");
            if (!DBUpdater<LoginDatabaseConnection>::UpdateProgression(LoginDatabase, dirs,
                ProgressionManifest::Files, ProgressionManifest::NativeIdentity, adoption))
                throw UpdateException("Strict progression auth update failed");
        }
        if (DBUpdater<CharacterDatabaseConnection>::IsEnabled(updateFlags))
        {
            auto dirs = GetDatabaseDirectories("characters");
            if (!DBUpdater<CharacterDatabaseConnection>::UpdateProgression(CharacterDatabase, dirs,
                ProgressionManifest::Files, ProgressionManifest::NativeIdentity, adoption))
                throw UpdateException("Strict progression characters update failed");
        }
        if (DBUpdater<WorldDatabaseConnection>::IsEnabled(updateFlags))
        {
            auto dirs = GetDatabaseDirectories("world");
            if (!DBUpdater<WorldDatabaseConnection>::UpdateProgression(WorldDatabase, dirs,
                ProgressionManifest::Files, ProgressionManifest::NativeIdentity, adoption))
                throw UpdateException("Strict progression world update failed");
            for (uint32 id : attunements)
            {
                WorldDatabase.DirectExecute("DELETE FROM dungeon_access_requirements WHERE dungeon_access_id = {}", id);
                auto check = WorldDatabase.Query("SELECT COUNT(*) FROM dungeon_access_requirements WHERE dungeon_access_id = {}", id);
                if (!check || check->Fetch()[0].Get<uint64>() != 0)
                    throw UpdateException("DisabledAttunements deletion not confirmed");
            }
        }
    }
};

void AddProgressionSystemScripts()
{
    new ProgressionSystemLoadingDBUpdates();
}
