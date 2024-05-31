/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_HINTERNET_H_
#define XENIA_KERNEL_HINTERNET_H_

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <string>
#include "xenia/kernel/xobject.h"

using namespace std::chrono_literals;

namespace xe {
namespace kernel {

class KernelState;

  class HINTERNET : public XObject {

 public:
  static const XObject::Type kObjectType = XObject::Type::Internet;
  enum class Type { Session, Connection, Request };

  HINTERNET(KernelState* kernel_state);

  Type type() { return type_; };
  
  std::string user_agent() { return user_agent_; };

  std::string server_name() { return server_name_; };

  uint16_t port() { return port_; };

  std::string path() { return path_; };

  std::string url() { return url_; };

  std::string method() { return method_; };

  Type type_;

  std::string path_ = "";
  std::string server_name_ = "";
  std::string user_agent_ = "";
  std::string method_ = "";
  uint32_t port_ = 4135;
  std::string url_ = "";

  uint32_t HINTERNET::SendRequest(uint32_t handle, std::string Headers,
                                  uint8_t* buffer, uint32_t buf_len, std::string method);

  uint32_t HINTERNET::Connect(uint32_t handle);
  //std::string ReceiveResponse();

  X_HANDLE CreateSessionHandle(uint32_t hInternet,
      std::string user_agent);

  X_HANDLE CreateConnectionHandle(uint32_t hsession_handle,
                                          std::string server_name, uint32_t port);

  X_HANDLE CreateRequestHandle(uint32_t hconnection_handle,
                                       std::string path, 
                                       std::string method);

};

}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_HINTERNET_H_
