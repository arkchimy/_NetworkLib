#include <iostream>
#include "Parser_lib.h"

#define WRITE_LOG(filename,log) \
{\
	while (1)\
		if(WriteLog(filename,log))\
			break; \
}
#define WRITE_TAGLOG(filename,log,tag) \
{\
	while (1)\
		if(WriteLog(filename,log,tag))\
			break; \
}

std::list<const WCHAR *> ParserManager::_tags;

const WCHAR* _gLogString[size_t(ESearchType::MAX)] =
{
	L"Success",
	L" ' 태그 '를 찾지 못했습니다 \n",
	L"세미 콜론이 빠져있습니다. \n",
	L" ' = '이 빠져있습니다.\n",
	L"주석이 마지막 줄에 존재합니다.\n",
	L" ",
	L"따음표 부재.\n",
	L"버퍼 오버플로우가 발생했습니다.\n",

};

bool WriteLog(const WCHAR* logFilename, const WCHAR* log, const WCHAR* tag = L"")
{
	if (ParserManager::existChk(tag))
		return true;
	ParserManager::_tags.push_back(tag);

	FILE* logfile;
	_wfopen_s(&logfile, logFilename, L"a+, ccs=UTF-16LE");
	if (logfile == nullptr)
	{
		//LogFile 이 생성 안됨.
		return false;
	}
	fwrite(tag, sizeof(WCHAR), wcslen(tag), logfile);
	fwrite(log, sizeof(WCHAR), wcslen(log), logfile);
	fflush(logfile);
	fclose(logfile);

	return true;
}
void StringToNumber(const WCHAR *str, size_t len, int &out)
{
    out = 0;

    bool minus = false;
    bool signedChk = false;

    if (str[0] == L'-')
    {
        minus = true;
        signedChk = true;
    }
    else if (str[0] == L'+')
    {
        minus = false;
        signedChk = true;
    }

    if (signedChk)
    {
        for (size_t i = 0; i < len - 1; i++)
        {
            int num = static_cast<int>((str[len - i - 1] - L'0') * pow(10, i));
            out += num;
        }
    }
    else
    {
        for (size_t i = 0; i < len; i++)
        {
            int num = static_cast<int>((str[len - i - 1] - L'0') * pow(10, i));
            out += num;
        }
    }
    if (minus)
    {
        out *= -1;
    }
}
void StringToNumber(const WCHAR *str, size_t len, unsigned short &out)
{
    out = 0;

    bool minus = false;
    bool signedChk = false;

    if (str[0] == L'-')
    {
        minus = true;
        signedChk = true;
    }
    else if (str[0] == L'+')
    {
        minus = false;
        signedChk = true;
    }

    if (signedChk)
    {
        for (size_t i = 0; i < len - 1; i++)
        {
            int num = static_cast<int>((str[len - i - 1] - L'0') * pow(10, i));
            out += num;
        }
    }
    else
    {
        for (size_t i = 0; i < len; i++)
        {
            int num = static_cast<int>((str[len - i - 1] - L'0') * pow(10, i));
            out += num;
        }
    }
    if (minus)
    {
        out *= -1;
    }
}
void StringToNumber(const WCHAR *str, size_t len, short &out)
{
    out = 0;

    bool minus = false;
    bool signedChk = false;

    if (str[0] == L'-')
    {
        minus = true;
        signedChk = true;
    }
    else if (str[0] == L'+')
    {
        minus = false;
        signedChk = true;
    }

    if (signedChk)
    {
        for (size_t i = 0; i < len - 1; i++)
        {
            int num = static_cast<int>((str[len - i - 1] - L'0') * pow(10, i));
            out += num;
        }
    }
    else
    {
        for (size_t i = 0; i < len; i++)
        {
            int num = static_cast<int>((str[len - i - 1] - L'0') * pow(10, i));
            out += num;
        }
    }
    if (minus)
    {
        out *= -1;
    }
}
void SubString(const WCHAR* str, size_t len, WCHAR* out)
{
	memcpy(out, str, sizeof(WCHAR) * len);
	out[len] = 0;
}
Parser::Parser()
	:logFilename(L"LogError.txt")
{
	/*FILE* logfile;
	_wfopen_s(&logfile, logFilename, L"w, ccs=UTF-16LE");
	if (logfile == nullptr)
	{
		__debugbreak();
	}
	fclose(logfile);*/
}

Parser::~Parser()
{
	free(buffer);
	fclose(OpenReadFile);
}

