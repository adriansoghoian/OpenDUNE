/* $Id$ */

/** @file src/scenario.c %Scenario handling routines. */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "types.h"
#include "global.h"
#include "os/strings.h"

#include "scenario.h"

#include "file.h"
#include "house.h"
#include "ini.h"
#include "map.h"
#include "pool/house.h"
#include "pool/pool.h"
#include "pool/structure.h"
#include "pool/unit.h"
#include "sprites.h"
#include "structure.h"
#include "team.h"
#include "tile.h"
#include "unit.h"


static void Scenario_Load_General()
{
	char *readBuffer = (char *)emu_get_memorycsip(g_global->readBuffer);

	g_global->scenario.winFlags  = Ini_GetInteger("BASIC", "WinFlags",    0,                         readBuffer);
	g_global->scenario.loseFlags = Ini_GetInteger("BASIC", "LoseFlags",   0,                         readBuffer);
	g_global->scenario.mapSeed   = Ini_GetInteger("MAP",   "Seed",        0,                         readBuffer);
	g_global->scenario.timeOut   = Ini_GetInteger("BASIC", "TimeOut",     0,                         readBuffer);
	g_global->minimapPosition    = Ini_GetInteger("BASIC", "TacticalPos", g_global->minimapPosition, readBuffer);
	g_global->variable_3A00      = Ini_GetInteger("BASIC", "CursorPos",   g_global->variable_3A00,   readBuffer);
	g_global->scenario.mapScale  = Ini_GetInteger("BASIC", "MapScale",    0,                         readBuffer);

	Ini_GetString("BASIC", "BriefPicture", "HARVEST.WSA",  g_global->scenario.pictureBriefing, 14, readBuffer);
	Ini_GetString("BASIC", "WinPicture",   "WIN1.WSA",     g_global->scenario.pictureWin,      14, readBuffer);
	Ini_GetString("BASIC", "LosePicture",  "LOSTBILD.WSA", g_global->scenario.pictureLose,     14, readBuffer);

	g_global->viewportPosition  = g_global->minimapPosition;
	g_global->selectionPosition = g_global->variable_3A00;
}

static void Scenario_Load_House(uint8 houseID)
{
	char *readBuffer = (char *)emu_get_memorycsip(g_global->readBuffer);
	char *houseName = (char *)emu_get_memorycsip(g_houseInfo[houseID].name);
	char *houseType;
	char buf[128];
	char *b;
	House *h;

	/* Get the type of the House (CPU / Human) */
	Ini_GetString(houseName, "Brain", "NONE", buf, 127, readBuffer);
	for (b = buf; *b != '\0'; b++) if (*b >= 'a' && *b <= 'z') *b += 'A' - 'a';
	houseType = strstr("HUMAN$CPU", buf);
	if (houseType == NULL) return;

	/* Create the house */
	h = House_Allocate(houseID);

	h->credits      = Ini_GetInteger(houseName, "Credits",  0, readBuffer);
	h->creditsQuota = Ini_GetInteger(houseName, "Quota",    0, readBuffer);
	h->unitCountMax = Ini_GetInteger(houseName, "MaxUnit", 39, readBuffer);

	/* For 'Brain = Human' we have to set a few additional things */
	if (*houseType != 'H') return;

	h->flags.s.human = true;

	g_global->playerHouseID       = houseID;
	g_playerHouse                 = h;
	g_global->playerCreditsNoSilo = h->credits;
}

static void Scenario_Load_Houses()
{
	House *h;
	uint8 houseID;

	for (houseID = 0; houseID < HOUSE_MAX; houseID++) {
		Scenario_Load_House(houseID);
	}

	h = g_playerHouse;
	/* In case there was no unitCountMax in the scenario, calculate
	 *  it based on values used for the AI controlled houses. */
	if (h->unitCountMax == 0) {
		PoolFindStruct find;
		uint8 max;
		House *h;

		find.houseID = 0xFFFF;
		find.index   = 0xFFFF;
		find.type    = 0xFFFF;

		max = 80;
		while ((h = House_Find(&find)) != NULL) {
			/* Skip the human controlled house */
			if (h->flags.s.human) continue;
			max -= h->unitCountMax;
		}

		h->unitCountMax = max;
	}
}

