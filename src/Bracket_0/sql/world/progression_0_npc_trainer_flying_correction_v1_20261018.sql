-- CPI356 versioned trainer-offer correction; MySQL 8.4.
-- Strict updater MUST pass --skip-force. A failed statement aborts the batch;
-- connection close rolls back uncommitted InnoDB changes and drops this session table.
-- Required maintenance preflight: exact current schema, no trainer triggers/FKs,
-- normal CREATE TEMPORARY TABLES permission, and positive writer closure.
-- No routines, global-grant policy, ledger DML or earned-state DML.
CREATE TEMPORARY TABLE `cpi356_flying_trainer_guard_v1` (
  `ok` TINYINT NOT NULL,
  CHECK (`ok`=1) ENFORCED
) ENGINE=InnoDB;
SET TRANSACTION ISOLATION LEVEL REPEATABLE READ;
START TRANSACTION;
SET @cpi356_flying_r1 = NULL, @cpi356_flying_r2 = NULL, @cpi356_flying_r3 = NULL;
-- Exact-key reads retain metadata locks; a missing key can also hold a gap lock.
SELECT CONCAT_WS(',', `ID`, `SpellID`, `MoneyCost`, `ReqSkillLine`, `ReqSkillRank`, `ReqLevel`, `ReqSpell`) INTO @cpi356_flying_r1
  FROM `npc_trainer` FORCE INDEX (`PRIMARY`)
  WHERE `ID`=200006 AND `SpellID`=33950 FOR UPDATE;
SELECT CONCAT_WS(',', `ID`, `SpellID`, `MoneyCost`, `ReqSkillLine`, `ReqSkillRank`, `ReqLevel`, `ReqSpell`) INTO @cpi356_flying_r2
  FROM `npc_trainer` FORCE INDEX (`PRIMARY`)
  WHERE `ID`=200006 AND `SpellID`=40120 FOR UPDATE;
SELECT CONCAT_WS(',', `ID`, `SpellID`, `MoneyCost`, `ReqSkillLine`, `ReqSkillRank`, `ReqLevel`, `ReqSpell`) INTO @cpi356_flying_r3
  FROM `npc_trainer` FORCE INDEX (`PRIMARY`)
  WHERE `ID`=202011 AND `SpellID`=34090 FOR UPDATE;
