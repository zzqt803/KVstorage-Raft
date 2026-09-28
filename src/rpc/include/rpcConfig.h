#pragma once

#include <string>

struct ServerConfig {
  std::string name = "RpcProvider";
  std::string host = "0.0.0.0";
  uint16_t port = 8888;
  int threads = 4;
};

struct LogConfig {
  std::string level = "info";
  std::string log_name = "rpc.log";
  std::string dir = "logs";
  bool console = true;
};

struct ZkConfig {
  std::string host = "127.0.0.1";
  uint16_t port = 8803;
};

class RpcConfig {
public:
  /**
   * @brief 加载配置文件
   * 
   * @param config_file 
   */
  void LoadConfigFile(const char *config_file);
  /**
   * @brief for Debug: print config info
   * 
   */
void dump() const;
  ServerConfig server;
  LogConfig log;
  ZkConfig zookeeper;
};