static void Scenario_Load_Unit(const char *key, char *settings)
{
	uint8 houseType, unitType, actionType;
	int8 orientation;
	uint16 hitpoints;
	tile32 position;
	Unit *u;
	char *split;

	VARIABLE_NOT_USED(key);

	/* The value should have 6 values separated by a ',' */
	split = strchr(settings, ',');
	if (split == NULL) return;
	*split = '\0';

	/* First value is the House type */
	houseType = House_StringToType(settings);
	if (houseType == HOUSE_INVALID) return;

	/* Find the next value in the ',' separated list */
	settings = split + 1;
	split = strchr(settings, ',');
	if (split == NULL) return;
	*split = '\0';

	/* Second value is the Unit type */
	unitType = Unit_StringToType(settings);
	if (unitType == UNIT_INVALID) return;

	/* Find the next value in the ',' separated list */
	settings = split + 1;
	split = strchr(settings, ',');
	if (split == NULL) return;
	*split = '\0';

	/* Third value is the Hitpoints in percent (in base 256) */
	hitpoints = atoi(settings);

	/* Find the next value in the ',' separated list */
	settings = split + 1;
	split = strchr(settings, ',');
	if (split == NULL) return;
	*split = '\0';

	/* Fourth value is the position on the map */
	position = Tile_UnpackTile(atoi(settings));

	/* Find the next value in the ',' separated list */
	settings = split + 1;
	split = strchr(settings, ',');
	if (split == NULL) return;
	*split = '\0';

	/* Fifth value is orientation */
	orientation = (int8)((uint8)atoi(settings));

	/* Sixth value is the current state of the unit */
	settings = split + 1;
	actionType = Unit_ActionStringToType(settings);
	if (actionType == ACTION_INVALID) return;


	u = Unit_Allocate(UNIT_INDEX_INVALID, unitType, houseType);
	if (u == NULL) return;
	u->o.flags.s.byScenario = true;

	u->o.hitpoints   = hitpoints * g_table_unitInfo[unitType].o.hitpoints / 256;
	u->o.position    = position;
	u->orientation[0].current = orientation;
	u->actionID     = actionType;
	u->nextActionID = ACTION_INVALID;

	/* In case the above function failed and we are passed campaign 2, don't add the unit */
	if (!Map_IsValidPosition(Tile_PackTile(u->o.position)) && g_global->campaignID > 2) {
		Unit_Free(u);
		return;
	}

	/* XXX -- There is no way this is ever possible, as the beingBuilt flag is unset by Unit_Allocate() */
	if (!u->o.flags.s.isNotOnMap) Unit_SetAction(u, u->actionID);

	u->o.variable_09 = 0x00;

	Unit_HouseUnitCount_Add(u, u->o.houseID);

	Unit_SetOrientation(u, u->orientation[0].current, true, 0);
	Unit_SetOrientation(u, u->orientation[0].current, true, 1);
	Unit_Unknown204C(u, 0);
}

