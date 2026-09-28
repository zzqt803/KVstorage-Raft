#pragma once

#include "rpcConfig.h"
#include <string>

class RpcApplication {
public:
  static void Init(int argc, char **argv);
  static RpcApplication &GetInstance();
  RpcConfig &GetConfig() { return config_; }
  const RpcConfig &GetConfig() const { return config_; }

  RpcApplication(const RpcApplication &) = delete;
  RpcApplication &operator=(const RpcApplication &) = delete;

private:
  RpcApplication() = default;

  RpcConfig config_;
  inline static std::string config_path_ = "config/rpc-server.toml";
  inline static bool initialized_ = false;
};