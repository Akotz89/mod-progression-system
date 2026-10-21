-- CPI356 local source correction v1, MySQL 8.4; not executed against a database.
-- Required future preflight: dated schema compatibility, no affected-table triggers/FKs,
-- normal CREATE TEMPORARY TABLES permission and exclusive maintenance writer closure.
-- Strict updater --skip-force is mandatory: stop/disconnect on ANY statement error.
-- Disconnect rolls back uncommitted InnoDB writes. No persistent routines or ledger DML.
CREATE TEMPORARY TABLE `cpi356_ahune_guard_v1` (`ok` TINYINT NOT NULL, CHECK (`ok`=1) ENFORCED) ENGINE=InnoDB;
SET TRANSACTION ISOLATION LEVEL REPEATABLE READ;
START TRANSACTION;
INSERT INTO `cpi356_ahune_guard_v1` (`ok`) SELECT COALESCE(((SELECT COUNT(*) FROM information_schema.TABLES WHERE TABLE_SCHEMA=DATABASE() AND BINARY TABLE_NAME=BINARY 'gameobject_template' AND TABLE_TYPE='BASE TABLE' AND ENGINE='InnoDB')=1), 0);
SET @a_other=NULL, @a_script=NULL;
SELECT JSON_ARRAY(`entry`, `type`, `displayId`, `name`, `IconName`, `castBarCaption`, `unk1`, `size`, `Data0`, `Data1`, `Data2`, `Data3`, `Data4`, `Data5`, `Data6`, `Data7`, `Data8`, `Data9`, `Data10`, `Data11`, `Data12`, `Data13`, `Data14`, `Data15`, `Data16`, `Data17`, `Data18`, `Data19`, `Data20`, `Data21`, `Data22`, `Data23`, `AIName`, `VerifiedBuild`), `ScriptName` INTO @a_other, @a_script FROM `gameobject_template` FORCE INDEX (`PRIMARY`) WHERE `entry`=187882 FOR UPDATE;
INSERT INTO `cpi356_ahune_guard_v1` (`ok`) SELECT COALESCE(((@a_other IS NOT NULL) AND (BINARY @a_script=BINARY 'go_ahune_ice_stone' OR BINARY @a_script=BINARY 'go_ahune_ice_stone_1_19')), 0);
SET @a_change=(BINARY @a_script=BINARY 'go_ahune_ice_stone');
UPDATE `gameobject_template` SET `ScriptName`='go_ahune_ice_stone_1_19' WHERE `entry`=187882 AND @a_change=1;
INSERT INTO `cpi356_ahune_guard_v1` (`ok`) SELECT COALESCE((ROW_COUNT()=@a_change), 0);
SET @a_after_other=NULL, @a_after_script=NULL;
SELECT JSON_ARRAY(`entry`, `type`, `displayId`, `name`, `IconName`, `castBarCaption`, `unk1`, `size`, `Data0`, `Data1`, `Data2`, `Data3`, `Data4`, `Data5`, `Data6`, `Data7`, `Data8`, `Data9`, `Data10`, `Data11`, `Data12`, `Data13`, `Data14`, `Data15`, `Data16`, `Data17`, `Data18`, `Data19`, `Data20`, `Data21`, `Data22`, `Data23`, `AIName`, `VerifiedBuild`), `ScriptName` INTO @a_after_other, @a_after_script FROM `gameobject_template` WHERE `entry`=187882 FOR UPDATE;
INSERT INTO `cpi356_ahune_guard_v1` (`ok`) SELECT COALESCE(((BINARY @a_after_script=BINARY 'go_ahune_ice_stone_1_19') AND (BINARY CAST(@a_after_other AS CHAR) <=> BINARY CAST(@a_other AS CHAR))), 0);
COMMIT;
DROP TEMPORARY TABLE `cpi356_ahune_guard_v1`;
