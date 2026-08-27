/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Emulator. All rights reserved.                        *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/xam/cloud_storage.h"

#include "third_party/fmt/include/fmt/format.h"
#include "xenia/base/filesystem.h"
#include "xenia/base/logging.h"
#include "xenia/base/string.h"
#include "xenia/kernel/XLiveAPI.h"

// clang-format off
#include "xenia/base/platform.h"
#include "third_party/libcurl/include/curl/curl.h"
// clang-format on

#define RAPIDJSON_HAS_STDSTRING 1
#include "third_party/rapidjson/include/rapidjson/document.h"

namespace xe {
namespace kernel {
namespace xam {

namespace {
size_t WriteCallback(void* data, size_t size, size_t nmemb, void* clientp) {
  const size_t realsize = size * nmemb;
  auto* mem = static_cast<std::string*>(clientp);
  mem->append(static_cast<const char*>(data), realsize);
  return realsize;
}
}  // namespace

CloudStorage::CloudStorage() = default;
CloudStorage::~CloudStorage() = default;

std::string CloudStorage::BuildBlobUrl(uint64_t xuid, uint32_t title_id,
                                       XContentType content_type,
                                       const std::string& file_name) {
  std::string xbl_filename = fmt::format("{},ct{:08X},binary", file_name,
                                         static_cast<uint32_t>(content_type));
  return XLiveAPI::BuildEndpoint(
      fmt::format("storage/users/xuid({})/savedgames/titles/{}/files/{}", xuid,
                  title_id, xbl_filename));
}

std::vector<CloudStorage::BlobInfo> CloudStorage::List(
    uint64_t xuid, uint32_t title_id, XContentType content_type) {
  std::vector<BlobInfo> result;

  std::string endpoint = (title_id == 0xFFFE07D1 ? XLiveAPI::BuildEndpoint(fmt::format("storage/users/xuid({})/savedgames/files", xuid))
           : XLiveAPI::BuildEndpoint(fmt::format(
                 "storage/users/xuid({})/savedgames/titles/{}/files", xuid,
                 title_id)));

  XELOGI("CloudStorage: listing xuid={:016X} title={:08X}", xuid, title_id);

  CURL* curl = curl_easy_init();
  if (!curl) {
    XELOGE("CloudStorage: CURL init failed");
    return result;
  }

  std::string response_body;
  long http_code = 0;

  curl_easy_setopt(curl, CURLOPT_URL, endpoint.c_str());
  curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "GET");
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "xenia");
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);

  CURLcode res = curl_easy_perform(curl);
  if (res == CURLE_OK) {
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
  } else {
    XELOGE("CloudStorage: CURL error {}", static_cast<uint32_t>(res));
  }
  curl_easy_cleanup(curl);

  if (http_code != 200 || response_body.empty()) {
    XELOGI("CloudStorage: no content (HTTP {})", http_code);
    return result;
  }

  rapidjson::Document doc;
  doc.Parse(response_body.c_str(), response_body.size());
  if (doc.HasParseError() || !doc.IsObject()) {
    XELOGE("CloudStorage: JSON parse failed");
    return result;
  }

  if (!doc.HasMember("savedGames") || !doc["savedGames"].IsArray()) {
    XELOGI("CloudStorage: invalid response");
    return result;
  }

  for (const auto& game : doc["savedGames"].GetArray()) {
    if (!game.HasMember("fileName") || !game["fileName"].IsString()) {
      continue;
    }

    std::string raw_name = game["fileName"].GetString();

    // Parse format: {file_name},ct{content_type:08X},binary
    // Or old format: {file_name},binary
    std::string file_name;
    uint32_t blob_ct = 0xFFFFFFFF;

    size_t ct_pos = raw_name.find(",ct");
    if (ct_pos != std::string::npos) {
      file_name = raw_name.substr(0, ct_pos);
      std::string ct_str = raw_name.substr(ct_pos + 3, 8);
      blob_ct = static_cast<uint32_t>(std::stoul(ct_str, nullptr, 16));
    } else {
      size_t comma = raw_name.find_last_of(',');
      file_name =
          (comma != std::string::npos) ? raw_name.substr(0, comma) : raw_name;
      blob_ct = static_cast<uint32_t>(XContentType::kSavedGame);
    }

    if (title_id != 0xFFFE07D1 && blob_ct != static_cast<uint32_t>(content_type)) {
      continue;
    }

    std::u16string display_name = xe::to_utf16(file_name);
    uint32_t blob_title_id = title_id;
    uint64_t blob_size = 0;

    if (game.HasMember("titleId") && game.HasMember("size") &&
        game.HasMember("displayName")) {
      blob_title_id = game["titleId"].GetUint();
      blob_size = game["size"].GetUint();
      std::string dn = game["displayName"].GetString();
      if (!dn.empty()) {
        display_name = xe::to_utf16(dn);
      }
    }

    result.push_back({file_name, display_name, blob_title_id, blob_ct, blob_size});
  }

