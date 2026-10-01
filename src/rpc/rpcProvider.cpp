#include "rpcProvider.h"
#include "rpcApplication.h"
#include "rpcHeader.pb.h"
#include "rpcLogger.h"
#include "zookeeperUtil.h"
#include <google/protobuf/descriptor.h>
#include <google/protobuf/message.h>
#include <google/protobuf/service.h>
#include <google/protobuf/stubs/callback.h>
#include <memory>
#include <trantor/net/EventLoop.h>
#include <trantor/net/EventLoopThreadPool.h>
#include <trantor/net/InetAddress.h>
#include <trantor/net/TcpConnection.h>
#include <trantor/net/TcpServer.h>
#include <trantor/net/callbacks.h>
#include <trantor/utils/ConcurrentTaskQueue.h>
#include <trantor/utils/MsgBuffer.h>
#include <zookeeper/zookeeper.h>

class LambdaClosure : public google::protobuf::Closure {
public:
  explicit LambdaClosure(std::function<void()> cb) : cb_(std::move(cb)) {}
  void Run() override {
    cb_();
    delete this;
  }

private:
  std::function<void()> cb_;
};

/**
 * @brief 将service的信息保存到service_map中
 *
 * @param service
 */
void RpcProvider::NotifyService(google::protobuf::Service *service) {
  ServiceInfo service_info;
  const google::protobuf::ServiceDescriptor *psd = service->GetDescriptor();

  RpcLogger::info("RpcProvider::NotifyService: service_name={}", psd->name());
  // 填充服务和方法
  service_info.service = service;
  for (int i = 0; i < psd->method_count(); i++) {
    const google::protobuf::MethodDescriptor *pmd = psd->method(i);
    service_info.method_map.emplace(pmd->name(), pmd);
    RpcLogger::info("RpcProvider::NotifyService: method_name={}", pmd->name());
  }
  service_map_.emplace(psd->name(), service_info);
}

void RpcProvider::Run() {
  auto &config = RpcApplication::GetInstance().GetConfig();
  std::string server_name = config.server.name;
  std::string ip = config.server.host;
  uint16_t port = config.server.port;
  int thread_num = config.server.threads;

  // 初始化工作线程池
  work_pool_ =
      std::make_unique<trantor::ConcurrentTaskQueue>(thread_num, "RpcWorker");

  // 初始化TcpServer服务器
  trantor::InetAddress address(ip, port);
  auto server =
      std::make_shared<trantor::TcpServer>(&event_loop_, address, server_name);
  server_->setIoLoopNum(thread_num);
  server->setConnectionCallback(
      [this](const trantor::TcpConnectionPtr &conn) { OnConnection(conn); });
  server->setRecvMessageCallback(
      [this](const trantor::TcpConnectionPtr &conn, trantor::MsgBuffer *buf) {
        OnMessage(conn, buf);
      });

  // 初始化Zookeeper客户端
  zkclient_->Start();
  // 创建节点
  for (auto &[service_name, service_info] : service_map_) {
    std::string service_path = "/" + service_name;
    // 0代表持久节点，该节点为多个进程的公共节点
    zkclient_->Create(service_path, "", 0);

    std::string ip_port = ip + ":" + std::to_string(port);
    std::string instance_path = service_path + "/" + ip_port;
    // 临时节点，本进程自己的节点
    zkclient_->Create(instance_path, ip_port, ZOO_EPHEMERAL);
  }

  server_->start();
  RpcLogger::info("RpcProvider listening on {}:{}", config.server.host,
                  config.server.port);
  event_loop_.loop();
}

void RpcProvider::OnConnection(const trantor::TcpConnectionPtr &conn) {
  if (!conn->connected()) {
    conn->shutdown();
  }
}

/**
 * @brief RPC信息结构 | header_len | RPCHeader | Arg_string |
 *                      4字节
 * @param conn
 * @param buf
 */
void RpcProvider::OnMessage(const trantor::TcpConnectionPtr &conn,
                            trantor::MsgBuffer *buf) {
  // 一次回调可能包含多帧，循环处理
  while (true) {
    // 判断header_len是否到齐
    if (buf->readableBytes() < 4) {
      return;
    }
    uint32_t header_len = 0;
    std::memcpy(&header_len, buf->peek(), 4);
    header_len = ntohl(header_len);

    // 判断RPCHeader是否到齐
    if (buf->readableBytes() < 4 + header_len) {
      return;
    }
    // 先尝试能否解析成功，不消费
    RPC::RpcHeader header;
    if (!header.ParseFromArray(static_cast<const void *>(buf->peek() + 4),
                               static_cast<int>(header_len))) {
      RpcLogger::error("RpcProvider::OnMessage: RpcHeader parse failed from {}",
                       conn->peerAddr().toIpPort());
      conn->shutdown(); // 协议错位直接断开连接
      return;
    }
    const std::string &service_name = header.service_name();
    const std::string &method_name = header.method_name();
    uint32_t args_size = header.args_size();

    // 判断args是否到齐
    if (buf->readableBytes() < 4 + header_len + args_size) {
      return;
    }
    std::string args_str(buf->peek(), args_size);

    // 这里整帧到齐，统一消费
    buf->retrieve(4);
    buf->retrieve(header_len);
    buf->retrieve(args_size);

    // 查找service和method
    auto sit = service_map_.find(service_name);
    if (sit == service_map_.end()) {
      RpcLogger::error("RpcProvider::OnMessage: service not found: {}",
                       service_name);
      continue; // 不return，这里继续处理下一帧
    }
    auto mit = sit->second.method_map.find(method_name);
    if (mit == sit->second.method_map.end()) {
      RpcLogger::error("RpcProvider::OnMessage: method_name not found: {}",
                       method_name);
      continue;
    }

    google::protobuf::Service *service = sit->second.service;
    const google::protobuf::MethodDescriptor *method = mit->second;

    // 构造request和response
    google::protobuf::Message *request =
        service->GetRequestPrototype(method).New();
    if (!request->ParseFromString(args_str)) {
      RpcLogger::error("RpcProvider::OnMessage: args parse failed: {].{}",
                       service_name, method_name);
      delete request;
      continue;
    }

    google::protobuf::Message *response =
        service->GetResponsePrototype(method).New();

    // 业务方法执行完成后用done发回响应
    // 用weak_ptr防止延长生命周期
    std::weak_ptr<trantor::TcpConnection> weak_conn = conn;
    google::protobuf::Closure *done =
        new LambdaClosure([this, weak_conn, response, request]() {
          auto c = weak_conn.lock();
          if (c) {
            c->getLoop()->runInLoop([this, c, response, request]() {
              SendRpcResponse(c, response, request);
            });
          } else {
            delete response;
            delete request;
          }
        });

    // 业务方法丢到线程池
    work_pool_->runTaskInQueue([service, method, request, response, done]() {
      service->CallMethod(method, nullptr, request, response, done);
    });
  }
}
void RpcProvider::SendRpcResponse(const trantor::TcpConnectionPtr &conn,
                                  google::protobuf::Message *response,
                                  google::protobuf::Message *request) {
  std::string response_str;
  if (response->SerializePartialToString(&response_str)) {
    uint32_t len = response_str.size();
    uint32_t net_len = htonl(len);

    std::string send_buf;
    send_buf.resize(4 + len);
    std::memcpy(&send_buf[0], &net_len, 4);
    std::memcpy(&send_buf[4], response_str.data(), len);

    conn->send(send_buf);
  } else {
    RpcLogger::error("RpcProvider::SendRpcResponse: response serialize failed");
  }
  delete response;
  delete request;
}