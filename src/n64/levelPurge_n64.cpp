#include "levelPurge_n64.h"
#include <TFE_Asset/spriteAsset_Jedi.h>
#include <TFE_DarkForces/agent.h>
#include <TFE_DarkForces/generator.h>
#include <TFE_DarkForces/logic.h>
#include <TFE_DarkForces/player.h>
#include <TFE_Jedi/InfSystem/infTypesInternal.h>
#include <TFE_Jedi/InfSystem/message.h>
#include <TFE_Jedi/Level/levelData.h>
#include <TFE_Jedi/Level/robject.h>
#include <TFE_Jedi/Level/rsector.h>
#include <TFE_Jedi/Level/rwall.h>
#include <TFE_Jedi/Memory/allocator.h>
#include <TFE_System/system.h>
#include <cstring>
#include <vector>

#include <libdragon.h>

using namespace TFE_Jedi;
using namespace TFE_DarkForces;

namespace LevelPurge_N64
{
	struct PurgeRule
	{
		const char* level;
		// The one-way point: a "trigger single" switch (its master turns off once used).
		const char* triggerSector;
		s32         triggerWall;
		// The area left behind is every sector reachable from 'oldSector' without
		// entering 'boundarySector'.
		const char* oldSector;
		const char* boundarySector;
	};

	static const PurgeRule c_rules[] =
	{
		// ARC: the elevator switch in hang.elev1 takes the player up to Mohc and the
		// elevator stops for good ("terminate"). The Arc Hammer interior only connects
		// to the hangars through the level-3.elev / level-3.land adjoin.
		{ "arc", "hang.elev1", 2, "level-3.elev", "level-3.land" },
	};

	// The rule and trigger of the current level, looked up once after reset() so a frame
	// only reads one flag until the purge happens.
	static bool s_done = false;
	static bool s_resolved = false;
	static const PurgeRule* s_rule = nullptr;
	static InfTrigger* s_trigger = nullptr;

	void reset()
	{
		s_done = false;
		s_resolved = false;
		s_rule = nullptr;
		s_trigger = nullptr;
	}

	static RSector* getSector(const char* name)
	{
		MessageAddress* address = message_getAddress(name);
		return address ? address->sector : nullptr;
	}

	static InfTrigger* findTrigger(const PurgeRule* rule)
	{
		RSector* sector = getSector(rule->triggerSector);
		if (!sector || rule->triggerWall >= sector->wallCount) { return nullptr; }
		Allocator* links = (Allocator*)sector->walls[rule->triggerWall].infLink;
		if (!links) { return nullptr; }

		InfTrigger* trigger = nullptr;
		allocator_saveIter(links);
		for (InfLink* link = (InfLink*)allocator_getHead(links); link; link = (InfLink*)allocator_getNext(links))
		{
			if (link->type == LTYPE_TRIGGER && link->trigger->type == ITRIGGER_SINGLE)
			{
				trigger = link->trigger;
				break;
			}
		}
		allocator_restoreIter(links);
		return trigger;
	}

	// Marks the sectors reachable from 'oldSector' without going through 'boundarySector'.
	static bool markOldArea(const PurgeRule* rule, std::vector<u8>& old)
	{
		RSector* start = getSector(rule->oldSector);
		RSector* boundary = getSector(rule->boundarySector);
		if (!start || !boundary) { return false; }

		old.assign(s_levelState.sectorCount, 0);
		std::vector<RSector*> stack;
		stack.push_back(start);
		old[start->index] = 1;
		while (!stack.empty())
		{
			RSector* sector = stack.back();
			stack.pop_back();
			for (s32 w = 0; w < sector->wallCount; w++)
			{
				RSector* next = sector->walls[w].nextSector;
				if (!next || next == boundary || old[next->index]) { continue; }
				old[next->index] = 1;
				stack.push_back(next);
			}
		}

		// If the area turns out to contain the player or the trigger, the level is not laid
		// out as expected: keep everything.
		RSector* trigger = getSector(rule->triggerSector);
		if (old[boundary->index] || (trigger && old[trigger->index]) ||
			!s_playerObject || !s_playerObject->sector || old[s_playerObject->sector->index])
		{
			TFE_System::logWrite(LOG_WARNING, "LevelPurge", "Unexpected layout, nothing is released.");
			return false;
		}
		return true;
	}