  XELOGI("CloudStorage: found {} items", result.size());
  return result;
}

bool CloudStorage::Exists(uint64_t xuid, uint32_t title_id,
                          XContentType content_type,
                          const std::string& file_name) {
  std::string endpoint = BuildBlobUrl(xuid, title_id, content_type, file_name);

  CURL* curl = curl_easy_init();
  if (!curl) {
    return false;
  }

  curl_easy_setopt(curl, CURLOPT_URL, endpoint.c_str());
  curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "xenia");

  CURLcode res = curl_easy_perform(curl);
  long http_code = 0;
  curl_off_t content_length = -1;
  if (res == CURLE_OK) {
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_getinfo(curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T,
                      &content_length);
  }
  curl_easy_cleanup(curl);

  // Server returns 200 with Content-Length: 0 for non-existent files.
  // Only treat as existing if there's actual content.
  return http_code == 200 && content_length > 0;
}

bool CloudStorage::Download(uint64_t xuid, uint32_t title_id,
                            XContentType content_type,
                            const std::string& file_name,
                            const std::filesystem::path& package_path) {
  std::string endpoint = BuildBlobUrl(xuid, title_id, content_type, file_name);

  XELOGI("CloudStorage: downloading '{}'", file_name);

  CURL* curl = curl_easy_init();
  if (!curl) {
    XELOGE("CloudStorage: CURL init failed");
    return false;
  }

  std::string response_body;
  long http_code = 0;

  curl_easy_setopt(curl, CURLOPT_URL, endpoint.c_str());
  curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "GET");
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "xenia");
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);

  CURLcode res = curl_easy_perform(curl);
  if (res == CURLE_OK) {
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
  }
  curl_easy_cleanup(curl);

  if (http_code != 200) {
    XELOGE("CloudStorage: download failed (HTTP {})", http_code);
    return false;
  }

  // Server returns 200 with empty body for non-existent files — treat as
  // "no cloud data yet", create empty directory so the game can start fresh.
  if (response_body.empty()) {
    XELOGI("CloudStorage: no cloud data, creating empty package");
    std::filesystem::create_directories(package_path);
    return true;
  }

  XELOGI("CloudStorage: downloaded {} bytes", response_body.size());

  std::vector<uint8_t> blob(response_body.begin(), response_body.end());
  return Unpack(blob, package_path);
}

bool CloudStorage::Upload(uint64_t xuid, uint32_t title_id,
                          XContentType content_type,
                          const std::string& file_name,
                          const std::u16string& display_name,
                          const std::filesystem::path& package_path) {
  if (!std::filesystem::exists(package_path)) {
    XELOGW("CloudStorage: package path does not exist");
    return false;
  }

  std::vector<uint8_t> packed = Pack(package_path, display_name);
  if (packed.empty()) {
    XELOGW("CloudStorage: no files in package directory");
    return false;
  }

  std::string endpoint = BuildBlobUrl(xuid, title_id, content_type, file_name);

  XELOGI("CloudStorage: uploading '{}' ({} bytes)", file_name, packed.size());

  CURL* curl = curl_easy_init();
  if (!curl) {
    XELOGE("CloudStorage: CURL init failed");
    return false;
  }

  long http_code = 0;
  struct curl_slist* headers = nullptr;
  headers =
      curl_slist_append(headers, "Content-Type: application/octet-stream");

  curl_easy_setopt(curl, CURLOPT_URL, endpoint.c_str());
  curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "xenia");
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, packed.data());
  curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE,
                   static_cast<curl_off_t>(packed.size()));

  CURLcode res = curl_easy_perform(curl);
  bool success = false;
  if (res == CURLE_OK) {
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    XELOGI("CloudStorage: upload '{}' -> HTTP {}", file_name, http_code);
    success = http_code == 200 || http_code == 201;
  } else {
    XELOGE("CloudStorage: upload failed: CURL error {}",
           static_cast<uint32_t>(res));
  }

  curl_slist_free_all(headers);
  curl_easy_cleanup(curl);
  return success;
}

