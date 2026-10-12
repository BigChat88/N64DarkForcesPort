#include "savefs_n64.h"
#include <TFE_System/types.h>
#include <cerrno>
#include <cstdio>
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
		SRAM_SIZE   = 32 * 1024,	// N64_ROM_SAVETYPE = sram256k
		MAX_FILES   = 8,
		NAME_LEN    = 24,
		MAX_HANDLES = 4,
		// The files are kept in RAM; their total size is limited like the old single image.
		RAW_LIMIT   = SRAM_SIZE,
	};

	// SRAM layout: two slots, each holding a complete compressed copy of the files.
	// A save goes to the slot without the latest copy, so a save interrupted by a power
	// cut leaves the previous one intact. At boot the valid slot with the highest
	// sequence number wins.
	//
	// Slot: SlotHeader, then the compressed stream. Uncompressed stream: u32 count,
	// count x ImageEntry, then the file data in the same order. Compression: a control
	// byte c < 0x80 is followed by c + 1 literal bytes, c >= 0x80 stands for
	// (c & 0x7f) + 1 zero bytes. The agent file (DARKPILO.CFG) is mostly zeros: it always
	// holds 14 agents of ~1KB.
	//
	// SRAM is accessed through one 512 byte buffer instead of a copy of the whole SRAM
	// (32KB of RAM, see init()). PI DMA transfers stay aligned to 16 bytes.
	enum
	{
		SLOT_SIZE   = SRAM_SIZE / 2,
		SLOT_COUNT  = 2,
		CHUNK_SIZE  = 512,
	};
	static const u32 c_slotMagic = 0x44465333;	// 'DFS3'

	struct SlotHeader
	{
		u32 magic;
		u32 sequence;
		u32 size;		// compressed bytes after the header
		u32 checksum;	// FNV-1a of the compressed bytes
	};

	struct ImageEntry
	{
		char name[NAME_LEN];
		u32  size;
	};

	// Version 2 layout, read once to convert old saves: a single image with an
	// ImageHeader, MAX_FILES x ImageEntry, then the file data.
	static const u32 c_legacyMagic = 0x44463634;	// 'DF64'
	static const u32 c_legacyVersion = 2;
	struct LegacyHeader
	{
		u32 magic;
		u32 version;
		u32 count;
		u32 checksum;	// sum of every byte after the header
	};
	static const u32 c_legacyDataStart = sizeof(LegacyHeader) + MAX_FILES * sizeof(ImageEntry);

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
	static u8 s_chunk[CHUNK_SIZE] __attribute__((aligned(16)));
	static u32 s_chunkAddr = ~0u;	// SRAM address of the chunk held in s_chunk, for reads

	static u32 s_sequence = 0;		// sequence number of the latest copy
	static s32 s_nextSlot = 0;		// slot the next save goes to

	static u32 usedBytes()
	{
		u32 total = 0;
		for (s32 i = 0; i < MAX_FILES; i++)
		{
			if (s_files[i].name[0]) { total += s_files[i].size; }
		}
		return total;
	}

	static u32 fnv1a(u32 hash, u8 byte)
	{
		return (hash ^ byte) * 16777619u;
	}
	static const u32 c_fnvBasis = 2166136261u;

	/////////////////////////////////////////////
	// Reading
	/////////////////////////////////////////////
	static u8 readByte(u32 addr)
	{
		const u32 chunk = addr & ~(u32)(CHUNK_SIZE - 1);
		if (chunk != s_chunkAddr)
		{
			sram_read(s_chunk, chunk, CHUNK_SIZE);
			s_chunkAddr = chunk;
		}
		return s_chunk[addr - chunk];
	}

	static void readBytes(u32 addr, void* dst, u32 len)
	{
		u8* out = (u8*)dst;
		for (u32 i = 0; i < len; i++) { out[i] = readByte(addr + i); }
	}

	// Decompresses the stream of a slot.
	struct Unpacker
	{
		u32 addr;		// next compressed byte
		u32 end;
		u32 runLeft;
		bool zeros;
		bool error;

		void init(u32 start, u32 size)
		{
			addr = start;
			end = start + size;
			runLeft = 0;
			zeros = false;
			error = false;
		}

		u8 get()
		{
			if (!runLeft)
			{
				if (addr >= end) { error = true; return 0; }
				const u8 c = readByte(addr++);
				zeros = (c & 0x80) != 0;
				runLeft = (c & 0x7f) + 1;
			}
			runLeft--;
			if (zeros) { return 0; }
			if (addr >= end) { error = true; return 0; }
			return readByte(addr++);
		}

		void get(void* dst, u32 len)
		{
			u8* out = (u8*)dst;
			for (u32 i = 0; i < len; i++) { out[i] = get(); }
		}
	};

	static void freeFiles()
	{
		for (s32 i = 0; i < MAX_FILES; i++) { free(s_files[i].data); }
		memset(s_files, 0, sizeof(s_files));
	}

	// Adds a loaded file; false if the entry is not valid.
	static SaveFile* addLoadedFile(s32 index, const ImageEntry& entry, u32* total)
	{
		if (!entry.name[0] || entry.size > RAW_LIMIT - *total) { return nullptr; }
		SaveFile* file = &s_files[index];
		memcpy(file->name, entry.name, NAME_LEN);
		file->name[NAME_LEN - 1] = 0;
		file->size = entry.size;
		file->capacity = entry.size;
		file->data = (u8*)malloc(entry.size ? entry.size : 1);
		if (!file->data) { return nullptr; }
		*total += entry.size;
		return file;
	}

	// Reads the header of a slot and checks the compressed data against its checksum.
	static bool validateSlot(s32 slot, SlotHeader* header)
	{
		const u32 base = slot * SLOT_SIZE;
		readBytes(base, header, sizeof(SlotHeader));
		if (header->magic != c_slotMagic || header->size > SLOT_SIZE - sizeof(SlotHeader)) { return false; }

		u32 hash = c_fnvBasis;
		const u32 start = base + sizeof(SlotHeader);
		for (u32 i = 0; i < header->size; i++) { hash = fnv1a(hash, readByte(start + i)); }
		return hash == header->checksum;
	}

	static bool loadSlot(s32 slot, const SlotHeader& header)
	{
		Unpacker in;
		in.init(slot * SLOT_SIZE + sizeof(SlotHeader), header.size);

		u32 count = 0;
		in.get(&count, sizeof(count));
		if (in.error || count > MAX_FILES) { return false; }

		ImageEntry entries[MAX_FILES];
		in.get(entries, count * sizeof(ImageEntry));
		if (in.error) { return false; }

		u32 total = 0;
		for (u32 i = 0; i < count; i++)
		{
			SaveFile* file = addLoadedFile(i, entries[i], &total);
			if (!file) { freeFiles(); return false; }
			in.get(file->data, file->size);
			if (in.error) { freeFiles(); return false; }
		}
		return true;
	}

	static bool loadLegacy()
	{
		LegacyHeader header;
		readBytes(0, &header, sizeof(header));
		if (header.magic != c_legacyMagic || header.version != c_legacyVersion || header.count > MAX_FILES) { return false; }

		u32 checksum = 0;
		for (u32 i = sizeof(LegacyHeader); i < SRAM_SIZE; i++) { checksum += readByte(i); }
		if (checksum != header.checksum) { return false; }

		ImageEntry entries[MAX_FILES];
		readBytes(sizeof(LegacyHeader), entries, sizeof(entries));
		u32 offset = c_legacyDataStart;
		u32 total = 0;
		for (u32 i = 0; i < header.count; i++)
		{
			if (offset + entries[i].size > SRAM_SIZE) { break; }
			SaveFile* file = addLoadedFile(i, entries[i], &total);
			if (!file) { break; }
			readBytes(offset, file->data, file->size);
			offset += file->size;
		}
		return true;
	}

	static void load()
	{
		memset(s_files, 0, sizeof(s_files));
		s_chunkAddr = ~0u;

		SlotHeader headers[SLOT_COUNT];
		bool valid[SLOT_COUNT];
		for (s32 i = 0; i < SLOT_COUNT; i++) { valid[i] = validateSlot(i, &headers[i]); }

		// Newest valid slot first; a slot that fails to load falls back to the other one.
		s32 order[SLOT_COUNT] = { 0, 1 };
		if (valid[0] && valid[1] && s32(headers[1].sequence - headers[0].sequence) > 0) { order[0] = 1; order[1] = 0; }
		else if (!valid[0]) { order[0] = 1; order[1] = 0; }
		for (s32 i = 0; i < SLOT_COUNT; i++)
		{
			const s32 slot = order[i];
			if (valid[slot] && loadSlot(slot, headers[slot]))
			{
				s_sequence = headers[slot].sequence;
				s_nextSlot = slot ^ 1;
				return;
			}
		}

		// Saves of the previous versions: converted the next time a file is saved. The
		// first save goes to slot 1, which overwrites the end of the old image.
		s_sequence = 0;
		s_nextSlot = 1;
		if (!loadLegacy()) { freeFiles(); }
	}

	/////////////////////////////////////////////
	// Writing
	/////////////////////////////////////////////
	// Compresses the files into a slot. Without 'write' it only computes the size and
	// checksum, so a save that does not fit never touches the SRAM.
	struct Packer
	{
		bool write;
		u32  base;		// SRAM address of the slot
		u32  size;		// compressed bytes so far
		u32  hash;
		u8   literal[128];
		s32  literalCount;
		u32  zeroCount;

		void init(bool doWrite, u32 slotBase)
		{
			write = doWrite;
			base = slotBase;
			size = 0;
			hash = c_fnvBasis;
			literalCount = 0;
			zeroCount = 0;
		}

		void emit(u8 byte)
		{
			hash = fnv1a(hash, byte);
			if (write)
			{
				const u32 pos = sizeof(SlotHeader) + size;
				if (pos < SLOT_SIZE)
				{
					s_chunk[pos % CHUNK_SIZE] = byte;
					if ((pos + 1) % CHUNK_SIZE == 0) { sram_write(s_chunk, base + pos + 1 - CHUNK_SIZE, CHUNK_SIZE); }
				}
			}
			size++;
		}

		void flushLiteral()
		{
			if (!literalCount) { return; }
			emit(u8(literalCount - 1));
			for (s32 i = 0; i < literalCount; i++) { emit(literal[i]); }
			literalCount = 0;
		}

		void addLiteral(u8 byte)
		{
			literal[literalCount++] = byte;
			if (literalCount == 128) { flushLiteral(); }
		}

		// Short zero runs are cheaper inside a literal run.
		void flushZeros(bool force)
		{
			if (!zeroCount) { return; }
			if (zeroCount >= 3 || force)
			{
				flushLiteral();
				while (zeroCount)
				{
					const u32 n = zeroCount < 128 ? zeroCount : 128;
					emit(u8(0x80 | (n - 1)));
					zeroCount -= n;
				}
			}
			else
			{
				for (; zeroCount; zeroCount--) { addLiteral(0); }
			}
		}

		void put(u8 byte)
		{
			if (!byte) { zeroCount++; return; }
			flushZeros(false);
			addLiteral(byte);
		}

		void put(const void* src, u32 len)
		{
			const u8* in = (const u8*)src;
			for (u32 i = 0; i < len; i++) { put(in[i]); }
		}

		void finish()
		{
			flushZeros(true);
			flushLiteral();
			if (write)
			{
				// The last partial chunk, padded to a 16 byte transfer.
				const u32 end = sizeof(SlotHeader) + size;
				const u32 chunkStart = end & ~(u32)(CHUNK_SIZE - 1);
				const u32 len = (end - chunkStart + 15) & ~15u;
				if (len)
				{
					memset(&s_chunk[end - chunkStart], 0, len - (end - chunkStart));
					sram_write(s_chunk, base + chunkStart, len);
				}
			}
		}
	};

	static void packFiles(Packer& out)
	{
		u32 count = 0;
		for (s32 i = 0; i < MAX_FILES; i++) { if (s_files[i].name[0]) { count++; } }
		out.put(&count, sizeof(count));
		for (s32 i = 0; i < MAX_FILES; i++)
		{
			if (!s_files[i].name[0]) { continue; }
			ImageEntry entry = {};
			memcpy(entry.name, s_files[i].name, NAME_LEN);
			entry.size = s_files[i].size;
			out.put(&entry, sizeof(entry));
		}
		for (s32 i = 0; i < MAX_FILES; i++)
		{
			if (s_files[i].name[0]) { out.put(s_files[i].data, s_files[i].size); }
		}
		out.finish();
	}

	static bool flush()
	{
		const u32 base = s_nextSlot * SLOT_SIZE;

		Packer measure;
		measure.init(false, base);
		packFiles(measure);
		if (measure.size > SLOT_SIZE - sizeof(SlotHeader))
		{
			fprintf(stderr, "[SaveFS] Save does not fit: %lu compressed bytes.\n", (unsigned long)measure.size);
			return false;
		}

		// The header is part of the first chunk, written with the data.
		SlotHeader header = { c_slotMagic, s_sequence + 1, measure.size, measure.hash };
		memcpy(s_chunk, &header, sizeof(header));
		s_chunkAddr = ~0u;

		Packer out;
		out.init(true, base);
		packFiles(out);

		s_sequence++;
		s_nextSlot ^= 1;
		return true;
	}

	/////////////////////////////////////////////
	// Filesystem
	/////////////////////////////////////////////
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
			// The compressed size is only known when saving (see flush()).
			if (usedBytes() + (end - file->size) > RAW_LIMIT) { errno = ENOSPC; return -1; }
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
		if (dirty && !flush()) { errno = ENOSPC; return -1; }
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
		if (!flush()) { errno = ENOSPC; return -1; }
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