static void Scenario_Load_Structure(const char *key, char *settings)
{
	uint8 index, houseType, structureType;
	uint16 hitpoints, position;
	char *split;

	/* 'GEN' marked keys are Slabs and Walls, where the number following indicates the position on the map */
	if (strncasecmp(key, "GEN", 3) == 0) {
		/* Position on the map is in the key */
		position = atoi(key + 3);

		/* The value should have two values separated by a ',' */
		split = strchr(settings, ',');
		if (split == NULL) return;
		*split = '\0';
		/* First value is the House type */
		houseType = House_StringToType(settings);
		if (houseType == HOUSE_INVALID) return;

		/* Second value is the Structure type */
		settings = split + 1;
		structureType = Structure_StringToType(settings);
		if (structureType == STRUCTURE_INVALID) return;

		Structure_Create(STRUCTURE_INDEX_INVALID, structureType, houseType, position);
		return;
	}

	/* The key should start with 'ID', followed by the index */
	index = atoi(key + 2);

	/* The value should have four values separated by a ',' */
	split = strchr(settings, ',');
	if (split == NULL) return;
	*split = '\0';

	/* First value is the House type */
	houseType = House_StringToType(settings);
	if (houseType == HOUSE_INVALID) return;

	/* Find the next value in the ',' separated list */
	settings = split + 1;
	split = strchr(settings, ',');
	if (split == NULL) return;
	*split = '\0';

	/* Second value is the Structure type */
	structureType = Structure_StringToType(settings);
	if (structureType == STRUCTURE_INVALID) return;

	/* Find the next value in the ',' separated list */
	settings = split + 1;
	split = strchr(settings, ',');
	if (split == NULL) return;
	*split = '\0';

	/* Third value is the Hitpoints in percent (in base 256) */
	hitpoints = atoi(settings);
	/* ENHANCEMENT -- Dune2 ignores the % hitpoints read from the scenario */
	if (!g_dune2_enhanced) hitpoints = 256;

	/* Fourth value is the position of the structure */
	settings = split + 1;
	position = atoi(settings);

	/* Ensure nothing is already on the tile */
	/* XXX -- DUNE2 BUG? -- This only checks the top-left corner? Not really a safety, is it? */
	if (Structure_Get_ByPackedTile(position) != NULL) return;

	{
		Structure *s;

		s = Structure_Create(index, structureType, houseType, position);
		if (s == NULL) return;

		s->o.hitpoints = hitpoints * g_table_structureInfo[s->o.type].o.hitpoints / 256;
		s->o.flags.s.degrades = false;
		s->animation = 0;
	}
}

static void Scenario_Load_Map(const char *key, char *settings)
{
	Tile *t;
	uint16 packed;
	uint16 value;
	char *s;
	char posY[3];

	if (*key != 'C') return;

	memcpy(posY, key + 4, 2);
	posY[2] = '\0';

	packed = Tile_PackXY(atoi(posY), atoi(key + 6)) & 0xFFF;
	t = Map_GetTileByPosition(packed);

	s = strtok(settings, ",\r\n");
	value = atoi(s);
	t->houseID      = value & 0x07;
	t->isUnveiled   = (value & 0x08) != 0 ? true : false;
	t->hasUnit      = (value & 0x10) != 0 ? true : false;
	t->hasStructure = (value & 0x20) != 0 ? true : false;
	t->hasAnimation = (value & 0x40) != 0 ? true : false;
	t->flag_10      = (value & 0x80) != 0 ? true : false;

	s = strtok(NULL, ",\r\n");
	t->groundSpriteID = atoi(s) & 0x01FF;
	if (g_map[packed] != t->groundSpriteID) g_map[packed] |= 0x8000;

	if (!t->isUnveiled) {
		t->overlaySpriteID = g_global->variable_39F2 & 0x7F;
	}
}

static void Scenario_Load_Map_Bloom(uint16 packed, Tile *t)
{
	t->groundSpriteID = g_global->bloomSpriteID & 0x01FF;
	g_map[packed] |= 0x8000;
}

static void Scenario_Load_Map_Field(uint16 packed, Tile *t)
{
	Map_B4CD_14CA(packed, HOUSE_INVALID);

	/* Show where a field started in the preview mode by making it an odd looking sprite */
	if (g_global->debugScenario) {
		t->groundSpriteID = 0x01FF;
	}
}

static void Scenario_Load_Map_Special(uint16 packed, Tile *t)
{
	t->groundSpriteID = (g_global->bloomSpriteID + 1) & 0x01FF;
	g_map[packed] |= 0x8000;
}