bool CloudStorage::Delete(uint64_t xuid, uint32_t title_id,
                          XContentType content_type,
                          const std::string& file_name) {
  std::string endpoint = BuildBlobUrl(xuid, title_id, content_type, file_name);

  CURL* curl = curl_easy_init();
  if (!curl) {
    return false;
  }

  curl_easy_setopt(curl, CURLOPT_URL, endpoint.c_str());
  curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "xenia");

  CURLcode res = curl_easy_perform(curl);
  long http_code = 0;
  if (res == CURLE_OK) {
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    XELOGI("CloudStorage: delete '{}' -> HTTP {}", file_name, http_code);
  }
  curl_easy_cleanup(curl);
  return http_code == 200;
}

std::vector<uint8_t> CloudStorage::Pack(
    const std::filesystem::path& package_path,
    const std::u16string& display_name) {
  // Pack format:
  //   [display_name_len:4][display_name:N]  (UTF-8)
  //   [file_count:4]
  //   For each file: [name_len:4][name:N][data_len:8][data:M]
  std::vector<uint8_t> blob;

  std::string name = xe::to_utf8(display_name);
  uint32_t name_len = static_cast<uint32_t>(name.size());
  blob.insert(blob.end(), reinterpret_cast<const uint8_t*>(&name_len),
              reinterpret_cast<const uint8_t*>(&name_len) + 4);
  blob.insert(blob.end(), name.begin(), name.end());

  size_t file_count_offset = blob.size();
  uint32_t file_count = 0;
  blob.resize(blob.size() + 4);

  for (const auto& entry : std::filesystem::directory_iterator(package_path)) {
    if (!entry.is_regular_file()) {
      continue;
    }
    file_count++;

    std::string file_name = xe::path_to_utf8(entry.path().filename());
    std::vector<uint8_t> file_data;
    auto file = xe::filesystem::OpenFile(entry.path(), "rb");
    if (!file) {
      continue;
    }

    fseek(file, 0, SEEK_END);
    size_t file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    file_data.resize(file_size);
    fread(file_data.data(), 1, file_size, file);
    fclose(file);

    uint32_t fn_len = static_cast<uint32_t>(file_name.size());
    uint64_t data_len = file_data.size();
    blob.insert(blob.end(), reinterpret_cast<const uint8_t*>(&fn_len),
                reinterpret_cast<const uint8_t*>(&fn_len) + 4);
    blob.insert(blob.end(), file_name.begin(), file_name.end());
    blob.insert(blob.end(), reinterpret_cast<const uint8_t*>(&data_len),
                reinterpret_cast<const uint8_t*>(&data_len) + 8);
    blob.insert(blob.end(), file_data.begin(), file_data.end());
  }

  std::memcpy(blob.data() + file_count_offset, &file_count, 4);

  XELOGI("CloudStorage: packed {} files ({} bytes)", file_count, blob.size());
  return blob;
}

bool CloudStorage::Unpack(const std::vector<uint8_t>& blob,
                          const std::filesystem::path& package_path) {
  const uint8_t* ptr = blob.data();
  const uint8_t* end = ptr + blob.size();

  // Read display name (ignored here — caller handles it)
  if (ptr + 4 > end) {
    return false;
  }
  uint32_t name_len;
  std::memcpy(&name_len, ptr, 4);
  ptr += 4;
  if (ptr + name_len > end) {
    return false;
  }
  ptr += name_len;

  // Read file count
  if (ptr + 4 > end) {
    return false;
  }
  uint32_t file_count;
  std::memcpy(&file_count, ptr, 4);
  ptr += 4;

  std::filesystem::create_directories(package_path);

  for (uint32_t i = 0; i < file_count && ptr + 4 <= end; i++) {
    uint32_t fn_len;
    std::memcpy(&fn_len, ptr, 4);
    ptr += 4;
    if (ptr + fn_len > end) {
      break;
    }
    std::string file_name(ptr, ptr + fn_len);
    ptr += fn_len;
    if (ptr + 8 > end) {
      break;
    }
    uint64_t data_len;
    std::memcpy(&data_len, ptr, 8);
    ptr += 8;
    if (ptr + data_len > end) {
      break;
    }

    auto out_path = package_path / xe::to_path(file_name);
    auto file = xe::filesystem::OpenFile(out_path, "wb");
    if (file) {
      fwrite(ptr, 1, data_len, file);
      fclose(file);
    }
    ptr += data_len;
  }

  XELOGI("CloudStorage: unpacked {} files", file_count);
  return true;
}

}  // namespace xam
}  // namespace kernel
}  // namespace xe
