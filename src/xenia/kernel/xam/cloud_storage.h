/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Emulator. All rights reserved.                        *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XAM_CLOUD_STORAGE_H_
#define XENIA_KERNEL_XAM_CLOUD_STORAGE_H_

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "xenia/kernel/xam/content_manager.h"

namespace xe {
namespace kernel {
namespace xam {

// Handles HTTP operations against the titlestorage API for cloud saves.
// ContentManager delegates cloud-specific work here.
class CloudStorage {
 public:
  struct BlobInfo {
    std::string file_name;
    std::u16string display_name;
    uint32_t title_id = 0;
    uint32_t content_type = 0;
    uint64_t size = 0;
  };

  CloudStorage();
  ~CloudStorage();

  // List all blobs in the titlestorage container, filtered by content_type.
  std::vector<BlobInfo> List(uint64_t xuid, uint32_t title_id,
                             XContentType content_type);

  // Check if a blob exists on the server.
  bool Exists(uint64_t xuid, uint32_t title_id, XContentType content_type,
              const std::string& file_name);

  // Download a blob and unpack it into package_path.
  // Returns false on failure.
  bool Download(uint64_t xuid, uint32_t title_id, XContentType content_type,
                const std::string& file_name,
                const std::filesystem::path& package_path);

  // Pack all files in package_path into a single blob and upload it.
  // display_name is embedded in the blob header.
  bool Upload(uint64_t xuid, uint32_t title_id, XContentType content_type,
              const std::string& file_name, const std::u16string& display_name,
              const std::filesystem::path& package_path);

  // Delete a blob from the server.
  bool Delete(uint64_t xuid, uint32_t title_id, XContentType content_type,
              const std::string& file_name);

 private:
  // Build the titlestorage URL for a blob.
  static std::string BuildBlobUrl(uint64_t xuid, uint32_t title_id,
                                  XContentType content_type,
                                  const std::string& file_name);

  // Pack a directory into a single blob.
  static std::vector<uint8_t> Pack(const std::filesystem::path& package_path,
                                   const std::u16string& display_name);

  // Unpack a blob into a directory.
  static bool Unpack(const std::vector<uint8_t>& blob,
                     const std::filesystem::path& package_path);
};

}  // namespace xam
}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XAM_CLOUD_STORAGE_H_
