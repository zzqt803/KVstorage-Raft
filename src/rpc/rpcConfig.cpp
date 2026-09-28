#include "rpcConfig.h"
#include "rpcLogger.h"
#include <cstdlib>
#include <exception>
#include <toml.hpp>
#include <toml11/parser.hpp>
#include <toml11/types.hpp>

void RpcConfig::LoadConfigFile(const char *config_file) {
  if (config_file == nullptr || std::strlen(config_file) == 0) {
    RpcLogger::fatal("RpcConfig::LoadConfigFile: config file path is empty");
  }

  /// 解析文件
  toml::value tbl;
  try {
    tbl = toml::parse(config_file);
  } catch (const toml::syntax_error &e) {
    // 语法错误
    RpcLogger::fatal(
        "RpcConfig::LoadConfigFile: config syntax error in '{}': {}",
        config_file, e.what());
  } catch (const std::exception &e) {
    // 文件不存在、权限不足等
    RpcLogger::fatal("RpcConfig::LoadConfigFile: failed to load '{}': {}",
                     config_file, e.what());
  }

  /// 解析[server]
  if (tbl.contains("server")) {
    const auto &s = tbl.at("server");
    if (!s.is_table()) {
      RpcLogger::fatal(
          "RpcConfig::LoadConfigFile: config 'server' must be a table");
    }
    try {
      server.name = toml::find_or<std::string>(s, "name", server.name);
      server.host = toml::find_or<std::string>(s, "host", server.host);
      server.port = static_cast<uint16_t>(
          toml::find_or<int>(s, "port", static_cast<int>(server.port)));
      server.threads = toml::find_or<int>(s, "threads", server.threads);
    } catch (const toml::type_error &e) {
      RpcLogger::fatal(
          "RpcConfig::LoadConfigFile: config type error in [server]: {}",
          e.what());
    }
  }

  /// 解析[log]
  if (tbl.contains("log")) {
    const auto &l = tbl.at("log");
    if (!l.is_table()) {
      RpcLogger::fatal(
          "RpcConfig::LoadConfigFile: config 'log' must be a table");
    }
    try {
      log.level = toml::find_or<std::string>(l, "level", log.level);
      log.log_name = toml::find_or<std::string>(l, "log_name", log.log_name);
      log.dir = toml::find_or<std::string>(l, "dir", log.dir);
      log.console = toml::find_or<bool>(l, "console", log.console);
    } catch (const toml::type_error &e) {
      RpcLogger::fatal(
          "RpcConfig::LoadConfigFile: config type error in [log]: {}",
          e.what());
    }
  }

  /// 解析[zookeeper]
  if (tbl.contains("zookeeper")) {
    const auto &z = tbl.at("zookeeper");
    if (!z.is_table()) {
      RpcLogger::fatal(
          "RpcConfig::LoadConfigFile: config 'zookeeper' must be a table");
    }
    try {
      zookeeper.host = toml::find_or<std::string>(z, "host", zookeeper.host);
      zookeeper.port = static_cast<uint16_t>(
          toml::find_or<int>(z, "port", static_cast<int>(zookeeper.port)));
    } catch (const toml::type_error &e) {
      RpcLogger::fatal(
          "RpcConfig::LoadConfigFile: config type error in [zookeeper]: {}",
          e.what());
    }
  }
}

void RpcConfig::dump() const {
  std::fprintf(stdout,
               "[config]\n"
               "  server.host    = %s\n"
               "  server.port    = %u\n"
               "  server.threads = %d\n"
               "  log.level      = %s\n"
               "  log.log_name   = %s\n"
               "  log.dir        = %s\n"
               "  log.console    = %s\n"
               "  zookeeper.host = %s\n"
               "  zookeeper.port = %u\n",
               server.host.c_str(), server.port, server.threads,

               log.level.c_str(), log.log_name.c_str(),
               log.dir.empty() ? "(none)" : log.dir.c_str(),
               log.console ? "true" : "false",

               zookeeper.host.c_str(), zookeeper.port);
}