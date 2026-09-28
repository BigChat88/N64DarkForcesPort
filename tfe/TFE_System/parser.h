#pragma once
//////////////////////////////////////////////////////////////////////
// The Force Engine System Library
// System functionality, such as timers and logging.
//////////////////////////////////////////////////////////////////////

#include "types.h"
#include <vector>
#include <string>

struct FilePath;
typedef std::vector<std::string> TokenList;

class TFE_Parser
{
public:
	TFE_Parser();
	~TFE_Parser();

	void init(const char* buffer, size_t len);
	// Stream a file through a small window instead of requiring the whole file in memory.
	// The file is re-opened on every window refill, so other files (even from the same
	// archive) may be opened and closed while parsing.
	bool initStream(const FilePath* filePath);

	// Enable block comments of the form /*...*/
	void enableBlockComments();

	// Enable : as a seperator but do not remove it.
	void enableColonSeperator();

	// Add a string representing a comment, such as ";" "#" "//"
	void addCommentString(const char* comment);

	// Convert resulting strings to upper case, defaults to false.
	void convertToUpperCase(bool enable);

	// Read the next non-comment/whitespace line.
	const char* readLine(size_t& bufferPos, bool skipLeadingWhitespace = false, bool commentOnlyAtBeginning = false);
	// Split a line into tokens using space, comma or equals as separators.
	// Note strings with spaces still work, they need to be closed in quotes, which are removed upon tokenizing.
	void tokenizeLine(const char* line, TokenList& tokens);

private:
	const char* m_buffer;
	size_t m_bufferLen;
	TokenList m_commentStrings;
	bool m_enableBlockComments;
	bool m_blockComment;
	bool m_enableColorSeperator;
	bool m_convertToUppercase;

	// Streaming window (only used when m_streamPath is set).
	FilePath* m_streamPath;
	char*   m_window;
	size_t  m_winStart;
	size_t  m_winLen;

private:
	bool isComment(const char* buffer);
	const char* ptr(size_t i);
	char at(size_t i) { return *ptr(i); }
};
