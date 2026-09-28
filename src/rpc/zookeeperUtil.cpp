#include "zookeeperUtil.h"
#include "rpcApplication.h"
#include "rpcLogger.h"
#include <chrono>
#include <mutex>
#include <zookeeper/zookeeper.h>
#include <zookeeper/zookeeper.jute.h>

ZkClient::ZkClient() = default;

ZkClient::~ZkClient() {
  if (zhandle_ != nullptr) {
    zookeeper_close(zhandle_);
  }
}
void ZkClient::Start() {
  auto &config = RpcApplication::GetInstance().GetConfig();
  std::string connstr =
      config.zookeeper.host + ":" + std::to_string(config.zookeeper.port);

  // init操作是异步的，由一个后台线程通知
  // 所以要在回调函数用条件变量和锁控制
  zhandle_ =
      zookeeper_init(connstr.c_str(), GlobalWatcher, 6000, nullptr, this, 0);
  if (!zhandle_) {
    RpcLogger::fatal("ZkClient::Start: zookeeper init error");
  }

  // 6s没连接成功就认为失败
  std::unique_lock<std::mutex> lock(mutex_);
  bool ok = cv_.wait_for(lock, std::chrono::seconds(6),
                         [this] { return connected_; });
  if (!ok) {
    RpcLogger::fatal("ZkClient::Start: zookeeper connect timeout");
  }

  RpcLogger::info("ZkClient::Start: zookeeper connected: {}", connstr);
}

/**
 * @brief 静态函数，通过参数中的this指针来改变实例的状态
 *
 */
void ZkClient::GlobalWatcher(zhandle_t * /*zh*/, int type, int status,
                             const char * /*path*/, void *ctx) {
  auto *self = static_cast<ZkClient *>(ctx);
  if (!self)
    return;

  if (type == ZOO_SESSION_EVENT) {
    self->OnSessionEvent(status);
  }
}

void ZkClient::OnSessionEvent(int status) {
  if (status == ZOO_CONNECTED_STATE) {
    std::lock_guard<std::mutex> lock(mutex_);
    connected_ = true;
  }
  cv_.notify_one(); // 唤醒Start() 里等待的主线程
}

/**
 * @brief
 *
 * @param path 节点路径
 * @param data ip:port
 * @param state 节点类型
 */
void ZkClient::Create(const std::string &path, const std::string &data,
                      int state) {
  // path_buffer 用于接收实际创建的路径（ZOO_SEQUENCE 时会带序号）
  char path_buffer[256] = {0};

  int rc = zoo_create(zhandle_, path.c_str(), data.c_str(),
                      static_cast<int>(data.size()), &ZOO_OPEN_ACL_UNSAFE,
                      state, path_buffer, sizeof(path_buffer));

  if (rc == ZOK) {
    RpcLogger::info("ZkClient::Create: register success in '{}'", path);
  } else if (rc == ZNODEEXISTS) {
    RpcLogger::warn("ZkClient::Create: node already exists '{}'", path);
  } else {
    RpcLogger::error("ZkClient::Create: register failed: {}", zerror(rc));
  }
}

std::string ZkClient::GetData(const std::string &path) {

  // 第一次用一个合理大小的 buffer
  std::vector<char> buf(512, 0);
  int bufferlen = static_cast<int>(buf.size());

  int flag = zoo_get(zhandle_, path.c_str(),
                     0, // 不注册 watcher
                     buf.data(), &bufferlen,
                     nullptr); // 不需要 Stat

  if (flag != ZOK) {
    RpcLogger::error("ZkClient::Create: zoo_get failed: {} ({})", path,
                     zerror(flag));
    return "";
  }

  // 数据比 buffer 大时，bufferlen 会被设成实际需要的长度
  // 需要重新分配再查一次
  if (bufferlen > static_cast<int>(buf.size())) {
    buf.resize(bufferlen + 1);
    int newlen = static_cast<int>(buf.size());
    flag = zoo_get(zhandle_, path.c_str(), 0, buf.data(), &newlen, nullptr);
    if (flag != ZOK) {
      RpcLogger::error("ZkClient::Create: zoo_get retry failed: {} ({})", path,
                       zerror(flag));
      return "";
    }
    bufferlen = newlen;
  }

  // 注意：ZK 返回的数据不一定以 '\0' 结尾，用长度构造
  return std::string(buf.data(), bufferlen);
}

std::vector<std::string> ZkClient::GetChildren(const std::string &path,
                                               watcher_fn fn, void *cbContext) {
  struct String_vector nodes;
  memset(&nodes, 0, sizeof(nodes));

  int flag = zoo_wget_children(zhandle_, path.c_str(), fn, cbContext, &nodes);

  std::vector<std::string> result;
  if (flag != ZOK) {
    RpcLogger::error("ZkClient::GetChildren: zoo_wget_children failed: {} ({})",
                     path, zerror(flag));
    return result;
  }

  for (int i = 0; i < nodes.count; ++i) {
    std::string child_path = path;
    if (child_path.empty() || child_path.back() != '/') {
      child_path += '/';
    }
    child_path += nodes.data[i];

    std::string data = GetData(child_path);
    if (!data.empty()) {
      result.push_back(std::move(data));
    }
  }

  deallocate_String_vector(&nodes);

  return result;
}
