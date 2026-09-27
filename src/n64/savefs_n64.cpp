#include "savefs_n64.h"
#include <TFE_System/types.h>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>

#include <libdragon.h>
#include <system.h>

namespace SaveFS_N64
{
	enum
	{
		SRAM_SIZE  = 32 * 1024,	// N64_ROM_SAVETYPE = sram256k
		MAX_FILES  = 8,
		NAME_LEN   = 24,
		MAX_HANDLES = 4,
	};

	// SRAM image layout: ImageHeader, MAX_FILES x ImageEntry, then file data.
	static const u32 c_magic = 0x44463634;	// 'DF64'
	// Version 2: the agent file is no longer seeded from the game's DARKPILO.CFG.
	static const u32 c_version = 2;

	struct ImageHeader
	{
		u32 magic;
		u32 version;
		u32 count;
		u32 checksum;	// sum of every byte after the header
	};

	struct ImageEntry
	{
		char name[NAME_LEN];
		u32  size;
	};

	struct SaveFile
	{
		char name[NAME_LEN];
		u8*  data;
		u32  size;
		u32  capacity;
	};

	struct Handle
	{
		SaveFile* file;
		u32  pos;
		bool inUse;
		bool writable;
		bool dirty;
	};

	static SaveFile s_files[MAX_FILES];
	static Handle s_handles[MAX_HANDLES];
	static u8 s_image[SRAM_SIZE] __attribute__((aligned(16)));

	static const u32 c_dataStart = sizeof(ImageHeader) + MAX_FILES * sizeof(ImageEntry);

	static u32 usedBytes()
	{
		u32 total = c_dataStart;
		for (s32 i = 0; i < MAX_FILES; i++)
		{
			if (s_files[i].name[0]) { total += s_files[i].size; }
		}
		return total;
	}

	static void flush()
	{
		memset(s_image, 0, sizeof(s_image));
		ImageHeader* header = (ImageHeader*)s_image;
		ImageEntry* entries = (ImageEntry*)(s_image + sizeof(ImageHeader));
		u32 offset = c_dataStart;
		u32 count = 0;
		for (s32 i = 0; i < MAX_FILES; i++)
		{
			const SaveFile* file = &s_files[i];
			if (!file->name[0]) { continue; }
			memcpy(entries[count].name, file->name, NAME_LEN);
			entries[count].size = file->size;
			memcpy(s_image + offset, file->data, file->size);
			offset += file->size;
			count++;
		}

		u32 checksum = 0;
		for (u32 i = sizeof(ImageHeader); i < SRAM_SIZE; i++) { checksum += s_image[i]; }
		header->magic = c_magic;
		header->version = c_version;
		header->count = count;
		header->checksum = checksum;

		data_cache_hit_writeback(s_image, sizeof(s_image));
		sram_write(s_image, 0, SRAM_SIZE);
	}

	static void load()
	{
		memset(s_files, 0, sizeof(s_files));
		sram_read(s_image, 0, SRAM_SIZE);

		const ImageHeader* header = (const ImageHeader*)s_image;
		if (header->magic != c_magic || header->version != c_version || header->count > MAX_FILES) { return; }

		u32 checksum = 0;
		for (u32 i = sizeof(ImageHeader); i < SRAM_SIZE; i++) { checksum += s_image[i]; }
		if (checksum != header->checksum) { return; }

		const ImageEntry* entries = (const ImageEntry*)(s_image + sizeof(ImageHeader));
		u32 offset = c_dataStart;
		for (u32 i = 0; i < header->count; i++)
		{
			if (offset + entries[i].size > SRAM_SIZE) { break; }
			SaveFile* file = &s_files[i];
			memcpy(file->name, entries[i].name, NAME_LEN);
			file->name[NAME_LEN - 1] = 0;
			file->size = entries[i].size;
			file->capacity = file->size;
			file->data = (u8*)malloc(file->size ? file->size : 1);
			memcpy(file->data, s_image + offset, file->size);
			offset += file->size;
		}
	}

	static SaveFile* findFile(const char* name)
	{
		for (s32 i = 0; i < MAX_FILES; i++)
		{
			if (s_files[i].name[0] && strcasecmp(s_files[i].name, name) == 0) { return &s_files[i]; }
		}
		return nullptr;
	}

	static SaveFile* createFile(const char* name)
	{
		if (strlen(name) >= NAME_LEN) { return nullptr; }
		for (s32 i = 0; i < MAX_FILES; i++)
		{
			if (!s_files[i].name[0])
			{
				SaveFile* file = &s_files[i];
				strcpy(file->name, name);
				file->size = 0;
				file->capacity = 0;
				file->data = nullptr;
				return file;
			}
		}
		return nullptr;
	}

	// libdragon passes the path without the "save:/" prefix, but possibly with a leading '/'.
	static const char* stripSlash(const char* name)
	{
		while (*name == '/') { name++; }
		return name;
	}

