/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "src/xenia/kernel/hinternet.h"

#include <cstring>

#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/xboxkrnl/xboxkrnl_threading.h"
#include <third_party/libcurl/include/curl/curl.h>

namespace xe {
namespace kernel {

HINTERNET::HINTERNET(KernelState* kernel_state)
    : XObject(kernel_state, kObjectType) {}

X_HANDLE HINTERNET::CreateSessionHandle(uint32_t handle,
                                         std::string user_agent) {
  if (handle <= 1) {
    return 1;
  }
  auto hInternet =
      kernel_state()->object_table()->LookupObject<HINTERNET>(handle);
  hInternet->type_ = HINTERNET::Type::Session; 
  hInternet->user_agent_ = user_agent;
  return hInternet->handle();
}

X_HANDLE HINTERNET::CreateConnectionHandle(uint32_t handle,
                                           std::string server_name,
                                           uint32_t port) {
  if (handle <= 1) {
    return 1;
  }
  if (port == 0) {
    port = 4135;
  }
  std::string rawUrl;
  rawUrl.append(server_name);
  rawUrl.append(":");
  rawUrl.append(std::to_string(port));

  auto hConnection =
      kernel_state()->object_table()->LookupObject<HINTERNET>(handle);

  if (hConnection->type() != HINTERNET::Type::Session) {
    XELOGI("HINTERNET handle mismatch!");
    return 1;
  }

  hConnection->type_ = HINTERNET::Type::Connection;
  hConnection->server_name_ = server_name;
  hConnection->port_ = port;
  hConnection->url_ = rawUrl;

  //int isConencted = hConnection->Connect(hConnection->handle());
  //if (!isConencted) {
  //  return NULL;
  //}

  return hConnection->handle();
}
X_HANDLE HINTERNET::CreateRequestHandle(uint32_t handle,
                                        std::string path, std::string method) {
  if (handle <= 1) {
    return NULL;
  }

  auto hRequest =
      kernel_state()->object_table()->LookupObject<HINTERNET>(handle);

  if (hRequest->type() != HINTERNET::Type::Connection) {
    XELOGI("HINTERNET: handle mismatch!");
    return NULL;
  }

  hRequest->type_ = HINTERNET::Type::Request;
  hRequest->path_ = path;
  hRequest->method_ = method;
  return hRequest->handle();
}
uint32_t HINTERNET::SendRequest(uint32_t handle, std::string Headers,
                                uint8_t* buffer, uint32_t buf_len,
                                std::string method) {
  if (handle <= 1) {
    return FALSE;
  }

  auto hRequest =
      kernel_state()->object_table()->LookupObject<HINTERNET>(handle);

  if (!hRequest) {
    return FALSE;
  }

  if (hRequest->type() != HINTERNET::Type::Request) {
    XELOGI("HINTERNET: handle mismatch!");
    return FALSE;
  }
  CURL* curl = curl_easy_init();
  if (curl) {
    curl_easy_setopt(curl, CURLOPT_URL, hRequest->url().c_str());
    curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, hRequest->user_agent().c_str());
    curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);

    if (method == "GET") {
      curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    } else if (method == "POST") {
      curl_easy_setopt(curl, CURLOPT_POSTFIELDS, (lpvoid_t)buffer);
      curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, buf_len);
    }

    // If connecting to a socket and revived an invalid response.
    // Should be handled correctly in a future.
    curl_easy_setopt(curl, CURLOPT_HTTP09_ALLOWED, true);

    CURLcode result = curl_easy_perform(curl);

    if (result != CURLE_OK) {
      return FALSE;
    } else {
      return TRUE;
    }
    curl_easy_cleanup(curl);
  }

  return FALSE;
}
uint32_t HINTERNET::Connect(uint32_t handle) {
  if (handle <= 1) {
    return FALSE;
  }

  auto hRequest =
      kernel_state()->object_table()->LookupObject<HINTERNET>(handle);

  if (!hRequest) {
    return FALSE;
  }

  if (hRequest->type() != HINTERNET::Type::Connection) {
    XELOGI("HINTERNET: handle mismatch!");
    return FALSE;
  }
  CURL* curl = curl_easy_init();
  if (curl) {
    curl_easy_setopt(curl, CURLOPT_URL, hRequest->url().c_str());
    curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, hRequest->user_agent().c_str());
    curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);

    // If connecting to a socket and revived an invalid response.
    // Should be handled correctly in a future.
    curl_easy_setopt(curl, CURLOPT_HTTP09_ALLOWED, true);

    CURLcode result = curl_easy_perform(curl);

    if (result != CURLE_OK) {
      return FALSE;
    } else {
      return TRUE;
    }
    curl_easy_cleanup(curl);
  }

  return FALSE;
}

}  // namespace kernel
}  // namespace xe