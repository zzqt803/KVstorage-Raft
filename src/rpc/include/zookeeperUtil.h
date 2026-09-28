#pragma once

#include <condition_variable>
#include <mutex>
#include <string>
#include <vector>
#include <zookeeper/zookeeper.h>

class ZkClient {
public:
  ZkClient();
  ~ZkClient();

  // zkclient启动连接zkserver
  void Start();
  // 在zkserver创建节点,默认临时节点
  void Create(const std::string &path, const std::string &data,
              int state = ZOO_EPHEMERAL);
  // 获取znode节点值
  std::string GetData(const std::string &);
  // 获取子节点数据列表
  // 如果传了 watcher_fn，子节点变化时会触发回调
  std::vector<std::string> GetChildren(const std::string &path,
                                       watcher_fn fn = nullptr,
                                       void *cbContext = nullptr);

private:
  static void GlobalWatcher(zhandle_t *zh, int type, int status,
                            const char *path, void *ctx);
  void OnSessionEvent(int status);

  zhandle_t *zhandle_ = nullptr;
  std::mutex mutex_;
  std::condition_variable cv_;
  bool connected_ = false;
};