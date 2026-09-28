#include <cstring>

#include "parser.h"
#include <TFE_FileSystem/filestream.h>
#include <cstdlib>
#include <algorithm>

namespace
{
	static char s_line[4096];
	enum
	{
		PARSER_WINDOW_SIZE = 16384,
		PARSER_LOOKAHEAD   = 64,	// Covers comment strings and "/*" checks past the current char.
	};
	bool isWhitespace(const char c)
	{
		if (c > 32 && c < 127)
		{
			return false;
		}
		return true;
	}

	bool isSeparator(const char c)
	{
		if (c == '=' || c == ',')
		{
			return true;
		}
		return false;
	}
}

TFE_Parser::TFE_Parser() : m_buffer(nullptr), m_bufferLen(0u), m_enableBlockComments(false), m_blockComment(false), m_enableColorSeperator(false), m_convertToUppercase(false),
	m_streamPath(nullptr), m_window(nullptr), m_winStart(0), m_winLen(0) {}
TFE_Parser::~TFE_Parser()
{
	free(m_window);
	delete m_streamPath;
}

void TFE_Parser::init(const char* buffer, size_t len)
{
	m_buffer = buffer;
	m_bufferLen = len;
	delete m_streamPath;
	m_streamPath = nullptr;
}

bool TFE_Parser::initStream(const FilePath* filePath)
{
	init(nullptr, 0);
	m_winStart = 0;
	m_winLen = 0;

	FileStream file;
	if (!file.open(filePath, Stream::MODE_READ)) { return false; }
	const size_t len = file.getSize();
	file.close();

	if (!m_window)
	{
		// Extra zeroed padding so lookahead past the end of the data reads zeros.
		m_window = (char*)malloc(PARSER_WINDOW_SIZE + PARSER_LOOKAHEAD);
		if (!m_window) { return false; }
	}
	m_streamPath = new FilePath(*filePath);
	m_bufferLen = len;
	return true;
}

// Returns a pointer to the data at absolute position i, with at least PARSER_LOOKAHEAD
// readable bytes after it (zero padded at the end of the data).
const char* TFE_Parser::ptr(size_t i)
{
	if (!m_streamPath) { return m_buffer + i; }

	const size_t winEnd = m_winStart + m_winLen;
	const bool needsFill = i < m_winStart || i >= winEnd ||
		(i + PARSER_LOOKAHEAD > winEnd && winEnd < m_bufferLen);
	if (needsFill)
	{
		// Keep one character of history for the "*/" check.
		m_winStart = i > 0 ? i - 1 : 0;
		size_t count = m_bufferLen > m_winStart ? m_bufferLen - m_winStart : 0;
		if (count > PARSER_WINDOW_SIZE) { count = PARSER_WINDOW_SIZE; }
		m_winLen = 0;
		FileStream file;
		if (count && file.open(m_streamPath, Stream::MODE_READ))
		{
			file.seek(s32(m_winStart));
			m_winLen = file.readBuffer(m_window, u32(count));
			file.close();
		}
		memset(m_window + m_winLen, 0, PARSER_WINDOW_SIZE + PARSER_LOOKAHEAD - m_winLen);
	}
	return m_window + (i - m_winStart);
}

// Enable block comments of the form /*...*/
void TFE_Parser::enableBlockComments()
{
	m_enableBlockComments = true;
}

// Enable : as a seperator but do not remove it.
void TFE_Parser::enableColonSeperator()
{
	m_enableColorSeperator = true;
}

// Add a string representing a comment, such as ";" "#" "//"
void TFE_Parser::addCommentString(const char* comment)
{
	m_commentStrings.push_back(comment);
}

// Convert resulting strings to upper case, defaults to false.
void TFE_Parser::convertToUpperCase(bool enable)
{
	m_convertToUppercase = enable;
}

bool TFE_Parser::isComment(const char* buffer)
{
	const size_t commentCount = m_commentStrings.size();
	const std::string* comments = m_commentStrings.data();
	for (size_t c = 0; c < commentCount; c++)
	{
		if (strncmp(comments[c].c_str(), buffer, comments[c].length()) == 0)
		{
			return true;
		}
	}
	return false;
}

