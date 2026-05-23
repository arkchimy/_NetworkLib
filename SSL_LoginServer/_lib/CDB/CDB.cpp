// CDB.cpp : 정적 라이브러리를 위한 함수를 정의합니다.
//

#include "pch.h"
#include "CDB.h"
#include "../Parser_lib/Parser_lib.h"
#include <map>

thread_local CDB db;
// TODO: 라이브러리 함수의 예제입니다.
void fnCDB()
{
    CDB db;
    if (db.Connect() == false)
        __debugbreak();
    {
        // RAII로 mysql_query 에서 내부적으로 생성된 버퍼를 제거하기 위한 자료형
        // 소멸자로 mysql_free_result 를 호출해 줌.
        stResultSet stRowSet;
        bool bSuccess;

        bSuccess = db.Query("SELECT * FROM ACCOUNT", stRowSet);
        if (bSuccess)
        {
            while (1)
            {
                // 모든 값이 char* (문자열)로 옵니다. 숫자가 필요하면 atoi()로 변환
                if (stRowSet.Fetch() == false)
                    break;

                int accountNo = atoi(stRowSet.GetValue("AccountNo"));
                std::string ID = stRowSet.GetValue("ID");
                std::string PW = stRowSet.GetValue("PW");

                printf("AcccountNo : %d , ID : %s , PW : %s \n", accountNo, ID.c_str(), PW.c_str());
            }
        }
        {
            stSTMTResultSet stRow;
            db.Query("SELECT * FROM Account WHERE ID = %s AND PW = %s", stRow, "testUser", "testPass");
            while (stRow.Fetch())
            {
                int accountNo = atoi(stRow.GetValue("AccountNo"));
                std::string ID = stRow.GetValue("ID");
                std::string PW = stRow.GetValue("PW");

                printf("AcccountNo : %d , ID : %s , PW : %s \n", accountNo, ID.c_str(), PW.c_str());
            }
        }
    }
}

#define RT_ASSERT(x)        \
    do                      \
    {                       \
        if (!(x))           \
            __debugbreak(); \
    } while (0)

enum enDB
{
    HostLen = 20,
    UserLen = 20,
    PasswordLen = 20,
    dbNameLen = 20,
};
bool CDB::Connect()
{
    _conn = mysql_init(nullptr);
    RT_ASSERT(_conn != nullptr);

    char host[HostLen];
    char user[UserLen];
    char pass[PasswordLen];
    char dbName[dbNameLen];
    int port;

    // parser 밑 변환.
    {
        wchar_t whost[HostLen];
        wchar_t wuser[UserLen];
        wchar_t wpass[PasswordLen];
        wchar_t wdbName[dbNameLen];

        Parser parser;
        parser.LoadFile(L"config.txt");
        parser.GetValue(L"DB_Host", whost, HostLen);
        parser.GetValue(L"DB_User", wuser, UserLen);
        parser.GetValue(L"DB_PW", wpass, PasswordLen);
        parser.GetValue(L"DB_Name", wdbName, dbNameLen);
        parser.GetValue(L"DB_port", port);

        size_t i;
        wcstombs_s(&i, host, HostLen, whost, HostLen);
        wcstombs_s(&i, user, UserLen, wuser, UserLen);
        wcstombs_s(&i, pass, PasswordLen, wpass, PasswordLen);
        wcstombs_s(&i, dbName, dbNameLen, wdbName, dbNameLen);
    }
    MYSQL *ret = mysql_real_connect(_conn, host, user, pass, dbName, port, nullptr, 0);
    if (ret == nullptr)
    {
        printf(" [CDB] 연결 실패 : errno = %u , err = %s \n", mysql_errno(_conn), mysql_error(_conn));
        mysql_close(_conn); // 실패해도 init한 건 정리해야 함
        return false;
    }
    _conn = ret;
    return true;
}

bool CDB::Query(const char *str, stResultSet &out)
{
    mysql_query(_conn, str);

    MYSQL_RES *result = mysql_store_result(_conn);
    // MYSQL_RES* (유효한 포인터)
    // nullptr -> a. INSERT/UPDATE/DELETE 같이 결과 행이 없는 쿼리
    // nullptr -> b. 에러발생 "비정상상황"
    if (result == nullptr)
    {
        if (mysql_errno(_conn) != 0)
        {
            // Log를 작성하기.
            __debugbreak();
        }
        return false;
    }

    out._result = result;
    // 외부에서 컬럼명으로 접근할 수 잇도록
    // [컬럼명] =  인덱스 매핑
    auto fields = mysql_fetch_fields(out._result);
    auto fieldCnt = mysql_num_fields(out._result);
    for (unsigned int i = 0; i < fieldCnt; i++)
    {
        out._hashmap.insert({fields[i].name, i});
    }
    return true;
}

