//CDB.h
#pragma once
#include <C:\Program Files\MySQL\MySQL Server 8.0\include\mysql.h>
#pragma comment(lib, "libmysql.lib")
#include <string>
#include <unordered_map>

struct stResultSet;
struct stSTMTResultSet;
class CDB
{
  public:
    ~CDB()
    {
        Disconnect();
    }
    bool Connect();
    void Disconnect()
    {
        if (_conn != nullptr)
        {
            mysql_close(_conn);
            _conn = nullptr;
        }
    }
    bool Query(const char *str, stResultSet &out);
    bool Query(const char *str, stSTMTResultSet &out, ...);

    bool ConvertQuery(std::string &str, int len, std::vector<size_t> &type);

  private:
    MYSQL *_conn = nullptr;
};
struct stResultSet
{
    friend class CDB;

    ~stResultSet()
    {
        if (_result != nullptr)
            mysql_free_result(_result);
    }
    bool Fetch();
    const char *GetValue(const char *column);

  private:
    MYSQL_RES *_result = nullptr;
    MYSQL_ROW _row{0};
    std::unordered_map<std::string, unsigned int> _hashmap;
};

struct stSTMTResultSet
{
    friend class CDB;

    ~stSTMTResultSet()
    {
        for (int i = 0; i < _bind.size(); i++)
        {
            free(_bind[i].buffer);
        }

        if (_stmt != nullptr)
            mysql_stmt_close(_stmt);
    }
    bool Fetch();
    const char *GetValue(const char *column);

  private:
    MYSQL_STMT *_stmt = nullptr;
    MYSQL_RES *_result = nullptr;
    std::vector<MYSQL_BIND> _bind{0};

    std::unordered_map<std::string, unsigned int> _hashmap;
};