static void Scenario_Load_Reinforcement(const char *key, char *settings)
{
	uint8 index, houseType, unitType, locationID;
	uint16 timeBetween;
	tile32 position;
	bool repeat;
	Unit *u;
	char *split;

	index = atoi(key);

	/* The value should have 4 values separated by a ',' */
	split = strchr(settings, ',');
	if (split == NULL) return;
	*split = '\0';

	/* First value is the House type */
	houseType = House_StringToType(settings);
	if (houseType == HOUSE_INVALID) return;

	/* Find the next value in the ',' separated list */
	settings = split + 1;
	split = strchr(settings, ',');
	if (split == NULL) return;
	*split = '\0';

	/* Second value is the Unit type */
	unitType = Unit_StringToType(settings);
	if (unitType == UNIT_INVALID) return;

	/* Find the next value in the ',' separated list */
	settings = split + 1;
	split = strchr(settings, ',');
	if (split == NULL) return;
	*split = '\0';

	/* Third value is the location of the reinforcement */
	     if (strcasecmp(settings, "NORTH")     == 0) locationID = 0;
	else if (strcasecmp(settings, "EAST")      == 0) locationID = 1;
	else if (strcasecmp(settings, "SOUTH")     == 0) locationID = 2;
	else if (strcasecmp(settings, "WEST")      == 0) locationID = 3;
	else if (strcasecmp(settings, "AIR")       == 0) locationID = 4;
	else if (strcasecmp(settings, "VISIBLE")   == 0) locationID = 5;
	else if (strcasecmp(settings, "ENEMYBASE") == 0) locationID = 6;
	else if (strcasecmp(settings, "HOMEBASE")  == 0) locationID = 7;
	else return;

	/* Fourth value is the time between reinforcement */
	settings = split + 1;
	timeBetween = atoi(settings) * 6 + 1;
	repeat = (settings[strlen(settings) - 1] == '+') ? true : false;
	/* ENHANCEMENT -- Dune2 makes a mistake in reading the '+', causing repeat to be always false */
	if (!g_dune2_enhanced) repeat = false;

	position.s.x = 0xFFFF;
	position.s.y = 0xFFFF;
	u = Unit_Create(UNIT_INDEX_INVALID, unitType, houseType, position, 0);
	if (u == NULL) return;

	g_global->scenario.reinforcement[index].unitID      = u->o.index;
	g_global->scenario.reinforcement[index].locationID  = locationID;
	g_global->scenario.reinforcement[index].timeLeft    = timeBetween;
	g_global->scenario.reinforcement[index].timeBetween = timeBetween;
	g_global->scenario.reinforcement[index].repeat      = repeat ? 1 : 0;
}

static void Scenario_Load_Team(const char *key, char *settings)
{
	uint8 houseType, teamActionType, movementType;
	uint16 unknown, maxMembers;
	char *split;

	VARIABLE_NOT_USED(key);

	/* The value should have 5 values separated by a ',' */
	split = strchr(settings, ',');
	if (split == NULL) return;
	*split = '\0';

	/* First value is the House type */
	houseType = House_StringToType(settings);
	if (houseType == HOUSE_INVALID) return;

	/* Find the next value in the ',' separated list */
	settings = split + 1;
	split = strchr(settings, ',');
	if (split == NULL) return;
	*split = '\0';

	/* Second value is the teamAction type */
	teamActionType = Team_ActionStringToType(settings);
	if (teamActionType == TEAM_ACTION_INVALID) return;

	/* Find the next value in the ',' separated list */
	settings = split + 1;
	split = strchr(settings, ',');
	if (split == NULL) return;
	*split = '\0';

	/* Third value is the movement type */
	movementType = Unit_MovementStringToType(settings);
	if (movementType == MOVEMENT_INVALID) return;

	/* Find the next value in the ',' separated list */
	settings = split + 1;
	split = strchr(settings, ',');
	if (split == NULL) return;
	*split = '\0';

	/* XXX -- Fourth value is unknown */
	unknown = atoi(settings);

	/* Find the next value in the ',' separated list */
	settings = split + 1;
	split = strchr(settings, ',');
	if (split == NULL) return;
	*split = '\0';

	/* Fifth value is maximum amount of members in team */
	maxMembers = atoi(settings);

	Team_Create(houseType, teamActionType, movementType, unknown, maxMembers);
}