// Read the next non-comment/whitespace line.
const char* TFE_Parser::readLine(size_t& bufferPos, bool skipLeadingWhitespace, bool commentOnlyAtBeginning)
{
	if (bufferPos >= m_bufferLen || m_bufferLen < 1) { return nullptr; }

	// Keep reading lines until either one has real content or we reach the end of the buffer.
	bool lineHasContent = false;
	s32 skip = -1;
	while (!lineHasContent && bufferPos < m_bufferLen)
	{
		s_line[0] = 0;
		size_t linePos = 0;
		bool inComment = false;
		for (size_t i = bufferPos; i < m_bufferLen; i++)
		{
			bufferPos = i + 1;

			if (m_enableBlockComments && i > 0 && at(i-1) == '*' && at(i) == '/')
			{
				m_blockComment = false;
			}
			else if (m_enableBlockComments && at(i) == '/' && at(i+1) == '*')
			{
				m_blockComment = true;
			}
			else if (at(i) == '\n' || at(i) == '\r')
			{
				// search for the next valid character, then end it.
				for (size_t ii = i + 1; ii < m_bufferLen; ii++)
				{
					if (at(ii) != '\n' && at(ii) != '\r')
					{
						bufferPos = ii;
						break;
					}
				}
				break;
			}
			else if (!inComment && !m_blockComment)
			{
				// is this the beginning of a comment?
				if (!commentOnlyAtBeginning)
				{
					inComment = isComment(ptr(i));
				}

				// if not in a comment, go ahead and add to the line.
				if (!inComment)
				{
					if (m_convertToUppercase)
					{
						s_line[linePos++] = toupper(at(i));
					}
					else
					{
						s_line[linePos++] = at(i);
					}
				}
			}
		}
		s_line[linePos] = 0;

		// check to see if the line has any content.
		lineHasContent = false;
		skip = -1;
		for (size_t i = 0; i < linePos; i++)
		{
			// Content is any non-white space character.
			if (!isWhitespace(s_line[i]))
			{
				// Is this a comment?
				if (commentOnlyAtBeginning && isComment(&s_line[i]))
				{
					break;
				}
				if (skip < 0) { skip = s32(i); }
				lineHasContent = true;
				break;
			}
		}
	}

	if (lineHasContent && skipLeadingWhitespace && skip > 0)
	{
		return &s_line[skip];
	}

	return s_line[0]!=0 ? s_line : nullptr;
}

// Split a line into tokens using space, comma or equals as separators.
// Note strings with spaces still work, they need to be closed in quotes, which are removed upon tokenizing.
void TFE_Parser::tokenizeLine(const char* line, TokenList& tokens)
{
	tokens.clear();

	const size_t len = strlen(line);
	// first move past leading whitespace and ending white space.
	size_t start = 0, end = 0;
	for (size_t c = 0; c < len; c++)
	{
		if (!isWhitespace(line[c]))
		{
			if (start == 0 && end == 0) { start = c; }
			end = c + 1;
		}
	}

	// next start reading tokens.
	bool inQuote = false;
	char curToken[1024];
	size_t curTokenPos = 0;
	// TODO: Add an option to allow white space in tokens when not in quotes, but still remove trailing/ending whitespace.
	// This is useful for names.
	for (size_t c = start; c < end; c++)
	{
		if (line[c] == '"')
		{
			if (inQuote && curTokenPos == 0)
			{
				tokens.push_back("");
			}
			inQuote = !inQuote;
		}
		else if (!inQuote && (isWhitespace(line[c]) || isSeparator(line[c])))
		{
			curToken[curTokenPos] = 0;
			if (curTokenPos)
			{
				tokens.push_back(curToken);
			}

			curTokenPos = 0;
			curToken[0] = 0;
		}
		else if (!inQuote && m_enableColorSeperator && line[c] == ':')
		{
			curToken[curTokenPos++] = line[c];

			curToken[curTokenPos] = 0;
			if (curTokenPos)
			{
				tokens.push_back(curToken);
			}

			curTokenPos = 0;
			curToken[0] = 0;
		}
		else
		{
			curToken[curTokenPos++] = line[c];
		}
	}

	if (curTokenPos)
	{
		curToken[curTokenPos] = 0;
		tokens.push_back(curToken);
	}
}