bool Parser::LoadFile(const WCHAR* filename)
{
	FILE* file;
	

	_wfopen_s(&file, filename, L"r, ccs=UTF-16LE");
	OpenReadFile = file;
	if (file == nullptr)
	{
		WRITE_LOG(logFilename, L"파일 Load실패 하였습니다.\n");
		return false;
	}

	fseek(file, SEEK_SET, SEEK_END);
	size_t len = ftell(file);//  Hxd 의 Byte 갯수를 읽음.

	buffer = (WCHAR*)malloc(len + sizeof(WCHAR));
	if (buffer == nullptr)
	{
		__debugbreak();
		return false;
	}
	fseek(file, SEEK_SET, 0);
	len = fread(buffer, sizeof(WCHAR), len / sizeof(WCHAR), file);// txt의 경우 줄바꿈의 경우 OD 0A 가 0D로 바뀌기 때문에
	buffer[len] = 0;
	return true;
}
ESearchType Parser::SearchValue(const WCHAR** left, size_t& _len)
{
	const WCHAR* right = *left;
	bool misstakeChk = true; // 값이 없는  Key가 존재하는지 체크
	const WCHAR* pLimit = buffer + wcslen(buffer);
	ESearchType result = ESearchType::SucceseSearch;

	while (1)
	{
		if (
			**left == L' ' ||
			**left == L',' ||
			**left == L'.' ||
			**left == L'\t' ||
			**left == L'\n' ||
			**left == L'\r'
			)
		{
			(*left)++;
		}
		else if (**left == L'=')
		{
			//= 를 두번 만났을 경우  세미콜론 미스
			if (misstakeChk == false)
			{
				result = ESearchType::MissSemicolon;
			}
			misstakeChk = false;
			(*left)++;
		}
		else if (misstakeChk)
		{	//대입을 못만났는데 값을 만났을 경우
			return ESearchType::MissEqule;
		}
		else if (**left == L'\"')
		{
			result = ESearchType::StringValue;
			(*left)++;
			break;
		}
		else
			break;
	}


	const WCHAR* pStrend = buffer + wcslen(buffer);

	right = *left;
	// 위 로직에서 left는 원하는 Value의 첫번째 주소를 가리킨다.


	if (result == ESearchType::StringValue)
	{
		while (1)
		{
			if (right == pLimit)
			{
				return ESearchType::HasNotquotation;
			}
			else if (*right == L'\"')
			{
				break;
			}
			right++;

		}
	}

	while (1)
	{
		if (right == pStrend)
		{
			result = ESearchType::HasNotTag;
			break;
		}
		if (*right == L';')
		{
			if (result == ESearchType::StringValue)
			{
				right--;

			}
			result = ESearchType::SucceseSearch;
			break;
		}
		if (*right == L'\n')
		{
			result = ESearchType::MissSemicolon;
			break;
		}
		if (result == ESearchType::StringValue && *right == L'\"')
		{

		}
		right++;

	}
	_len = right - *left;
	return result;
}
bool Parser::GetValue(const WCHAR *tag, unsigned short &out)
{
    const WCHAR *left;
    size_t len = 0;

    bool misstakeChk = true; // 값이 없는  Key가 존재하는지 체크

    ESearchType type = ESearchType(SearchTag(&left, tag));

    switch (type)
    {
    case ESearchType::SucceseSearch:
    {
        type = SearchValue(&left, len);

        if (type != ESearchType::SucceseSearch)
        {
            WRITE_TAGLOG(logFilename, _gLogString[size_t(type)], tag);
            return false;
        }
        StringToNumber(left, len, out);
        wprintf(L"%s    :  %d \n", tag, out);

        break;
    }
    default:
    {
        WRITE_TAGLOG(logFilename, _gLogString[size_t(type)], tag);
        return false;
    }
    }

    return true;
}
bool Parser::GetValue(const WCHAR *tag, short &out)
{
    const WCHAR *left;
    size_t len = 0;

    bool misstakeChk = true; // 값이 없는  Key가 존재하는지 체크

    ESearchType type = ESearchType(SearchTag(&left, tag));

    switch (type)
    {
    case ESearchType::SucceseSearch:
    {
        type = SearchValue(&left, len);

        if (type != ESearchType::SucceseSearch)
        {
            WRITE_TAGLOG(logFilename, _gLogString[size_t(type)], tag);
            return false;
        }
        StringToNumber(left, len, out);
        wprintf(L"%s    :  %d \n", tag, out);

        break;
    }
    default:
    {
        WRITE_TAGLOG(logFilename, _gLogString[size_t(type)], tag);
        return false;
    }
    }

    return true;
}
bool Parser::GetValue(const WCHAR* tag, int& out)
{
	const WCHAR* left;
	size_t len = 0;

	bool misstakeChk = true; // 값이 없는  Key가 존재하는지 체크

	ESearchType type = ESearchType(SearchTag(&left, tag));

	switch (type)
	{
	case ESearchType::SucceseSearch:
	{
		type = SearchValue(&left, len);

		if (type != ESearchType::SucceseSearch)
		{
			WRITE_TAGLOG(logFilename, _gLogString[size_t(type)], tag);
			return false;

		}
		StringToNumber(left, len, out);
		wprintf(L"%s    :  %d \n", tag, out);

		break;
	}
	default:
	{
		WRITE_TAGLOG(logFilename, _gLogString[size_t(type)], tag);
		return false;
	}
	}


	return true;
}

