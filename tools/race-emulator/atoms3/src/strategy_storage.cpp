#include "strategy_storage.h"

#include <LittleFS.h>
#include <mbedtls/sha256.h>
#include <cstring>
#include <utility>

namespace sim {
namespace {
int hexDigit(char ch) {
  if (ch >= '0' && ch <= '9') return ch - '0';
  if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
  return -1;
}
std::string digest(const std::string &bytes) {
  unsigned char hash[32];
  if (mbedtls_sha256_ret(reinterpret_cast<const unsigned char *>(bytes.data()),
                         bytes.size(), hash, 0) != 0) return {};
  const char *hex = "0123456789abcdef";
  std::string result;
  for (auto byte : hash) { result += hex[byte >> 4]; result += hex[byte & 15]; }
  return result;
}
bool safeFilename(const char *name) {
  const size_t size = strlen(name);
  if (size < 6 || size > 35 || strcmp(name + size - 5, ".json")) return false;
  for (size_t i = 0; i < size - 5; ++i) {
    const char c = name[i];
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
          (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
  }
  return true;
}
}  // namespace

void StrategyStorage::clear() {
  active_ = false;
  plan_ = {};
  filename_.clear(); sha256_.clear(); upload_.clear(); expected_sha256_.clear();
  received_ = 0;
}

bool StrategyStorage::accept(const std::string &bytes, const CourseSpec &course,
                             const char *name, std::string &error, bool persist) {
  DrivingStrategy parsed;
  if (!parseDrivingStrategy(bytes.data(), bytes.size(), course, parsed, error)) return false;
  const auto checksum = digest(bytes);
  if (checksum.empty()) { error = "SHA256 failed"; return false; }
  if (persist) {
    File file = LittleFS.open("/host-strategy.tmp", "w");
    if (!file || file.write(reinterpret_cast<const uint8_t *>(bytes.data()), bytes.size()) != bytes.size()) {
      error = "strategy file write failed"; return false;
    }
    file.flush(); file.close();
    if (!LittleFS.rename("/host-strategy.tmp", "/host-strategy.json")) {
      error = "strategy file rename failed"; return false;
    }
  }
  plan_ = std::move(parsed);
  filename_ = name;
  sha256_ = checksum;
  active_ = true;
  return true;
}

bool StrategyStorage::load(const char *name, const CourseSpec &course, std::string &error) {
  if (!safeFilename(name)) { error = "invalid strategy filename"; return false; }
  const std::string path = std::string(name) == "host-strategy.json" ?
                             "/host-strategy.json" : std::string("/strategies/") + name;
  File file = LittleFS.open(path.c_str(), "r");
  if (!file || !file.size() || file.size() > kMaxStrategyBytes) {
    error = "strategy file missing or too large"; return false;
  }
  std::string bytes(file.size(), '\0');
  if (file.readBytes(&bytes[0], bytes.size()) != bytes.size()) {
    error = "strategy read failed"; return false;
  }
  return accept(bytes, course, name, error, false);
}

bool StrategyStorage::beginUpload(size_t bytes, const char *sha256, std::string &error) {
  upload_.clear(); expected_sha256_.clear(); received_ = 0;
  if (!bytes || bytes > kMaxStrategyBytes || strlen(sha256) != 64) {
    error = "invalid upload size or SHA256"; return false;
  }
  for (const char *p = sha256; *p; ++p)
    if (hexDigit(*p) < 0) { error = "invalid SHA256"; return false; }
  upload_.resize(bytes);
  expected_sha256_ = sha256;
  return true;
}

bool StrategyStorage::chunk(const char *hex, std::string &error) {
  const size_t digits = strlen(hex);
  if (upload_.empty() || !digits || digits % 2 || received_ + digits / 2 > upload_.size()) {
    error = "unexpected strategy chunk"; return false;
  }
  for (size_t i = 0; i < digits; ++i)
    if (hexDigit(hex[i]) < 0) { error = "invalid chunk hex"; return false; }
  for (size_t i = 0; i < digits; i += 2)
    upload_[received_++] = static_cast<char>((hexDigit(hex[i]) << 4) | hexDigit(hex[i + 1]));
  return true;
}

bool StrategyStorage::finishUpload(const CourseSpec &course, std::string &error) {
  if (upload_.empty() || received_ != upload_.size() || digest(upload_) != expected_sha256_) {
    error = "incomplete strategy upload or SHA256 mismatch"; return false;
  }
  const bool ok = accept(upload_, course, "host-strategy.json", error, true);
  upload_.clear(); expected_sha256_.clear(); received_ = 0;
  return ok;
}
}  // namespace sim
