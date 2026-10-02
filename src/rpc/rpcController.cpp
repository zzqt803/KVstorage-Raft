
#include "rpcController.h"

void RpcController::Reset() {
  failed_ = false;
  error_text_ = "";
}

bool RpcController::Failed() const { return failed_; }

std::string RpcController::ErrorText() const { return error_text_; }

void RpcController::SetFailed(const std::string &reason) {
  failed_ = true;
  error_text_ = reason;
}