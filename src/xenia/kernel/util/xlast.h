/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2024 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_UTIL_XLAST_H_
#define XENIA_KERNEL_UTIL_XLAST_H_

#include <map>
#include <string>
#include <vector>

#include "third_party/pugixml/src/pugixml.hpp"
#include "xenia/xbox.h"

namespace xe {
namespace kernel {
namespace util {

static const std::map<XLanguage, std::string> language_mapping = {
    {XLanguage::kEnglish, "en-US"},    {XLanguage::kJapanese, "ja-JP"},
    {XLanguage::kGerman, "de-DE"},     {XLanguage::kFrench, "fr-FR"},
    {XLanguage::kSpanish, "es-ES"},    {XLanguage::kItalian, "it-IT"},
    {XLanguage::kKorean, "ko-KR"},     {XLanguage::kTChinese, "zh-CHT"},
    {XLanguage::kPortuguese, "pt-PT"}, {XLanguage::kPolish, "pl-PL"},
    {XLanguage::kRussian, "ru-RU"}};

class XLastMatchmakingQuery {
 public:
  XLastMatchmakingQuery();
  XLastMatchmakingQuery(const pugi::xpath_node query_node);

  std::string GetName() const;
  std::vector<uint32_t> GetReturns() const;
  std::vector<uint32_t> GetParameters() const;
  std::vector<uint32_t> GetFilters() const;

 private:
  pugi::xpath_node node_;
};

class XLast {
 public:
  XLast();
  XLast(const uint8_t* compressed_xml_data, const uint32_t compressed_data_size,
        const uint32_t decompressed_data_size);
  ~XLast();

  std::u16string GetTitleName();
  std::u16string GetLocalizedString(uint32_t string_id, XLanguage language);
  XLastMatchmakingQuery* GetMatchmakingQuery(uint32_t query_id);
  static std::vector<uint32_t> GetAllValuesFromNode(
      const pugi::xpath_node node, const std::string child_name,
      const std::string attirbute_name);

  void Dump(std::string file_name);

 private:
  std::string GetLocaleStringFromLanguage(XLanguage language);

  std::vector<uint8_t> xlast_decompressed_xml_;
  std::unique_ptr<pugi::xml_document> parsed_xlast_ = nullptr;
  pugi::xml_parse_result parse_result_ = {};
};

}  // namespace util
}  // namespace kernel
}  // namespace xe

#endif