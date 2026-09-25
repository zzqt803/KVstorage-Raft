#pragma once

#include <spdlog/async.h>
#include <spdlog/async_logger.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <cstdlib>
#include <memory>
#include <string>
#include <utility>
#include <vector>

class RpcLogger {
public:
  // 全局唯一的初始化入口，构造即完成初始化
  // queue_size : 异步队列容量
  // policy     : 队列满时的策略（默认阻塞，不丢日志）
  // console : true  → 输出到控制台
  //           false → 不输出控制台（配合非空 log_dir 就是"只写文件"）
  explicit RpcLogger(const std::string &name = "rpc",
                     const std::string &log_dir = "logs",
                     spdlog::level::level_enum level = spdlog::level::debug,
                     bool console = true, size_t queue_size = 8192,
                     spdlog::async_overflow_policy policy =
                         spdlog::async_overflow_policy::block) {
    spdlog::init_thread_pool(queue_size, 1);

    // ---------- 按需组装 sinks ----------
    std::vector<spdlog::sink_ptr> sinks;

    if (console) {
      sinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
    }

    if (!log_dir.empty()) {
      sinks.push_back(std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
          log_dir + "/" + name + ".log",
          10 * 1024 * 1024, // 10 MB
          3));              // 保留 3 个历史文件
    }

    // 兜底：两个都关掉会导致无 sink，spdlog 会静默丢弃所有日志
    if (sinks.empty()) {
      sinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
    }

    // ---------- 主 logger：异步 ----------
    auto async = std::make_shared<spdlog::async_logger>(
        name, sinks.begin(), sinks.end(), spdlog::thread_pool(), policy);
    async->set_level(level);
    async->set_pattern(kPattern);
    async->flush_on(spdlog::level::warn);
    logger_ = async;

    // ---------- FATAL logger：同步，复用同一组 sinks，绕过异步队列 ----------
    auto fatal_logger = std::make_shared<spdlog::logger>(
        name + "-fatal", sinks.begin(), sinks.end());
    fatal_logger->set_level(spdlog::level::critical);
    fatal_logger->set_pattern(kPattern);
    fatal_logger->flush_on(spdlog::level::critical);
    fatal_logger_ = fatal_logger;

    spdlog::set_default_logger(logger_);
  }

  ~RpcLogger() {
    // 等异步队列清空，避免退出时丢日志
    if (logger_)
      logger_->flush();
    // 不调用 spdlog::shutdown()，让 spdlog 在进程退出时自行清理
  }

  RpcLogger(const RpcLogger &) = delete;
  RpcLogger &operator=(const RpcLogger &) = delete;

  // ================= 带源码位置的通用接口 =================

  template <typename... Args>
  static void log(spdlog::source_loc loc, spdlog::level::level_enum lvl,
                  spdlog::format_string_t<Args...> fmt, Args &&...args) {
    logger_->log(loc, lvl, fmt, std::forward<Args>(args)...);
  }

  // ================= 各级别快捷方法 =================

  template <typename... Args>
  static void trace(spdlog::format_string_t<Args...> fmt, Args &&...args) {
    logger_->trace(fmt, std::forward<Args>(args)...);
  }

  template <typename... Args>
  static void debug(spdlog::format_string_t<Args...> fmt, Args &&...args) {
    logger_->debug(fmt, std::forward<Args>(args)...);
  }

  template <typename... Args>
  static void info(spdlog::format_string_t<Args...> fmt, Args &&...args) {
    logger_->info(fmt, std::forward<Args>(args)...);
  }

  template <typename... Args>
  static void warn(spdlog::format_string_t<Args...> fmt, Args &&...args) {
    logger_->warn(fmt, std::forward<Args>(args)...);
  }

  template <typename... Args>
  static void error(spdlog::format_string_t<Args...> fmt, Args &&...args) {
    logger_->error(fmt, std::forward<Args>(args)...);
  }

  template <typename... Args>
  static void critical(spdlog::format_string_t<Args...> fmt, Args &&...args) {
    logger_->critical(fmt, std::forward<Args>(args)...);
  }

  // ================= FATAL：同步直出后终止进程 =================

  template <typename... Args>
  [[noreturn]] static void fatal(spdlog::format_string_t<Args...> fmt,
                                 Args &&...args) {
    fatal_logger_->critical(fmt, std::forward<Args>(args)...);
    fatal_logger_->flush(); // 同步 logger，flush 立即完成
    std::abort();
  }

  template <typename... Args>
  [[noreturn]] static void log_fatal(spdlog::source_loc loc,
                                     spdlog::format_string_t<Args...> fmt,
                                     Args &&...args) {
    fatal_logger_->log(loc, spdlog::level::critical, fmt,
                       std::forward<Args>(args)...);
    fatal_logger_->flush();
    std::abort();
  }

  // 手动刷新异步队列（例如 fork 前、进入关键区前）
  static void flush() {
    if (logger_)
      logger_->flush();
  }

private:
  inline static constexpr const char *kPattern =
      "[%Y-%m-%d %H:%M:%S.%e] [%t] [%^%l%$] [%s:%#] %v";

  // 全局只初始化一次，成员用静态即可
  inline static std::shared_ptr<spdlog::logger> logger_;
  inline static std::shared_ptr<spdlog::logger> fatal_logger_;
};

// ================= 便捷宏（自动捕获源码位置） =================

#define RPC_LOG_TRACE(...)                                                     \
  ::RpcLogger::log({__FILE__, __LINE__, __func__}, spdlog::level::trace,       \
                   __VA_ARGS__)

#define RPC_LOG_DEBUG(...)                                                     \
  ::RpcLogger::log({__FILE__, __LINE__, __func__}, spdlog::level::debug,       \
                   __VA_ARGS__)

#define RPC_LOG_INFO(...)                                                      \
  ::RpcLogger::log({__FILE__, __LINE__, __func__}, spdlog::level::info,        \
                   __VA_ARGS__)

#define RPC_LOG_WARN(...)                                                      \
  ::RpcLogger::log({__FILE__, __LINE__, __func__}, spdlog::level::warn,        \
                   __VA_ARGS__)

#define RPC_LOG_ERROR(...)                                                     \
  ::RpcLogger::log({__FILE__, __LINE__, __func__}, spdlog::level::err,         \
                   __VA_ARGS__)

#define RPC_LOG_FATAL(...)                                                     \
  ::RpcLogger::log_fatal({__FILE__, __LINE__, __func__}, __VA_ARGS__)