bool CDB::Query(const char *str, stSTMTResultSet &out, ...)
{

    std::vector<size_t> typeList;
    std::string temp = str;

    {
        int len = static_cast<int>(temp.length());

        RT_ASSERT(len != 0);

        //  포멧팅 제거와 들어오는 순서 확인.
        if (ConvertQuery(temp, len, typeList) == false)
        {
            return false;
        }
    }
    {
        out._stmt = mysql_stmt_init(_conn);

        unsigned long len = static_cast<unsigned long>(strlen(temp.c_str()));
        if (mysql_stmt_prepare(out._stmt, temp.c_str(), len))
        {

            printf("\n\n  %s , %d \n\n",
                   mysql_stmt_error(out._stmt), mysql_stmt_errno(out._stmt));
            mysql_stmt_close(out._stmt);
            return false;
        }
        va_list va;
        va_start(va, str);
        std::vector<MYSQL_BIND> binds;
        std::vector<const char *> arg_cstr;
        std::vector<int> arg_int;

        arg_cstr.reserve(typeList.size());
        arg_int.reserve(typeList.size());

        binds.resize(typeList.size(), {0});
        ZeroMemory(binds.data(), sizeof(MYSQL_BIND) * typeList.size());

        for (int i = 0; i < typeList.size(); i++)
        {
            switch (typeList[i])
            {
            case 0:
            {
                const char *val = va_arg(va, const char *);
                arg_cstr.push_back(val);
                binds[i].buffer_type = MYSQL_TYPE_STRING;
                binds[i].buffer = (void *)arg_cstr.back();
                binds[i].buffer_length = static_cast<unsigned long>(strlen(val));
            }
            break;
            case 1:
            {
                int val = va_arg(va, int);
                arg_int.push_back(val);

                binds[i].buffer_type = MYSQL_TYPE_LONG;
                binds[i].buffer = &arg_int.back();
                binds[i].buffer_length = sizeof(val);
                break;
            }
            }
        }
        va_end(va);
        // 매개변수 넘겨줌.
        mysql_stmt_bind_param(out._stmt, binds.data());
        mysql_stmt_execute(out._stmt);

        // 결과 받아오기.
        out._result = mysql_stmt_result_metadata(out._stmt);

        auto fields = mysql_fetch_fields(out._result);
        auto fieldCnt = mysql_num_fields(out._result);
        for (unsigned int i = 0; i < fieldCnt; i++)
        {
            out._hashmap.insert({fields[i].name, i});
        }

        out._bind.resize(fieldCnt);
        for (unsigned int i = 0; i < fieldCnt; i++)
        {
            unsigned long strMaxLen = fields[i].length;

            out._bind[i].buffer = malloc(strMaxLen);
            out._bind[i].buffer_type = MYSQL_TYPE_STRING;
            out._bind[i].buffer_length = strMaxLen;
        }

        mysql_free_result(out._result);
        mysql_stmt_bind_result(out._stmt, out._bind.data());
        mysql_stmt_store_result(out._stmt);
    }
    return true;
}

bool CDB::ConvertQuery(std::string &str, int len, std::vector<size_t> &type)
{
    std::string replaceStr = "?";
    std::map<size_t, int> m;
    std::string stringTarget = "%s";
    std::string intTarget = "%d";
    {
        size_t startPos = 0;
        size_t idx = str.find(stringTarget, startPos);

        while (idx != std::string::npos)
        {
            m.insert({idx, 0});
            startPos = idx + 1;
            idx = str.find(stringTarget, startPos);
        }
    }

    {
        size_t startPos = 0;
        size_t idx = str.find(intTarget, startPos);
        while (idx != std::string::npos)
        {
            m.insert({idx, 1});
            startPos = idx + 1;
            idx = str.find(intTarget, startPos);
        }
    }
    std::vector<size_t> idxs;
    for (auto &idx : m)
    {
        type.push_back(idx.second);
        idxs.push_back(idx.first);
    }

    for (int i = (int)idxs.size() - 1; i >= 0; --i)
    {
        str.replace(idxs[i], 2, replaceStr.c_str());
    }

    return true;
}

bool stResultSet::Fetch()
{
    RT_ASSERT(_result != nullptr);

    MYSQL_ROW row = mysql_fetch_row(_result);
    if (row == nullptr)
        return false;
    _row = row;

    return true;
}

const char *stResultSet::GetValue(const char *colunm)
{
    std::string str = colunm;
    auto iter = _hashmap.find(str);
    if (iter == _hashmap.end())
    {
        __debugbreak();
        return nullptr;
    }
    return _row[iter->second];
}
bool stSTMTResultSet::Fetch()
{
    int retval;
    RT_ASSERT(_stmt != nullptr);
    for (int i = 0; i < _bind.size(); i++)
    {
        ZeroMemory(_bind[i].buffer, _bind[i].buffer_length);
    }
    retval = mysql_stmt_fetch(_stmt);
    if (retval == MYSQL_NO_DATA)
        return false;

    return true;
}

const char *stSTMTResultSet::GetValue(const char *colunm)
{
    std::string str = colunm;
    auto iter = _hashmap.find(str);
    if (iter == _hashmap.end())
    {
        __debugbreak();
        return nullptr;
    }
    return static_cast<const char *>(_bind[iter->second].buffer);
}