static void Scenario_Load_Choam(const char *key, char *settings)
{
	uint8 unitType;

	unitType = Unit_StringToType(key);
	if (unitType == UNIT_INVALID) return;

	g_global->starportAvailable[unitType] = atoi(settings);
}

static void Scenario_Load_MapParts(const char *key, void (*ptr)(uint16 packed, Tile *t))
{
	char *readBuffer = (char *)emu_get_memorycsip(g_global->readBuffer);
	char *s;
	char buf[128];

	Ini_GetString("MAP", key, '\0', buf, 127, readBuffer);

	s = strtok(buf, ",\r\n");
	while (s != NULL) {
		uint16 packed;
		Tile *t;

		packed = atoi(s);
		t = Map_GetTileByPosition(packed);

		(*ptr)(packed, t);

		s = strtok(NULL, ",\r\n");
	}
}

static void Scenario_Load_Chunk(const char *category, void (*ptr)(const char *key, char *settings))
{
	char *readBuffer = (char *)emu_get_memorycsip(g_global->readBuffer);
	uint16 length    = strlen(readBuffer) + 2;
	char *buffer     = readBuffer + length;

	Ini_GetString(category, NULL, NULL, buffer, g_global->readBufferSize - length, readBuffer);
	while (true) {
		char buf[127];

		if (*buffer == '\0') break;

		Ini_GetString(category, buffer, NULL, buf, 127, readBuffer);

		(*ptr)(buffer, buf);
		buffer += strlen(buffer) + 1;
	}
}

bool Scenario_Load(uint16 scenarioID, uint8 houseID)
{
	char *readBuffer = (char *)emu_get_memorycsip(g_global->readBuffer);
	char filename[14];
	int i;

	if (houseID >= HOUSE_MAX) return false;

	g_global->scenarioID = scenarioID;

	/* Clear the buffer we will read in */
	memset(readBuffer, 0, g_global->readBufferSize);

	/* Load scenario file */
	sprintf(filename, "SCEN%c%03d.INI", emu_get_memorycsip(g_houseInfo[houseID].name)[0], scenarioID);
	if (!File_Exists(filename)) return false;
	File_ReadBlockFile(filename, readBuffer, g_global->readBufferSize);

	memset(&g_global->scenario, 0, sizeof(Scenario));
	g_global->scenario.savegameVersion = 0x290;

	Scenario_Load_General();
	Sprites_LoadTiles();
	Map_CreateLandscape(g_global->scenario.mapSeed);

	for (i = 0; i < 16; i++) {
		g_global->scenario.reinforcement[i].unitID = UNIT_INDEX_INVALID;
	}

	Scenario_Load_Houses();

	Scenario_Load_Chunk("UNITS", &Scenario_Load_Unit);
	Scenario_Load_Chunk("STRUCTURES", &Scenario_Load_Structure);
	Scenario_Load_Chunk("MAP", &Scenario_Load_Map);
	Scenario_Load_Chunk("REINFORCEMENTS", &Scenario_Load_Reinforcement);
	Scenario_Load_Chunk("TEAMS", &Scenario_Load_Team);
	Scenario_Load_Chunk("CHOAM", &Scenario_Load_Choam);

	Scenario_Load_MapParts("Bloom", Scenario_Load_Map_Bloom);
	Scenario_Load_MapParts("Field", Scenario_Load_Map_Field);
	Scenario_Load_MapParts("Special", Scenario_Load_Map_Special);

	g_global->tickScenarioStart = g_global->tickGlobal;
	return true;
}