	static Logic* getLogic(SecObject* obj, LogicType type)
	{
		Allocator* logics = (Allocator*)obj->logic;
		if (!logics) { return nullptr; }
		Logic* found = nullptr;
		allocator_saveIter(logics);
		for (Logic** logic = (Logic**)allocator_getHead(logics); logic; logic = (Logic**)allocator_getNext(logics))
		{
			if (*logic && (*logic)->type == type) { found = *logic; break; }
		}
		allocator_restoreIter(logics);
		return found;
	}

	static void collectObjects(const std::vector<u8>& old, std::vector<SecObject*>& objects)
	{
		for (u32 s = 0; s < s_levelState.sectorCount; s++)
		{
			if (!old[s]) { continue; }
			RSector* sector = &s_levelState.sectors[s];
			for (s32 i = 0; i < sector->objectCapacity; i++)
			{
				SecObject* obj = sector->objectList[i];
				if (obj && obj != s_playerObject && obj != s_playerEye) { objects.push_back(obj); }
			}
		}
	}

	// Frees the level sprites that no object or generator uses any more.
	static s32 freeUnusedSprites()
	{
		std::vector<JediWax*> used;
		for (u32 s = 0; s < s_levelState.sectorCount; s++)
		{
			RSector* sector = &s_levelState.sectors[s];
			for (s32 i = 0; i < sector->objectCapacity; i++)
			{
				SecObject* obj = sector->objectList[i];
				if (!obj) { continue; }
				if (obj->type == OBJ_TYPE_SPRITE) { used.push_back(obj->wax); }
				if (Logic* gen = getLogic(obj, LOGIC_GENERATOR)) { used.push_back(generator_getWax(gen)); }
			}
		}

		// Copy the list: freeWax() changes it.
		std::vector<JediWax*> sprites = TFE_Sprite_Jedi::getWaxList(POOL_LEVEL);
		s32 freed = 0;
		for (JediWax* wax : sprites)
		{
			if (!wax) { continue; }
			bool inUse = false;
			for (JediWax* u : used)
			{
				if (u == wax) { inUse = true; break; }
			}
			if (!inUse)
			{
				TFE_Sprite_Jedi::freeWax(wax);
				freed++;
			}
		}
		return freed;
	}

	static void purge(const PurgeRule* rule)
	{
		std::vector<u8> old;
		if (!markOldArea(rule, old)) { return; }

		heap_stats_t before;
		sys_get_heap_stats(&before);

		// Enemies, corpses, pickups and scenery first. Generators go last: the enemies
		// they spawned report their deletion to the generator task.
		std::vector<SecObject*> objects;
		collectObjects(old, objects);
		std::vector<Logic*> generators;
		s32 deleted = 0;
		for (SecObject* obj : objects)
		{
			// Skip objects already deleted along with another one (freed objects lose 'self').
			if (obj->self != obj || !obj->sector) { continue; }
			if (Logic* gen = getLogic(obj, LOGIC_GENERATOR))
			{
				generators.push_back(gen);
				continue;
			}
			freeObject(obj);
			deleted++;
		}
		for (Logic* gen : generators)
		{
			if (generator_release(gen)) { deleted++; }
		}
		const s32 sprites = freeUnusedSprites();

		heap_stats_t after;
		sys_get_heap_stats(&after);
		TFE_System::logWrite(LOG_MSG, "LevelPurge", "%s: deleted %d objects and %d sprites, %d KB released.",
			rule->level, deleted, sprites, (after.free - before.free) / 1024);
	}

	static void resolve()
	{
		s_resolved = true;
		const char* levelName = agent_getLevelName();
		if (!levelName) { return; }
		for (const PurgeRule& rule : c_rules)
		{
			if (strcasecmp(rule.level, levelName) == 0) { s_rule = &rule; }
		}
		if (s_rule)
		{
			s_trigger = findTrigger(s_rule);
			if (!s_trigger)
			{
				TFE_System::logWrite(LOG_WARNING, "LevelPurge", "%s: trigger not found.", s_rule->level);
			}
		}
	}

	void update()
	{
		if (s_done) { return; }
		if (!s_resolved) { resolve(); }
		if (!s_trigger)
		{
			s_done = true;	// not a level with a rule
			return;
		}

		// A "trigger single" turns its master off once it has been used.
		if (!s_trigger->master)
		{
			purge(s_rule);
			s_done = true;
		}
	}
}
