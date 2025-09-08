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

#include <string>
#include "xenia/kernel/xobject.h"
#include "xenia/kernel/xsocket.h"

namespace xe {
namespace kernel {

class KernelState;

class HINTERNET : public XObject {
 public:
  static const XObject::Type kObjectType = XObject::Type::Internet;

  HINTERNET(KernelState* kernel_state);

  XSocket* getSocket() const { return socket_; };

  uint32_t getLastError() const { return last_error_; };

  std::string user_agent() const { return user_agent_; };

  std::string server_name() const { return server_name_; };

  uint16_t port() const { return port_; };

  std::string path_ = "/";
  std::string server_name_ = "";
  std::string user_agent_ = "Xenia";
  std::string method_ = "GET";
  std::string version_ = "HTTP/1.0";
  uint16_t port_ = 80;
  std::string hostname_ = "";
  uint32_t last_error_ = 0;
  XSocket* socket_;

  void SendRequest(std::string Headers, std::string buffer);

  bool Connect();

  void setLastError(uint32_t error) { last_error_ = error; };

  HINTERNET* SetUserAgent(std::string user_agent);

  HINTERNET* SetHost(std::string server_name, uint16_t port);

  HINTERNET* SetRequest(std::string path, std::string method,
                        std::string version);

  std::string getHost();
  std::string getPath();
  std::string getMethod();
  std::string getUserAgent();
  std::string getVersion();
};

}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_HINTERNET_H_