	static void* fs_open(char* name, int flags)
	{
		name = (char*)stripSlash(name);
		const int access = flags & O_ACCMODE;
		const bool writable = access != O_RDONLY;

		SaveFile* file = findFile(name);
		if (!file)
		{
			if (!(flags & O_CREAT)) { errno = ENOENT; return nullptr; }
			file = createFile(name);
			if (!file) { errno = ENOSPC; return nullptr; }
		}
		if (flags & O_TRUNC) { file->size = 0; }

		for (s32 i = 0; i < MAX_HANDLES; i++)
		{
			Handle* handle = &s_handles[i];
			if (handle->inUse) { continue; }
			handle->inUse = true;
			handle->file = file;
			handle->pos = (flags & O_APPEND) ? file->size : 0;
			handle->writable = writable;
			handle->dirty = (flags & (O_CREAT | O_TRUNC)) != 0 && writable;
			return handle;
		}
		errno = EMFILE;
		return nullptr;
	}

	static int fs_read(void* h, uint8_t* ptr, int len)
	{
		Handle* handle = (Handle*)h;
		SaveFile* file = handle->file;
		if (handle->pos >= file->size) { return 0; }
		const u32 count = (u32)len < file->size - handle->pos ? (u32)len : file->size - handle->pos;
		memcpy(ptr, file->data + handle->pos, count);
		handle->pos += count;
		return (int)count;
	}

	static int fs_write(void* h, uint8_t* ptr, int len)
	{
		Handle* handle = (Handle*)h;
		SaveFile* file = handle->file;
		if (!handle->writable) { errno = EBADF; return -1; }

		const u32 end = handle->pos + (u32)len;
		if (end > file->size)
		{
			// Refuse writes that would no longer fit in SRAM.
			if (usedBytes() + (end - file->size) > SRAM_SIZE) { errno = ENOSPC; return -1; }
			if (end > file->capacity)
			{
				const u32 capacity = end + 1024;
				u8* data = (u8*)realloc(file->data, capacity);
				if (!data) { errno = ENOMEM; return -1; }
				file->data = data;
				file->capacity = capacity;
			}
			// Zero-fill a gap left by seeking past the end.
			if (handle->pos > file->size) { memset(file->data + file->size, 0, handle->pos - file->size); }
			file->size = end;
		}
		memcpy(file->data + handle->pos, ptr, len);
		handle->pos = end;
		handle->dirty = true;
		return len;
	}

	static int fs_lseek(void* h, int offset, int dir)
	{
		Handle* handle = (Handle*)h;
		s32 base = 0;
		if (dir == SEEK_CUR) { base = (s32)handle->pos; }
		else if (dir == SEEK_END) { base = (s32)handle->file->size; }
		const s32 pos = base + offset;
		if (pos < 0) { errno = EINVAL; return -1; }
		handle->pos = (u32)pos;
		return pos;
	}

	static int fs_close(void* h)
	{
		Handle* handle = (Handle*)h;
		const bool dirty = handle->dirty;
		handle->inUse = false;
		if (dirty) { flush(); }
		return 0;
	}

	static int fs_fstat(void* h, struct stat* st)
	{
		Handle* handle = (Handle*)h;
		memset(st, 0, sizeof(*st));
		st->st_mode = S_IFREG;
		st->st_size = handle->file->size;
		return 0;
	}

	static int fs_stat(char* name, struct stat* st)
	{
		SaveFile* file = findFile(stripSlash(name));
		if (!file) { errno = ENOENT; return -1; }
		memset(st, 0, sizeof(*st));
		st->st_mode = S_IFREG;
		st->st_size = file->size;
		return 0;
	}

	static int fs_unlink(char* name)
	{
		SaveFile* file = findFile(stripSlash(name));
		if (!file) { errno = ENOENT; return -1; }
		free(file->data);
		memset(file, 0, sizeof(*file));
		flush();
		return 0;
	}

	static s32 s_findIndex = 0;

	static int fs_findnext(dir_t* dir)
	{
		for (; s_findIndex < MAX_FILES; s_findIndex++)
		{
			if (s_files[s_findIndex].name[0])
			{
				strcpy(dir->d_name, s_files[s_findIndex].name);
				dir->d_type = DT_REG;
				dir->d_size = s_files[s_findIndex].size;
				s_findIndex++;
				return 0;
			}
		}
		return -1;
	}

	static int fs_findfirst(char* path, dir_t* dir)
	{
		s_findIndex = 0;
		return fs_findnext(dir);
	}

	static filesystem_t s_saveFs = {};

	void init()
	{
		sram_init();
		load();

		s_saveFs.thread_safe = false;
		s_saveFs.open = fs_open;
		s_saveFs.fstat = fs_fstat;
		s_saveFs.stat = fs_stat;
		s_saveFs.lseek = fs_lseek;
		s_saveFs.read = fs_read;
		s_saveFs.write = fs_write;
		s_saveFs.close = fs_close;
		s_saveFs.unlink = fs_unlink;
		s_saveFs.findfirst = fs_findfirst;
		s_saveFs.findnext = fs_findnext;
		attach_filesystem("save:/", &s_saveFs);
	}
}