SET @cpi356_flying_before = (@cpi356_flying_r1 <=> '200006,33950,34000,0,0,60,0') AND (@cpi356_flying_r2 <=> '200006,40120,200000,762,300,71,0') AND (@cpi356_flying_r3 <=> '202011,34090,2500000,762,150,60,0');
SET @cpi356_flying_after = (@cpi356_flying_r1 <=> '200006,33950,34000,0,0,68,0') AND @cpi356_flying_r2 IS NULL AND (@cpi356_flying_r3 <=> '202011,34090,8000000,762,150,70,0');
-- An enforced CHECK rejects false (0); COALESCE maps unknown/NULL to false.
-- Only ordinary table metadata is queried; privileged trigger/FK preflight is external.
INSERT INTO `cpi356_flying_trainer_guard_v1` (`ok`)
SELECT COALESCE(
  (@cpi356_flying_before OR @cpi356_flying_after)
  AND (SELECT COUNT(*) FROM information_schema.TABLES
    WHERE TABLE_SCHEMA=DATABASE() AND TABLE_NAME='npc_trainer'
      AND TABLE_TYPE='BASE TABLE' AND ENGINE='InnoDB')=1
  AND (SELECT COUNT(*) FROM information_schema.COLUMNS
    WHERE TABLE_SCHEMA=DATABASE() AND TABLE_NAME='npc_trainer')=7
  AND (SELECT COUNT(*) FROM information_schema.COLUMNS
    WHERE TABLE_SCHEMA=DATABASE() AND TABLE_NAME='npc_trainer'
      AND IS_NULLABLE='NO' AND COLUMN_DEFAULT='0' AND EXTRA=''
      AND (
        (ORDINAL_POSITION=1 AND BINARY COLUMN_NAME=BINARY 'ID' AND COLUMN_TYPE='int unsigned') OR
        (ORDINAL_POSITION=2 AND BINARY COLUMN_NAME=BINARY 'SpellID' AND COLUMN_TYPE='int') OR
        (ORDINAL_POSITION=3 AND BINARY COLUMN_NAME=BINARY 'MoneyCost' AND COLUMN_TYPE='int unsigned') OR
        (ORDINAL_POSITION=4 AND BINARY COLUMN_NAME=BINARY 'ReqSkillLine' AND COLUMN_TYPE='smallint unsigned') OR
        (ORDINAL_POSITION=5 AND BINARY COLUMN_NAME=BINARY 'ReqSkillRank' AND COLUMN_TYPE='smallint unsigned') OR
        (ORDINAL_POSITION=6 AND BINARY COLUMN_NAME=BINARY 'ReqLevel' AND COLUMN_TYPE='tinyint unsigned') OR
        (ORDINAL_POSITION=7 AND BINARY COLUMN_NAME=BINARY 'ReqSpell' AND COLUMN_TYPE='int unsigned')
      ))=7
  AND (SELECT COUNT(*) FROM information_schema.STATISTICS
    WHERE TABLE_SCHEMA=DATABASE() AND TABLE_NAME='npc_trainer')=2
  AND (SELECT COUNT(*) FROM information_schema.STATISTICS
    WHERE TABLE_SCHEMA=DATABASE() AND TABLE_NAME='npc_trainer'
      AND INDEX_NAME='PRIMARY' AND NON_UNIQUE=0 AND SUB_PART IS NULL
      AND ((SEQ_IN_INDEX=1 AND BINARY COLUMN_NAME=BINARY 'ID')
        OR (SEQ_IN_INDEX=2 AND BINARY COLUMN_NAME=BINARY 'SpellID')))=2,
  0);

UPDATE `npc_trainer` SET `ReqLevel`=68 WHERE `ID`=200006 AND `SpellID`=33950 AND @cpi356_flying_before=1;
INSERT INTO `cpi356_flying_trainer_guard_v1` (`ok`)
SELECT COALESCE(ROW_COUNT()=@cpi356_flying_before, 0);
DELETE FROM `npc_trainer` WHERE `ID`=200006 AND `SpellID`=40120 AND @cpi356_flying_before=1;
INSERT INTO `cpi356_flying_trainer_guard_v1` (`ok`)
SELECT COALESCE(ROW_COUNT()=@cpi356_flying_before, 0);
UPDATE `npc_trainer` SET `MoneyCost`=8000000, `ReqLevel`=70 WHERE `ID`=202011 AND `SpellID`=34090 AND @cpi356_flying_before=1;
INSERT INTO `cpi356_flying_trainer_guard_v1` (`ok`)
SELECT COALESCE(ROW_COUNT()=@cpi356_flying_before, 0);

INSERT INTO `cpi356_flying_trainer_guard_v1` (`ok`)
SELECT COALESCE(
  (SELECT COUNT(*) FROM `npc_trainer` WHERE
    (`ID`=200006 AND `SpellID`=33950 AND `MoneyCost`=34000 AND `ReqSkillLine`=0 AND `ReqSkillRank`=0 AND `ReqLevel`=68 AND `ReqSpell`=0) OR
    (`ID`=202011 AND `SpellID`=34090 AND `MoneyCost`=8000000 AND `ReqSkillLine`=762 AND `ReqSkillRank`=150 AND `ReqLevel`=70 AND `ReqSpell`=0))=2
  AND NOT EXISTS (SELECT 1 FROM `npc_trainer` WHERE `ID`=200006 AND `SpellID`=40120),
  0);
COMMIT;
DROP TEMPORARY TABLE `cpi356_flying_trainer_guard_v1`;
