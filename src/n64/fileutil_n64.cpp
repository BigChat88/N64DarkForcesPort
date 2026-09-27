// FileUtil for the N64: the game data lives in the read-only ROM filesystem
// (rom:/), so directory handling is minimal and nothing can be created or
// deleted. Replaces TFE_FileSystem/fileutil-posix.cpp.
#include <TFE_FileSystem/fileutil.h>
#include <TFE_FileSystem/paths.h>
#include <TFE_System/system.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>
#include <ctype.h>

#include <dir.h>

namespace FileUtil
{
	static const char* c_root = "rom:/";

	static bool fileExistsExact(const char* path)
	{
		FILE* file = fopen(path, "rb");
		if (!file) { return false; }
		fclose(file);
		return true;
	}

	// Returns the offset of the file name within the path.
	static size_t getNameOffset(const char* path)
	{
		const char* slash = strrchr(path, '/');
		return slash ? size_t(slash - path + 1) : 0;
	}

	static bool hasExtension(const char* name, const char* ext)
	{
		if (!ext || !ext[0]) { return true; }
		const char* dot = strrchr(name, '.');
		return dot && strcasecmp(dot + 1, ext) == 0;
	}

	void readDirectory(const char* dir, const char* ext, FileList& fileList)
	{
		char pattern[TFE_MAX_PATH];
		snprintf(pattern, TFE_MAX_PATH, "%s", dir);

		dir_t entry;
		for (int err = dir_findfirst(pattern, &entry); err == 0; err = dir_findnext(pattern, &entry))
		{
			if (entry.d_type == DT_REG && hasExtension(entry.d_name, ext))
			{
				fileList.push_back(entry.d_name);
			}
		}
	}

	void readSubdirectories(const char* dir, FileList& dirList)
	{
		char pattern[TFE_MAX_PATH];
		snprintf(pattern, TFE_MAX_PATH, "%s", dir);

		dir_t entry;
		for (int err = dir_findfirst(pattern, &entry); err == 0; err = dir_findnext(pattern, &entry))
		{
			if (entry.d_type == DT_DIR)
			{
				dirList.push_back(std::string(dir) + entry.d_name + "/");
			}
		}
	}

	// "save:/" is flat: directories always exist, nothing needs to be created.
	bool makeDirectory(const char* dir) { return true; }
	void getCurrentDirectory(char* dir) { strcpy(dir, c_root); }
	void getExecutionDirectory(char* dir) { strcpy(dir, c_root); }
	void setCurrentDirectory(const char* dir) {}

	void getFilePath(const char* filename, char* path)
	{
		memset(path, 0, TFE_MAX_PATH);
		if (!filename) { return; }
		const size_t len = getNameOffset(filename);
		memcpy(path, filename, len);
	}

	void getFileExtension(const char* filename, char* extension)
	{
		memset(extension, 0, TFE_MAX_PATH);
		const char* dot = strrchr(filename, '.');
		if (dot) { strcpy(extension, dot + 1); }
	}

	void getFileNameFromPath(const char* path, char* name, bool includeExt)
	{
		memset(name, 0, TFE_MAX_PATH);
		strcpy(name, path + getNameOffset(path));
		if (!includeExt)
		{
			char* dot = strrchr(name, '.');
			if (dot) { *dot = 0; }
		}
	}

	void copyFile(const char* srcFile, const char* dstFile)
	{
		FILE* src = fopen(srcFile, "rb");
		if (!src) { return; }
		FILE* dst = fopen(dstFile, "wb");
		if (!dst) { fclose(src); return; }

		char buffer[1024];
		size_t count;
		while ((count = fread(buffer, 1, sizeof(buffer), src)) > 0)
		{
			if (fwrite(buffer, 1, count, dst) != count) { break; }
		}
		fclose(dst);
		fclose(src);
	}

	void deleteFile(const char* srcFile)
	{
		remove(srcFile);
	}

	// The ROM filesystem is case sensitive while the game mixes cases, so try
	// the path as given and then with the file name upper- and lower-cased.
	char* findFileNoCase(const char* filename)
	{
		if (fileExistsExact(filename)) { return strdup(filename); }

		char* candidate = strdup(filename);
		const size_t nameOffset = getNameOffset(filename);
		for (char* c = candidate + nameOffset; *c; c++) { *c = toupper(*c); }
		if (fileExistsExact(candidate)) { return candidate; }

		for (char* c = candidate + nameOffset; *c; c++) { *c = tolower(*c); }
		if (fileExistsExact(candidate)) { return candidate; }

		free(candidate);
		return nullptr;
	}

	bool existsNoCase(const char* path)
	{
		char* found = findFileNoCase(path);
		if (!found) { return false; }
		free(found);
		return true;
	}

	bool exists(const char* path)
	{
		return existsNoCase(path);
	}

	bool directoryExits(const char* path, char* outPath)
	{
		// Only the filesystem roots are treated as directories.
		if (strncmp(path, c_root, strlen(c_root)) != 0 && strncmp(path, "save:/", 6) != 0) { return false; }
		if (outPath) { snprintf(outPath, TFE_MAX_PATH, "%s", path); }
		return true;
	}

	u64 getModifiedTime(const char* path)
	{
		return 0;
	}

	void fixupPath(char* path)
	{
		for (char* c = path; *c; c++)
		{
			if (*c == '\\') { *c = '/'; }
		}
	}

	void convertToOSPath(const char* path, char* pathOS)
	{
		memset(pathOS, 0, TFE_MAX_PATH);
		strncpy(pathOS, path, TFE_MAX_PATH - 1);
		fixupPath(pathOS);
	}
}
