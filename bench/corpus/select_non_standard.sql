-- Ranked/approved difficulties, ranked separately within each native game mode.
-- Extra candidates allow missing stored files to be reported and skipped.
-- Run each mode query with the same snapshot; no score or player data is read.
SET SESSION MAX_EXECUTION_TIME = 10000;
START TRANSACTION READ ONLY;
SELECT beatmap_id, mode, playcount, ranked FROM beatmaps
WHERE mode = 1 AND ranked IN (2, 3) AND beatmap_id > 0
ORDER BY playcount DESC, beatmap_id ASC LIMIT 5000;
SELECT beatmap_id, mode, playcount, ranked FROM beatmaps
WHERE mode = 2 AND ranked IN (2, 3) AND beatmap_id > 0
ORDER BY playcount DESC, beatmap_id ASC LIMIT 5000;
SELECT beatmap_id, mode, playcount, ranked FROM beatmaps
WHERE mode = 3 AND ranked IN (2, 3) AND beatmap_id > 0
ORDER BY playcount DESC, beatmap_id ASC LIMIT 5000;
COMMIT;