bool Parser::GetValue(const WCHAR *tag, bool &out)
{
    return false;
}

bool Parser::GetValue(const WCHAR* tag, WCHAR* out, size_t bufferSize)
{
	const WCHAR* left;
	size_t len = 0;

	bool misstakeChk = true; // 값이 없는  Key가 존재하는지 체크

	ESearchType type = ESearchType(SearchTag(&left, tag));

	switch (type)
	{
	case ESearchType::SucceseSearch:
	{
		type = SearchValue(&left, len);
		//버퍼 사이즈 체크
		if (bufferSize <= (len + 1) )
			type = ESearchType::BufferOverflow;
		else
		{
			SubString(left, len, out);
		}
		wprintf(L"%ls    :   %ls \n", tag, out);
		break;
	}
	default:
	{
		WRITE_TAGLOG(logFilename, _gLogString[size_t(type)], tag);
		return false;
	}
	}


	return true;
}



size_t Parser::SearchTag(const WCHAR** pleft, const WCHAR* tag)
{
	//LE 일경우 FF FE  가 존재하기 buffer 시작 지점에서 한 칸 때문에 건너뜀.
	WCHAR* left = buffer + 1;
	WCHAR* right = left;

	size_t len = wcslen(tag);
	size_t limit = wcslen(buffer);

	WCHAR* pStrend = buffer + wcslen(buffer);

	ESearchType result = ESearchType::SucceseSearch;

	WCHAR* key;
	bool sameChk = false;
	bool bComment = false;


	do
	{
		//Tag의 시작점 잡기.
		while (1)
		{
			// 주석 발견
			if (*left == L'/' && *(left + 1) == L'/')
			{
				while (*left != L'\n')
				{

					if (left == pStrend)
					{
						// 줄바꿈을 못만난 채로 파일 끝에 도달하는 경우.
						return size_t(ESearchType::HasNotLineBreak);
					}
					left++;
				}
				left++;
			}
			if (
				*left == L' ' ||
				*left == L',' ||
				*left == L'.' ||
				*left == L'\"' ||
				*left == L'\t' ||
				*left == L'\n' ||
				*left == L'\r'
				)
			{
				left++;
			}
			else if (left == pStrend)
				return size_t(ESearchType::HasNotTag);
			else
				break;
		}
		right = left;

		while (1)
		{
			if (
				*right == L'\n' ||
				*right == L' ' ||
				*right == L'\t' ||
				*right == L'='
				)
			{
				break;
			}
			right++;
		}

		if (right - left != wcslen(tag))
		{
			left = right;
			while (1)
			{
				if (left == pStrend)
				{
					return size_t(ESearchType::HasNotTag);
				}
				else if (*left == L'\n')
				{
					break;
				}
				left++;
			}
			if (*left == L'\n')
			{
				left++;
				continue;
			}
		}

		key = (WCHAR*)malloc(sizeof(WCHAR) * (right - left) + sizeof(WCHAR));

		if (key == nullptr)
			__debugbreak();

		memcpy(key, left, (right - left) * sizeof(WCHAR));
		key[right - left] = 0;

		sameChk = true;
		for (size_t i = 0; i < len; i++)
		{
			if (key[i] != tag[i])
			{
				sameChk = false;
				break;
			}
		}
		if (sameChk == false)
		{
			while (1)
			{
				if (*left == '\n' || *left == L';')
				{
					left++;
					right = left;
					break;
				}
				else if (left == pStrend)
				{
					result = ESearchType::HasNotTag;
					break;
				}
				left++;
			}
		}
		free(key);

	} while (sameChk == false);

	*pleft = right + 1;
	return size_t(result);
}

bool ParserManager::existChk(const WCHAR* _target)
{
	for (const WCHAR* _data : _tags)
	{
		if (_data == _target)
			return true;
	}
	return false;
}
