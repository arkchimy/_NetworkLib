#pragma once
#include <cpp_redis/cpp_redis>

#pragma comment(lib, "cpp_redis.lib")
#pragma comment(lib, "tacopie.lib")
#pragma comment(lib, "ws2_32.lib")

constexpr char CONFIG_REDIS_IP[] = "192.168.45.97";
constexpr short CONFIG_REDIS_PORT = 6379;

enum eContentsConfig
{
    CONFIG_MSG_MAX_LEN = 800, //RINGBUFFER가 2001로 잡음
    CONFIG_NICKNAME_LEN = 20, //RINGBUFFER가 2001로 잡음
    CONFIG_TOKENKEY_LEN = 20,
};

