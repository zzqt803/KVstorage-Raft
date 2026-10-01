#pragma once
#include "google/protobuf/service.h"
#include "zookeeperUtil.h"
#include <memory>
#include <string>
#include <trantor/net/EventLoop.h>
#include <trantor/net/EventLoopThreadPool.h>
#include <trantor/net/TcpConnection.h>
#include <trantor/net/TcpServer.h>
#include <trantor/utils/ConcurrentTaskQueue.h>
#include <trantor/utils/MsgBuffer.h>
#include <unordered_map>
class RpcProvider {
public:
  void NotifyService(google::protobuf::Service *service);
  ~RpcProvider();
  void Run();

private:
  /// 按照构造顺序声明
  trantor::EventLoop event_loop_;
  std::unique_ptr<trantor::ConcurrentTaskQueue> work_pool_;
  std::unique_ptr<trantor::TcpServer> server_;
  std::unique_ptr<ZkClient> zkclient_;
  
  struct ServiceInfo {
    google::protobuf::Service *service;
    std::unordered_map<std::string, const google::protobuf::MethodDescriptor *>
        method_map;
  };

  std::unordered_map<std::string, ServiceInfo> service_map_;

  void OnConnection(const trantor::TcpConnectionPtr &conn);
  void OnMessage(const trantor::TcpConnectionPtr &conn,
                 trantor::MsgBuffer *buf);
  void SendRpcResponse(const trantor::TcpConnectionPtr &conn,
                       google::protobuf::Message *response,
                       google::protobuf::Message *request);
};