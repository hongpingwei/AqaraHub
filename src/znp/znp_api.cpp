#include "znp/znp_api.h"
#include <stlab/concurrency/immediate_executor.hpp>
#include "logging.h"
#include "znp/encoding.h"

namespace znp {
ZnpApi::ZnpApi(std::shared_ptr<ZnpRawInterface> interface)
    : raw_(std::move(interface)),
      on_frame_connection_(raw_->on_frame_.connect(
          std::bind(&ZnpApi::OnFrame, this, std::placeholders::_1,
                    std::placeholders::_2, std::placeholders::_3))) {}

stlab::future<ResetInfo> ZnpApi::SysReset(bool soft_reset) {
  auto retval = WaitFor(ZnpCommandType::AREQ, SysCommand::RESET_IND)
                    .then(znp::Decode<ResetInfo>);
  raw_->SendFrame(ZnpCommandType::AREQ, SysCommand::RESET,
                  Encode<bool>(soft_reset));
  return retval;
}

stlab::future<Capability> ZnpApi::SysPing() {
  return RawSReq(SysCommand::PING, znp::Encode()).then(znp::Decode<Capability>);
}

void ZnpApi::OnFrame(ZnpCommandType type, ZnpCommand command,
                     const std::vector<uint8_t>& payload) {
  if (type == ZnpCommandType::AREQ && command == SysCommand::RESET_IND) {
    ResetInfo reset_info;
    bool ok = false;
    try {
      reset_info = Decode<ResetInfo>(payload);
      ok = true;
    } catch (const std::exception& exc) {
      LOG("ZnpApi", info) << "Unable to decode RESET_IND... " << exc.what();
    }
    if (ok) {
      on_reset_(reset_info);
    }
  }
  // Handle WaitFor queue
  std::queue<QueueCallback>& callback_queue(
      queue_[std::make_pair(type, command)]);
  if (!callback_queue.empty()) {
    QueueCallback callback = std::move(callback_queue.front());
    callback_queue.pop();
    callback(nullptr, payload);
  }
}

stlab::future<std::vector<uint8_t>> ZnpApi::WaitFor(ZnpCommandType type,
                                                    ZnpCommand command) {
  auto package = stlab::package<std::vector<uint8_t>(std::exception_ptr,
                                                     std::vector<uint8_t>)>(
      stlab::immediate_executor,
      [](std::exception_ptr exc, std::vector<uint8_t> retval) {
        if (exc != nullptr) {
          std::rethrow_exception(exc);
        }
        return retval;
      });
  queue_[std::make_pair(type, command)].push(std::move(package.first));
  return package.second;
}

stlab::future<std::vector<uint8_t>> ZnpApi::RawSReq(
    ZnpCommand command, const std::vector<uint8_t>& payload) {
  auto retval = WaitFor(ZnpCommandType::SRSP, command);
  raw_->SendFrame(ZnpCommandType::SREQ, command, payload);
  return retval;
}

std::vector<uint8_t> ZnpApi::CheckStatus(const std::vector<uint8_t>& response) {
  if (response.size() < 1) {
    throw std::runtime_error("Empty response received");
  }
  if (response[0] != (uint8_t)ZnpStatus::Success) {
    // TODO: Parse and throw proper error!
    throw std::runtime_error("ZNP Status was not success");
  }
  return std::vector<uint8_t>(response.begin() + 1, response.end());
}

void ZnpApi::CheckOnlyStatus(const std::vector<uint8_t>& response) {
  if (CheckStatus(response).size() != 0) {
    throw std::runtime_error("Empty response after status expected");
  }
}

}  // namespace znp
