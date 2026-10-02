#pragma once

#include <google/protobuf/service.h>
#include <string>
class RpcController : public google::protobuf::RpcController {
public:
  RpcController() = default;
  void Reset();
  bool Failed() const;
  std::string ErrorText() const;
  void SetFailed(const std::string &reason);

private:
  bool failed_{false};
  std::string error_text_{};
};