#include "rpcApplication.h"
#include "CLI/CLI.hpp"
#include "rpcLogger.h"
#include <CLI/CLI.hpp>

void RpcApplication::Init(int argc, char **argv) {
  if (initialized_)
    return;
  initialized_ = true;
  // 命令行参数解析
  // 可指定配置文件位置
  CLI::App app{"RPC Server"};
  app.add_option("-c,--config", config_path_, "Path to config file");
  CLI11_PARSE(app, argc, argv);

  // 加载配置文件
  auto &inst = GetInstance();
  inst.config_.LoadConfigFile(config_path_.c_str());

  // 读取配置，初始化日志
  auto &config = inst.GetConfig();
  RpcLogger(config.log.log_name, config.log.dir,
            spdlog::level::from_str(config.log.level), config.log.console);
}

RpcApplication &RpcApplication::GetInstance() {
  static RpcApplication inst;
  return inst;
}