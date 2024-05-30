/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2024 Xenia Emulator. All rights reserved.                        *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_BASE_OBJECT_JSON_H_
#define XENIA_KERNEL_BASE_OBJECT_JSON_H_

#define RAPIDJSON_HAS_STDSTRING 1

#include <third_party/rapidjson/include/rapidjson/document.h>
#include <third_party/rapidjson/include/rapidjson/prettywriter.h>
#include <third_party/rapidjson/include/rapidjson/stringbuffer.h>

namespace xe {
namespace kernel {
class BaseObjectJSON {
 public:
  bool Deserialize(const std::string& s);
  bool Serialize(std::string& s);

  virtual std::pair<bool, std::string> Serialize() const;
  virtual bool Deserialize(const rapidjson::Value& obj) = 0;
  virtual bool Serialize(
      rapidjson::PrettyWriter<rapidjson::StringBuffer>* writer) const = 0;

 protected:
  bool InitDocument(const std::string& s, rapidjson::Document& doc);
};
}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_BASE_OBJECT_JSON_H_