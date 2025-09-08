/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2024 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "src/xenia/kernel/hinternet.h"

#include <cstring>

#include "xenia/base/string_util.h"
#include "xenia/kernel/XLiveAPI.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/xboxkrnl/xboxkrnl_threading.h"
#include "xenia/kernel/xsocket.h"
#include "xenia/kernel/xthread.h"

#include <winsock2.h>

namespace xe {
namespace kernel {

HINTERNET::HINTERNET(KernelState* kernel_state)
    : XObject(kernel_state, kObjectType) {
  socket_ = new XSocket(kernel_state);
  socket_->Initialize(XSocket::AddressFamily::X_AF_INET,
                      XSocket::Type::X_SOCK_STREAM,
                      XSocket::Protocol::X_IPPROTO_TCP);
}

HINTERNET* HINTERNET::SetUserAgent(std::string user_agent) {
  user_agent_ = user_agent;
  return this;
}

HINTERNET* HINTERNET::SetHost(std::string server_name, uint16_t port) {
  if (port == 0) {
    port = 80;
  }
  if (server_name.empty()) {
    server_name = "127.0.0.1";
  }
  size_t pos = 0;
  int count = 0;

  while (count < 3 && pos != std::string::npos) {
    pos = server_name.find('/', pos + 1);
    count++;
  }
  std::string hostname = fmt::format(
      "{}:{}",
      (pos != std::string::npos) ? server_name.substr(0, pos) : server_name,
      port);

  this->server_name_ = server_name;
  this->port_ = port;
  this->hostname_ = hostname;

  return this;
}
std::string HINTERNET::getHost() { return this->hostname_; }
std::string HINTERNET::getPath() { return this->path_; }
std::string HINTERNET::getMethod() { return this->method_; }
std::string HINTERNET::getUserAgent() { return this->user_agent_; }
std::string HINTERNET::getVersion() { return this->version_; }
HINTERNET* HINTERNET::SetRequest(std::string path, std::string method,
                                 std::string version) {
  this->path_ = path;
  this->method_ = method;
  this->version_ = version;
  return this;
}
void HINTERNET::SendRequest(std::string Header, std::string buffer) {
  // std::string request_data = Header + "\r\n" + buffer;

  std::vector<uint8_t> send_buffer(buffer.begin(), buffer.end());

  auto socket = this->getSocket();
  if (socket == nullptr) {
    this->setLastError(XHTTP_ERROR_NOT_INITIALIZED);
    return;
  }

  int sent = socket->Send(send_buffer.data(),
                          static_cast<uint32_t>(send_buffer.size()), 0);
  if (sent < 0) {
    this->setLastError(socket->GetLastWSAError());  // Could not send
    XELOGI("Failed to send buffer");
    return;
  }

  std::vector<uint8_t> recv_buffer(4096);
  int bytes_received = 0;
  std::string response;  // optional: store response if you want to log it

  while ((bytes_received = socket->Recv(
              recv_buffer.data(), static_cast<int>(recv_buffer.size()), 0)) >
         0) {
    response.append(reinterpret_cast<char*>(recv_buffer.data()),
                    bytes_received);
  }

  if (bytes_received < 0) {
    XELOGI("Error receiving response: WSAError {:08X}",
           socket->GetLastWSAError());
  } else {
    XELOGI("Received response ({} bytes):\n{}", response.size(), response);
  }

  this->setLastError(0);
  socket->Close();
  return;
}
bool HINTERNET::Connect() {
  auto socket = this->getSocket();
  if (!socket) {
    XELOGI("HInternet: Socket was null");
    return false;
  }

  std::string ip =
      this->server_name();  // This needs to be pulled from the DNS.
  if (ip.empty()) {
    XELOGI("HInternet: ip was empty");
    return false;
  }

  XSOCKADDR_IN addr = {};
  addr.address_family = XSocket::X_AF_INET;
  addr.address_port = this->port_;
  addr.address_ip.s_addr = inet_addr(ip.c_str());

  X_STATUS status = socket->Connect(&addr, sizeof(addr));
  XELOGI("Socket Status: {:08X}", status);
  XELOGI("WSA Status: {:08X}", socket->GetLastWSAError());
  XELOGI("family: {}", addr.address_family.get());
  XELOGI("port: {}", addr.address_port.get());
  XELOGI("ip: {:08X}", addr.address_ip.s_addr);
  XELOGI("Size: {}", sizeof(addr));
  XELOGI("Connecting to IP: {}, port: {}", ip, this->port_);
  XELOGI("inet_addr result: {:08X}", addr.address_ip.s_addr);

  if (status == X_STATUS_UNSUCCESSFUL) {
    uint32_t wsaError = socket->GetLastWSAError();
    if (wsaError == static_cast<uint32_t>(X_WSAError::X_WSAEALREADY)) {
      XELOGI("Already sending request!");
      return true;
    }
  }
  return status == X_STATUS_SUCCESS;
}

}  // namespace kernel
}  // namespace xe
