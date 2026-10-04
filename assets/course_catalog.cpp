#include "course_catalog.h"

#include <cJSON.h>
#include <esp_heap_caps.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <atomic>

namespace tab5 {
namespace {
CourseAsset courses[kMaxCourses]{};
std::atomic<size_t> count{0};
constexpr size_t kImageBytes = 480 * 480 * 2;
void *jsonAlloc(size_t size) {
  return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}
void jsonFree(void *memory) { free(memory); }
const vega::CoursePoint empty_points[] = {{0, 0, 0}, {1, 0, 1}};
const vega::CourseData empty_data{
    empty_points, 2, 1, {0, 0}, 1, 1, {{1, 0, 0, 0, -1, 0}}, {}, "no_course", 2};

const cJSON *item(const cJSON *parent, const char *key) {
  return parent ? cJSON_GetObjectItemCaseSensitive(parent, key) : nullptr;
}
bool string(const cJSON *parent, const char *key, char *out, size_t size) {
  const cJSON *v = item(parent, key);
  if (!cJSON_IsString(v) || !v->valuestring || strlen(v->valuestring) >= size) return false;
  snprintf(out, size, "%s", v->valuestring);
  return true;
}
bool number(const cJSON *parent, const char *key, double &out) {
  const cJSON *v = item(parent, key);
  if (!cJSON_IsNumber(v) || !std::isfinite(v->valuedouble)) return false;
  out = v->valuedouble;
  return true;
}
char *readJson(const char *path, size_t limit) {
  FILE *f = fopen(path, "rb");
  if (!f) return nullptr;
  char *buffer = nullptr;
  if (fseek(f, 0, SEEK_END) == 0) {
    long length = ftell(f);
    if (length > 0 && static_cast<size_t>(length) <= limit && fseek(f, 0, SEEK_SET) == 0) {
      buffer = static_cast<char *>(heap_caps_malloc(length + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
      if (buffer && fread(buffer, 1, length, f) == static_cast<size_t>(length)) buffer[length] = 0;
      else { free(buffer); buffer = nullptr; }
    }
  }
  fclose(f);
  return buffer;
}
void release(CourseAsset &asset) {
  for (auto *points : asset.route_points) free(points);
  free(asset.image_pixels);
  asset.~CourseAsset();
  new (&asset) CourseAsset();
}
bool loadPath(const cJSON *routes, const char *key, CourseAsset &asset, size_t index) {
  const cJSON *route = item(routes, key);
  if (!route) return index == 3;
  const cJSON *points = item(route, "points");
  int count = cJSON_GetArraySize(points);
  double length = 0;
  if (!cJSON_IsArray(points) || count < 2 || count > 2048 ||
      !number(route, "length_m", length) || length <= 0) return false;
  auto *values = static_cast<vega::CoursePoint *>(heap_caps_malloc(
      sizeof(vega::CoursePoint) * count, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!values) return false;
  asset.route_points[index] = values;
  double previous = -1;
  for (int i = 0; i < count; ++i) {
    const cJSON *point = cJSON_GetArrayItem(points, i);
    if (!number(point, "east_m", values[i].east) ||
        !number(point, "north_m", values[i].north) ||
        !number(point, "s_m", values[i].s) || values[i].s <= previous) return false;
    previous = values[i].s;
  }
  if (values[0].s != 0 || fabs(previous - length) > 0.02 ||
      !cJSON_IsBool(item(route, "closed"))) return false;
  asset.data_storage.routes[index] = {values, static_cast<size_t>(count), length,
                                      cJSON_IsTrue(item(route, "closed")) != 0};
  return true;
}
bool loadOne(const cJSON *entry, CourseAsset &asset) {
  char folder[48]{};
  if (!string(entry, "folder", folder, sizeof(folder)) ||
      !string(entry, "name", asset.name_storage, sizeof(asset.name_storage)) ||
      !string(entry, "short_name", asset.short_name_storage, sizeof(asset.short_name_storage)) ||
      !string(entry, "course_id", asset.id_storage, sizeof(asset.id_storage)) ||
      !string(entry, "settings_namespace", asset.namespace_storage,
              sizeof(asset.namespace_storage)) ||
      !string(entry, "legacy_settings_namespace", asset.legacy_namespace_storage,
              sizeof(asset.legacy_namespace_storage))) return false;
  for (const char *p = folder; *p; ++p)
    if (!((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '_')) return false;
  for (const char *p = asset.id_storage; *p; ++p)
    if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
          (*p >= '0' && *p <= '9') || *p == '_' || *p == '-')) return false;
  for (const char *p = asset.namespace_storage; *p; ++p)
    if (!((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '-')) return false;
  if (!asset.namespace_storage[0] || !strcmp(asset.namespace_storage, "vega-device") ||
      !strcmp(asset.namespace_storage, "vega-select")) return false;
  asset.legacy_settings_namespace = asset.legacy_namespace_storage[0] ?
                                    asset.legacy_namespace_storage : nullptr;
  double n = 0;
  if (!number(entry, "legacy_total_target_s", n) || n < 0 || n > 86400) return false;
  asset.legacy_total_target_s = n;
  if (!cJSON_IsBool(item(entry, "pedestrian_gps"))) return false;
  asset.pedestrian_gps = cJSON_IsTrue(item(entry, "pedestrian_gps"));
  auto &defaults = asset.default_settings;
  if (!number(entry, "total_target_s", n) || n <= 0 || n > 86400) return false;
  defaults.total_target_s = n;
  if (!number(entry, "lap_target_s", n) || n <= 0 || n > 86400) return false;
  defaults.lap_target_s.fill(n);
  if (!number(entry, "course_corridor_m", defaults.course_corridor_m)) return false;
  if (item(entry, "min_lap_progress_m") &&
      !number(entry, "min_lap_progress_m", defaults.min_lap_progress_m)) return false;
  if (item(entry, "min_lap_ms")) {
    if (!number(entry, "min_lap_ms", n) || n < 0 || n > 3600000 || floor(n) != n)
      return false;
    defaults.min_lap_ms = static_cast<uint32_t>(n);
  }
  if (item(entry, "lap_duplicate_ms")) {
    if (!number(entry, "lap_duplicate_ms", n) || n < 0 || n > 60000 || floor(n) != n)
      return false;
    defaults.lap_duplicate_ms = static_cast<uint32_t>(n);
  }
  vega::GeoPoint *positions[] = {&defaults.start, &defaults.timing, &defaults.goal};
  const char *keys[] = {"start", "timing", "goal"};
  for (int i = 0; i < 3; ++i) {
    const cJSON *pair = item(entry, keys[i]);
    const cJSON *lat = cJSON_GetArrayItem(pair, 0);
    const cJSON *lon = cJSON_GetArrayItem(pair, 1);
    if (cJSON_GetArraySize(pair) != 2 || !cJSON_IsNumber(lat) || !cJSON_IsNumber(lon))
      return false;
    *positions[i] = {lat->valuedouble, lon->valuedouble};
    if (!vega::validGeo(*positions[i])) return false;
  }

  char path[128];
  snprintf(path, sizeof(path), "/sdcard/vega/courses/%s/course.json", folder);
  char *contents = readJson(path, 256 * 1024);
  if (!contents) return false;
  cJSON *root = cJSON_Parse(contents);
  free(contents);
  if (!root) return false;
  bool valid = false;
  do {
    char id[48]{};
    if (!string(root, "course_id", id, sizeof(id)) || strcmp(id, asset.id_storage) ||
        !number(root, "schema_version", n) || n != 2) break;
    const cJSON *laps = item(root, "lap_count");
    n = laps ? laps->valuedouble : 7;
    if ((laps && !cJSON_IsNumber(laps)) || n < 2 || n > 7 || floor(n) != n) break;
    asset.data_storage.lap_count = n;
    asset.data_storage.id = asset.id_storage;
    const cJSON *cs = item(root, "coordinate_system");
    const cJSON *render = item(root, "render");
    const cJSON *routes = item(root, "routes");
    const cJSON *segments = item(root, "segments");
    if (!number(cs, "origin_lat_deg", asset.data_storage.origin.latitude) ||
        !number(cs, "origin_lon_deg", asset.data_storage.origin.longitude) ||
        !number(cs, "east_m_per_lon_deg", asset.data_storage.east_per_degree) ||
        !number(cs, "north_m_per_lat_deg", asset.data_storage.north_per_degree) ||
        asset.data_storage.east_per_degree <= 0 || asset.data_storage.north_per_degree <= 0)
      break;
    const cJSON *matrix = item(render, "local_to_pixel_matrix_2x3");
    if (cJSON_GetArraySize(matrix) != 2) break;
    bool matrix_ok = true;
    for (int row = 0; row < 2; ++row) {
      const cJSON *values = cJSON_GetArrayItem(matrix, row);
      if (cJSON_GetArraySize(values) != 3) { matrix_ok = false; break; }
      for (int col = 0; col < 3; ++col) {
        const cJSON *v = cJSON_GetArrayItem(values, col);
        if (!cJSON_IsNumber(v) || !std::isfinite(v->valuedouble)) matrix_ok = false;
        else asset.data_storage.pixel_matrix[row * 3 + col] = v->valuedouble;
      }
    }
    if (!matrix_ok) break;
    if (!loadPath(routes, "first_lap", asset, 0) ||
        !loadPath(routes, "regular_lap", asset, 1) ||
        !loadPath(routes, "final_lap", asset, 2) ||
        !loadPath(segments, "finish_approach", asset, 3)) break;
    const auto regular = asset.data_storage.routes[1];
    asset.data_storage.points = regular.points;
    asset.data_storage.count = regular.count;
    asset.data_storage.length = regular.length;
    if (!vega::validCourseSettings(defaults)) break;
    snprintf(path, sizeof(path), "/sdcard/vega/courses/%s/map.rgb565", folder);
    FILE *image = fopen(path, "rb");
    if (!image) break;
    asset.image_pixels = static_cast<uint8_t *>(heap_caps_malloc(
        kImageBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    bool image_ok = asset.image_pixels && fread(asset.image_pixels, 1, kImageBytes, image) == kImageBytes &&
                    fgetc(image) == EOF;
    fclose(image);
    if (!image_ok) break;
    auto &descriptor = asset.image_storage;
    descriptor.header.magic = LV_IMAGE_HEADER_MAGIC;
    descriptor.header.cf = LV_COLOR_FORMAT_RGB565;
    descriptor.header.w = 480;
    descriptor.header.h = 480;
    descriptor.header.stride = 960;
    descriptor.data_size = kImageBytes;
    descriptor.data = asset.image_pixels;
    valid = true;
  } while (false);
  cJSON_Delete(root);
  return valid;
}
}  // namespace

bool loadCoursesFromSd() {
  if (count.load()) return true;
  cJSON_Hooks hooks{};
  hooks.malloc_fn = jsonAlloc;
  hooks.free_fn = jsonFree;
  cJSON_InitHooks(&hooks);
  char *contents = readJson("/sdcard/vega/courses/catalog.json", 16 * 1024);
  // An interrupted serial replacement may leave the previous catalog here.
  if (!contents)
    contents = readJson("/sdcard/vega/courses/catalog.json.bak", 16 * 1024);
  if (!contents) {
    printf("[COURSE] catalog.json missing or unreadable\n");
    return false;
  }
  cJSON *root = cJSON_Parse(contents);
  free(contents);
  if (!root) {
    printf("[COURSE] catalog.json is invalid JSON\n");
    return false;
  }
  const cJSON *items = item(root, "courses");
  int length = cJSON_GetArraySize(items);
  double schema = 0;
  bool valid = cJSON_IsArray(items) && length > 0 && length <= kMaxCourses &&
               number(root, "schema_version", schema) && schema == 1;
  if (valid) for (int i = 0; i < length; ++i) {
    auto &asset = courses[i];
    if (!loadOne(cJSON_GetArrayItem(items, i), asset)) {
      printf("[COURSE] invalid course package at catalog index %d\n", i);
      valid = false;
      break;
    }
    for (int old = 0; old < i; ++old)
      if (strcmp(asset.id_storage, courses[old].id_storage) == 0) valid = false;
    if (!valid) break;
  }
  if (valid) count.store(length);
  else for (auto &asset : courses) release(asset);
  cJSON_Delete(root);
  return valid;
}
size_t courseCount() { return count.load(); }
const CourseAsset &courseAsset(size_t index) {
  if (index < count.load()) return courses[index];
  static CourseAsset empty;
  static const bool initialized = []() {
    empty.data = &empty_data;
    empty.image = nullptr;
    empty.name = "NO COURSE";
    empty.short_name = "NO COURSE";
    empty.settings_namespace = "vega-empty";
    return true;
  }();
  (void)initialized;
  return empty;
}
size_t courseIndex(const char *id) {
  if (id) for (size_t i = 0; i < count.load(); ++i)
    if (strcmp(id, courses[i].data->id) == 0) return i;
  return 0;
}
const char *legacyNamespaceForId(const char *id, bool older) {
  if (!id) return nullptr;
  struct Entry { const char *id, *current, *older; };
  static const Entry migrations[] = {
      {"tamagawagakuen_station_loop_test_v1", "vega-test4", "vega-test"},
      {"tobitakyu_hospital_loop_test_v2", "vega-tobi4", "vega-tobi2"},
      {"motegi_oval_2025_full_v2", "vega", nullptr},
  };
  for (const auto &entry : migrations)
    if (!strcmp(id, entry.id)) return older ? entry.older : entry.current;
  return nullptr;
}
}  // namespace tab5
