#include <cstring>

#include "list.h"
#include <TFE_Game/igame.h>

// Each list slot is a header (whose first byte flags the slot as used) followed by the item.
// __N64__: the original 1-byte header leaves every item at an odd address, which MIPS
// cannot load pointers/ints from. Use an 8-byte header and 8-byte aligned slots instead.
#ifdef __N64__
#define LIST_HDR 8
#else
#define LIST_HDR 1
#endif

namespace TFE_Jedi
{
	void initializeList(List* list);

	u8* list_getNext(List* list)
	{
		u8* iter = list->iter;
		u8* end  = list->end;
		s32 step = list->step;

		// Search for the next item where the first byte is non-zero.
		while (iter < end)
		{
			iter += step;
			if (*iter)
			{
				list->iter = iter;
				return iter + LIST_HDR;
			}
		}

		return nullptr;
	}

	u8* list_getHead(List* list)
	{
		u8* head = list->head;
		list->iter = list->head;
		// If the current entry is not filled, get the next one.
		if (!(head[0] & 1))
		{
			return list_getNext(list);
		}
		return head + LIST_HDR;
	}

	void list_removeItem(List* list, void* item)
	{
		if (!list || !item) { return; }

		u8* data = (u8*)item - LIST_HDR;
		// Already freed.
		if (!data[0]) { return; }

		// Free the item and reduce the count.
		data[0] = 0;
		list->count--;

		// The original method of finding the next free won't really work today, so...
		u8* nextFree = list->head;
		for (s32 i = 0; i < list->capacity; i++, nextFree += list->step)
		{
			if (!nextFree[0])
			{
				list->nextFree = nextFree;
				break;
			}
		}
	}

	u8* list_addItem(List* list)
	{
		u8* nextFree = list->nextFree;
		if (nextFree)
		{
			u8* newItem = nextFree;
			// By marking the first byte with 1, we signal that it is now used.
			newItem[0] = 1;

			list->count++;
			if (list->count >= list->capacity)
			{
				// This is the last available free item, so mark the nextFree as null.
				nextFree = nullptr;
			}
			else
			{
				// Keep stepping forward until another free slot is found.
				while (*nextFree)
				{
					nextFree += list->step;
				}
			}

			list->nextFree = nextFree;
			return newItem + LIST_HDR;
		}

		// TODO(Core Game Loop Release)
		// Handle Error
		return nullptr;
	}

	List* list_allocate(s32 elemSize, s32 capacity)
	{
#ifdef __N64__
		elemSize = (elemSize + LIST_HDR + 7) & ~7;
#else
		elemSize++;
#endif
		s32 size = elemSize * capacity + sizeof(List);
		List* list = (List*)game_alloc(size);
		u8* end = (u8*)list + size;
		list->end = end - elemSize;
		list->self = list;

		u8* start = (u8*)list + sizeof(List);
		list->head = start;
		list->step = elemSize;
		list->capacity = capacity;

		initializeList(list);
		return list;
	}

	void list_clear(List* list)
	{
		if (!list) { return; }

		s32 size = list->step * list->capacity + sizeof(List);
		u8* end = (u8*)list + size;
		list->end = end - list->step;
		list->self = list;

		u8* start = (u8*)list + sizeof(List);
		list->head = start;

		initializeList(list);
	}

	void initializeList(List* list)
	{
		list->iter = list->head;
		list->nextFree = list->head;
		list->count = 0;

		s32 size = s32(list->end - list->head + list->step);
		memset(list->head, 0, size);
	}

}