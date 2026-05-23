#pragma once
constexpr char CONFIG_REDIS_OP[] = "127.0.0.1";
constexpr short CONFIG_REDIS_PORT = 3306;

enum eContentsConfig
{
    CONFIG_MSG_MAX_LEN = 800, //RINGBUFFER가 2001로 잡음
    CONFIG_NICKNAME_LEN = 20, //RINGBUFFER가 2001로 잡음
};

