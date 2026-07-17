#include "mongoose_config.h"  // Must be first, before mongoose.h
extern "C" {
#include "mongoose.h"
}
#include "zw111.h"
#include "zw111_web.h"
#include "esphome/core/log.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs_flash.h>
#include <nvs.h>

#ifdef USE_WIFI
#include "esphome/components/wifi/wifi_component.h"
#endif

namespace esphome {
namespace zw111 {

static const char *const TAG = "zw111";

// Single component pointer for Mongoose static handler
static ZW111Component *s_component = nullptr;

// ===== RTC 缓存结构 (深度睡眠保留, 断电丢失) =====
static RTC_DATA_ATTR struct {
  bool valid;
  uint32_t device_addr;
  uint8_t  score_level;
  uint8_t  reserv0[3];
  char product_sn[16];
  char software_ver[16];
  char manufacturer[16];
  char sensor_name[16];
  char led_type[16];
  char sensor_size[16];
  uint16_t database_size;
  uint16_t template_size;
  uint16_t templates_per_finger;
  uint32_t baud_rate;
  char sensor_check_result[16];
  uint8_t enrolled_bitmap[13];
  uint8_t enroll_max;
  uint8_t dup_block;
  uint16_t stored_count;
  uint8_t reserv2[2];
  bool  touch_pre_state;
  uint8_t reserv1[3];
  bool  was_sleeping;
} s_rtc = {false};

// ===== UART 协议 =====

uint16_t ZW111Component::calc_checksum(const uint8_t *data, uint16_t len) {
  uint16_t sum = 0;
  for (uint16_t i = 0; i < len; i++) sum += data[i];
  return sum;
}

bool ZW111Component::send_command(uint8_t cmd, const uint8_t *params, uint16_t param_len) {
  uint16_t payload_len = 1 + param_len;
  uint8_t *buf = tx_buffer_; uint16_t offset = 0;
  buf[offset++] = PACKET_HEADER_HI; buf[offset++] = PACKET_HEADER_LO;
  buf[offset++] = (device_addr_ >> 24) & 0xFF; buf[offset++] = (device_addr_ >> 16) & 0xFF;
  buf[offset++] = (device_addr_ >> 8) & 0xFF; buf[offset++] = device_addr_ & 0xFF;
  buf[offset++] = PKT_TYPE_COMMAND;
  uint16_t pkt_len = payload_len + 2;
  buf[offset++] = (pkt_len >> 8) & 0xFF; buf[offset++] = pkt_len & 0xFF;
  buf[offset++] = cmd;
  if (params && param_len) { memcpy(buf + offset, params, param_len); offset += param_len; }
  uint16_t csum = calc_checksum(buf + 6, 1 + 2 + payload_len);
  buf[offset++] = (csum >> 8) & 0xFF; buf[offset++] = csum & 0xFF;
  this->write_array(buf, offset); this->flush();
  return true;
}

bool ZW111Component::read_response(uint8_t *confirm, uint8_t *data, uint16_t data_len, uint32_t timeout_ms) {
  uint32_t start = millis();
  while (millis() - start < timeout_ms) {
    if (this->available() < 9) { delay(5); continue; }
    uint8_t b1 = this->read(); if (b1 != PACKET_HEADER_HI) continue;
    if (this->available() < 1) { delay(1); continue; }
    uint8_t b2 = this->read(); if (b2 != PACKET_HEADER_LO) continue;
    if (!this->read_array(rx_buffer_, 4)) return false;
    if (!this->read_array(rx_buffer_ + 4, 1)) return false;
    uint8_t pkt_type = rx_buffer_[4];
    if (!this->read_array(rx_buffer_ + 5, 2)) return false;
    uint16_t pkt_len = ((uint16_t) rx_buffer_[5] << 8) | rx_buffer_[6];
    if (pkt_len > MAX_PACKET_SIZE) continue;
    if (pkt_len > 0 && !this->read_array(rx_buffer_ + 7, pkt_len)) return false;
    uint16_t payload_data_len = pkt_len - 2;
    uint16_t cc = calc_checksum(rx_buffer_ + 4, 1 + 2 + payload_data_len);
    uint16_t rc = ((uint16_t) rx_buffer_[7 + payload_data_len] << 8) | rx_buffer_[7 + payload_data_len + 1];
    if (cc != rc) continue;
    if (confirm) *confirm = rx_buffer_[7];
    if (data && data_len && payload_data_len > 1) {
      uint16_t n = (data_len < (payload_data_len - 1)) ? data_len : (payload_data_len - 1);
      memcpy(data, rx_buffer_ + 8, n);
    }
    return true;
  }
  return false;
}

bool ZW111Component::read_data_packets(uint8_t *buffer, uint16_t buffer_size, uint16_t &total_read, uint32_t timeout_ms) {
  total_read = 0; uint32_t start = millis();
  while (millis() - start < timeout_ms) {
    if (this->available() < 9) { delay(5); continue; }
    uint8_t b1 = this->read(); if (b1 != PACKET_HEADER_HI) continue;
    if (this->available() < 1) { delay(1); continue; }
    uint8_t b2 = this->read(); if (b2 != PACKET_HEADER_LO) continue;
    uint8_t addr[4]; if (!this->read_array(addr, 4)) return false;
    if (!this->read_array(rx_buffer_, 1)) return false;
    uint8_t pkt_type = rx_buffer_[0];
    if (!this->read_array(rx_buffer_, 2)) return false;
    uint16_t pkt_len = ((uint16_t) rx_buffer_[0] << 8) | rx_buffer_[1];
    if (pkt_len > MAX_PACKET_SIZE || pkt_len < 2) continue;
    if (!this->read_array(rx_buffer_, pkt_len)) return false;
    uint16_t payload_len = pkt_len - 2;
    uint8_t cs[3] = {pkt_type, (uint8_t)(pkt_len >> 8), (uint8_t)(pkt_len & 0xFF)};
    uint16_t csum = calc_checksum(cs, 3) + calc_checksum(rx_buffer_, payload_len);
    uint16_t recv = ((uint16_t) rx_buffer_[payload_len] << 8) | rx_buffer_[payload_len + 1];
    if (csum != recv) continue;
    if (total_read + payload_len > buffer_size) return false;
    memcpy(buffer + total_read, rx_buffer_, payload_len);
    total_read += payload_len;
    if (pkt_type == PKT_TYPE_END) return true;
    start = millis();
  }
  return false;
}

void ZW111Component::flush_input() { while (this->available()) this->read(); }

// ===== 系统设置 NVS =====

std::string ZW111Component::nvs_ns() { return nvs_prefix_.empty() ? "zw111_n" : nvs_prefix_ + "_n"; }
std::string ZW111Component::nvs_cfg() { return nvs_prefix_.empty() ? "zw111_c" : nvs_prefix_ + "_c"; }

void ZW111Component::load_settings_from_nvs() {
  nvs_handle_t handle;
  std::string ns = nvs_cfg();
  if (nvs_open(ns.c_str(), NVS_READONLY, &handle) == ESP_OK) {
    uint8_t v;
    if (nvs_get_u8(handle, "enroll_max", &v) == ESP_OK) setting_enroll_max_ = v;
    if (nvs_get_u8(handle, "dup_block", &v) == ESP_OK) setting_dup_block_ = v;
    nvs_close(handle);
  }
  if (setting_enroll_max_ < 1 || setting_enroll_max_ > 8) setting_enroll_max_ = 5;
  if (setting_dup_block_ > 1) setting_dup_block_ = 0;
  ESP_LOGI(TAG, "Settings loaded: enroll_max=%d dup_block=%d", setting_enroll_max_, setting_dup_block_);
}

void ZW111Component::save_setting_to_nvs(const char *key, uint8_t value) {
  nvs_handle_t handle;
  std::string ns = nvs_cfg();
  if (nvs_open(ns.c_str(), NVS_READWRITE, &handle) == ESP_OK) {
    nvs_set_u8(handle, key, value);
    nvs_commit(handle);
    nvs_close(handle);
  }
}

void ZW111Component::apply_score_level_to_module(uint8_t level) {
  uint8_t reg_params[2] = {0x05, level};
  flush_input();
  send_command(CMD_WRITE_REG, reg_params, 2);
  uint8_t confirm = 0xFF;
  if (read_response(&confirm) && confirm == CONFIRM_OK) {
    info_.score_level = level;
    ESP_LOGI(TAG, "Score level set to %d via PS_WriteReg", level);
  } else {
    ESP_LOGW(TAG, "Failed to set score level (confirm=0x%02X)", confirm);
  }
}

void ZW111Component::save_device_addr_nvs() {
  nvs_handle_t handle;
  std::string ns = nvs_cfg();
  if (nvs_open(ns.c_str(), NVS_READWRITE, &handle) == ESP_OK) {
    nvs_set_u32(handle, "dev_addr", device_addr_);
    nvs_set_u8(handle, "score_lvl", (uint8_t)info_.score_level);
    nvs_commit(handle);
    nvs_close(handle);
  }
}

bool ZW111Component::load_device_addr_nvs() {
  nvs_handle_t handle;
  std::string ns = nvs_cfg();
  if (nvs_open(ns.c_str(), NVS_READONLY, &handle) == ESP_OK) {
    uint32_t addr = 0;
    if (nvs_get_u32(handle, "dev_addr", &addr) == ESP_OK && addr != 0 && addr != DEFAULT_DEVICE_ADDRESS) {
      device_addr_ = addr;
      uint8_t sl = 0;
      if (nvs_get_u8(handle, "score_lvl", &sl) == ESP_OK && sl >= 1 && sl <= 5) {
        info_.score_level = sl;
      }
      nvs_close(handle);
      return true;
    }
    nvs_close(handle);
  }
  return false;
}

void ZW111Component::save_info_nvs() {
  nvs_handle_t handle;
  std::string ns = nvs_cfg();
  if (nvs_open(ns.c_str(), NVS_READWRITE, &handle) == ESP_OK) {
    nvs_set_str(handle, "prod_sn", info_.product_sn.c_str());
    nvs_set_str(handle, "sw_ver", info_.software_ver.c_str());
    nvs_set_str(handle, "mfg", info_.manufacturer.c_str());
    nvs_set_str(handle, "sensor_n", info_.sensor_name.c_str());
    nvs_set_str(handle, "led_type", info_.led_type.c_str());
    nvs_set_str(handle, "sensor_sz", info_.sensor_size.c_str());
    nvs_set_u16(handle, "db_size", (uint16_t)info_.database_size);
    nvs_set_u16(handle, "tmpl_sz", (uint16_t)info_.template_size);
    nvs_set_u16(handle, "tmpl_pf", (uint16_t)info_.templates_per_finger);
    nvs_set_u32(handle, "baud", (uint32_t)info_.baud_rate);
    nvs_commit(handle);
    nvs_close(handle);
  }
}

void ZW111Component::save_sensor_check_nvs() {
  nvs_handle_t handle;
  std::string ns = nvs_cfg();
  if (nvs_open(ns.c_str(), NVS_READWRITE, &handle) == ESP_OK) {
    nvs_set_str(handle, "sensor_chk", info_.sensor_check_result.c_str());
    nvs_commit(handle);
    nvs_close(handle);
  }
}

void ZW111Component::load_sensor_check_nvs() {
  nvs_handle_t handle;
  std::string ns = nvs_cfg();
  if (nvs_open(ns.c_str(), NVS_READONLY, &handle) == ESP_OK) {
    size_t len = 0;
    char buf[32];
    if (nvs_get_str(handle, "sensor_chk", nullptr, &len) == ESP_OK && len > 0) {
      nvs_get_str(handle, "sensor_chk", buf, &len);
      info_.sensor_check_result = buf;
      nvs_close(handle);
      return;
    }
    nvs_close(handle);
  }
  info_.sensor_check_result = "";
}

bool ZW111Component::load_info_nvs() {
  nvs_handle_t handle;
  std::string ns = nvs_cfg();
  if (nvs_open(ns.c_str(), NVS_READONLY, &handle) == ESP_OK) {
    size_t len = 0;
    char buf[64];
    if (nvs_get_str(handle, "prod_sn", nullptr, &len) == ESP_OK && len > 0) {
      nvs_get_str(handle, "prod_sn", buf, &len); info_.product_sn = buf;
      len = sizeof(buf); if (nvs_get_str(handle, "sw_ver", buf, &len) == ESP_OK) info_.software_ver = buf;
      len = sizeof(buf); if (nvs_get_str(handle, "mfg", buf, &len) == ESP_OK) info_.manufacturer = buf;
      len = sizeof(buf); if (nvs_get_str(handle, "sensor_n", buf, &len) == ESP_OK) info_.sensor_name = buf;
      len = sizeof(buf); if (nvs_get_str(handle, "led_type", buf, &len) == ESP_OK) info_.led_type = buf;
      len = sizeof(buf); if (nvs_get_str(handle, "sensor_sz", buf, &len) == ESP_OK) info_.sensor_size = buf;
      uint16_t u16 = 0; if (nvs_get_u16(handle, "db_size", &u16) == ESP_OK) info_.database_size = u16;
      u16 = 0; if (nvs_get_u16(handle, "tmpl_sz", &u16) == ESP_OK) info_.template_size = u16;
      u16 = 0; if (nvs_get_u16(handle, "tmpl_pf", &u16) == ESP_OK) info_.templates_per_finger = u16;
      uint32_t u32 = 0; if (nvs_get_u32(handle, "baud", &u32) == ESP_OK) info_.baud_rate = u32;
      char h[16]; snprintf(h, 16, "0x%08X", device_addr_); info_.device_addr = h;
      nvs_close(handle);
      return true;
    }
    nvs_close(handle);
  }
  return false;
}

std::string ZW111Component::extract_ascii_string(const uint8_t *data, uint16_t len) {
  char buf[64];
  uint16_t n = (len < (uint16_t) sizeof(buf) - 1) ? len : (uint16_t) sizeof(buf) - 1;
  memcpy(buf, data, n); buf[n] = '\0';
  for (int i = n - 1; i >= 0; i--)
    if (buf[i] == ' ' || buf[i] == '\0' || buf[i] == 0xFF || buf[i] == '\r' || buf[i] == '\n') buf[i] = '\0';
    else break;
  char *s = buf; while (*s && ((uint8_t) *s < 0x20 || (uint8_t) *s == 0xFF)) s++;
  return std::string(s);
}

const char *ZW111Component::led_type_to_name(uint16_t lt) {
  constexpr size_t n = sizeof(LED_TYPE_NAMES) / sizeof(LED_TYPE_NAMES[0]);
  return lt < n ? LED_TYPE_NAMES[lt] : "Unknown";
}

// ===== LED =====

void ZW111Component::led_control(uint8_t func, uint8_t color, uint8_t cycles, uint8_t time_10ms) {
  uint8_t params[7] = {func, color, color, 0x00, cycles, time_10ms};
  flush_input();
  send_command(CMD_CONTROL_BLN, params, 6);
  uint8_t c; read_response(&c);
  delay(30);
}

void ZW111Component::led_all_off() { led_control(0x04, 0x00, 0x00, 0x00); }
void ZW111Component::led_green_steady() { led_control(0x03, 0x02, 0x00, 0x00); }
void ZW111Component::led_red_blink(uint8_t times) { led_control(0x02, 0x04, times, 3); }
void ZW111Component::led_blue_blink() { led_control(0x01, 0x01, 0x00, 10); }

// ===== 诊断 =====

bool ZW111Component::do_handshake() { if (!send_command(CMD_HANDSHAKE)) return false; uint8_t c; return read_response(&c) && c == CONFIRM_OK; }

bool ZW111Component::do_read_para() {
  if (!send_command(CMD_READ_SYS_PARA)) return false;
  { uint8_t c; uint8_t p[16];
    if (!read_response(&c, p, 16) || c != CONFIRM_OK) return false;
    info_.enroll_count = ((uint16_t)p[0]<<8)|p[1]; info_.template_size = ((uint16_t)p[2]<<8)|p[3];
    info_.database_size = ((uint16_t)p[4]<<8)|p[5]; info_.score_level = ((uint16_t)p[6]<<8)|p[7];
    uint32_t da = ((uint32_t)p[8]<<24)|((uint32_t)p[9]<<16)|((uint32_t)p[10]<<8)|p[11];
    info_.baud_rate = ((uint16_t)p[14]<<8)|p[15] * 9600; device_addr_ = da;
    char h[16]; snprintf(h, 16, "0x%08X", da); info_.device_addr = h;
  }
  if (!send_command(CMD_READ_ADD_PARA)) return false;
  { uint8_t c; uint8_t p[8];
    if (!read_response(&c, p, 8) || c != CONFIRM_OK) return false;
    uint16_t sh = ((uint16_t)p[0]<<8)|p[1], sw = ((uint16_t)p[2]<<8)|p[3], lt = ((uint16_t)p[4]<<8)|p[5];
    info_.templates_per_finger = ((uint16_t)p[6]<<8)|p[7];
    char b[32]; snprintf(b, 32, "%dx%d px", sw, sh); info_.sensor_size = b;
    info_.led_type = led_type_to_name(lt);
  }
  if (!send_command(CMD_READ_INF_PAGE)) return false;
  { uint8_t c; if (!read_response(&c) || c != CONFIRM_OK) return false;
    uint8_t page[INF_PAGE_SIZE]; uint16_t tr = 0;
    if (!read_data_packets(page, INF_PAGE_SIZE, tr, LONG_TIMEOUT_MS)) return false;
    if (tr > 40) info_.product_sn = extract_ascii_string(page + 28, 8);
    if (tr > 48) info_.software_ver = extract_ascii_string(page + 36, 8);
    if (tr > 56) info_.manufacturer = extract_ascii_string(page + 44, 8);
    if (tr > 64) info_.sensor_name = extract_ascii_string(page + 52, 8);
  }
  return true;
}

bool ZW111Component::do_read_count() {
  if (!send_command(CMD_VALID_TEMPLATE_NUM)) return false;
  uint8_t confirm; uint8_t vn_buf[2] = {0, 0};
  if (!read_response(&confirm, vn_buf, 2) || confirm != CONFIRM_OK) return false;
  info_.stored_count = ((uint16_t)vn_buf[0] << 8) | vn_buf[1];
  return true;
}

bool ZW111Component::do_read_index_table() {
  memset(enrolled_, 0, sizeof(enrolled_));
  uint8_t page_param[1] = {0x00};
  flush_input();
  if (!send_command(CMD_READ_INDEX_TABLE, page_param, 1)) {
    ESP_LOGW(TAG, "ReadIndexTable: send failed");
    return false;
  }
  uint8_t confirm; uint8_t idx_data[32] = {0};
  if (read_response(&confirm, idx_data, 32, LONG_TIMEOUT_MS) && confirm == CONFIRM_OK) {
    for (int byte_i = 0; byte_i < 32; byte_i++) {
      uint8_t b = idx_data[byte_i];
      for (int bit_i = 0; bit_i < 8; bit_i++) {
        int id = byte_i * 8 + bit_i;
        if (id >= 100) break;
        enrolled_[id] = (b & 0x01) != 0;
        b >>= 1;
      }
    }
    ESP_LOGI(TAG, "IndexTable page0 loaded from UART");
    return true;
  } else {
    ESP_LOGW(TAG, "ReadIndexTable: response failed (confirm=0x%02X)", confirm);
    return false;
  }
}

bool ZW111Component::do_check_sensor() {
  flush_input();
  if (!send_command(CMD_CHECK_SENSOR)) {
    ESP_LOGW(TAG, "CheckSensor: send failed");
    info_.sensor_check_result = "发送失败";
    return false;
  }
  uint8_t confirm = 0xFF;
  if (read_response(&confirm) && confirm == CONFIRM_OK) {
    info_.sensor_check_result = "正常";
    ESP_LOGI(TAG, "CheckSensor: OK");
    return true;
  } else {
    if (confirm == 0x29) info_.sensor_check_result = "传感器校验出错";
    else info_.sensor_check_result = "状态异常";
    ESP_LOGW(TAG, "CheckSensor: failed (confirm=0x%02X)", confirm);
    return false;
  }
}

// ===== Notepad NVS =====

void ZW111Component::write_notepad_nvs(int id, const std::string &content) {
  nvs_handle_t handle;
  std::string ns = nvs_ns();
  if (nvs_open(ns.c_str(), NVS_READWRITE, &handle) == ESP_OK) {
    char key[8]; snprintf(key, sizeof(key), "n%d", id);
    nvs_set_str(handle, key, content.c_str());
    nvs_commit(handle);
    nvs_close(handle);
  }
}

std::string ZW111Component::read_notepad_nvs(int id) {
  nvs_handle_t handle;
  std::string ns = nvs_ns();
  if (nvs_open(ns.c_str(), NVS_READONLY, &handle) == ESP_OK) {
    char key[8]; snprintf(key, sizeof(key), "n%d", id);
    size_t len = 0;
    if (nvs_get_str(handle, key, nullptr, &len) == ESP_OK) {
      char *buf = new char[len];
      nvs_get_str(handle, key, buf, &len);
      std::string s(buf);
      delete[] buf;
      nvs_close(handle);
      return s;
    }
    nvs_close(handle);
  }
  return "";
}

void ZW111Component::load_all_notepads() {
  if (load_index_table_from_nvs()) {
    nvs_has_index_cache_ = true;
    ESP_LOGI(TAG, "IndexTable loaded from NVS cache");
  }
  for (int i = 0; i < 100; i++) {
    notepad_cache_[i] = read_notepad_nvs(i);
  }
  ESP_LOGI(TAG, "Notepad cache loaded (100 entries) from NVS");
}

void ZW111Component::flush_dirty_notepad() {
  for (int i = 0; i < 100; i++) {
    if (notepad_dirty_[i]) {
      notepad_dirty_[i] = false;
      write_notepad_nvs(i, notepad_cache_[i]);
      return;
    }
  }
}

// ===== IndexTable NVS =====

void ZW111Component::save_index_table_to_nvs() {
  nvs_handle_t handle;
  std::string ns = nvs_ns();
  if (nvs_open(ns.c_str(), NVS_READWRITE, &handle) == ESP_OK) {
    uint8_t packed[13] = {0};
    for (int i = 0; i < 100; i++) {
      if (enrolled_[i]) packed[i / 8] |= (1 << (i % 8));
    }
    nvs_set_blob(handle, "idx_table", packed, 13);
    nvs_commit(handle);
    nvs_close(handle);
    nvs_has_index_cache_ = true;
  }
}

bool ZW111Component::load_index_table_from_nvs() {
  nvs_handle_t handle;
  std::string ns = nvs_ns();
  if (nvs_open(ns.c_str(), NVS_READONLY, &handle) == ESP_OK) {
    size_t len = 0;
    if (nvs_get_blob(handle, "idx_table", nullptr, &len) == ESP_OK && len >= 13) {
      uint8_t packed[13] = {0};
      if (nvs_get_blob(handle, "idx_table", packed, &len) == ESP_OK && len >= 13) {
        memset(enrolled_, 0, sizeof(enrolled_));
        for (int i = 0; i < 100; i++) {
          enrolled_[i] = (packed[i / 8] & (1 << (i % 8))) != 0;
        }
        nvs_close(handle);
        return true;
      }
    }
    nvs_close(handle);
  }
  return false;
}

void ZW111Component::sync_index_table_from_uart() {
  ESP_LOGI(TAG, "Syncing IndexTable from UART...");
  if (do_read_index_table()) {
    save_index_table_to_nvs();
    do_read_count();
    ESP_LOGI(TAG, "IndexTable sync OK, stored=%d", (int)info_.stored_count);
  } else {
    ESP_LOGW(TAG, "IndexTable sync from UART failed");
  }
}

// ===== 生命周期 =====

ZW111Component::~ZW111Component() {
  if (mgr_inited_) {
    mg_mgr_free(&mgr_);
    mgr_inited_ = false;
  }
}

void ZW111Component::power_on_and_init() {
  bool was_sleeping = s_rtc.was_sleeping;
  if (was_sleeping) {
    ESP_LOGI(TAG, "Waking from deepsleep (fast init)");
  }

  if (power_ctl_pin_ != nullptr) {
    power_ctl_pin_->digital_write(true);
    delay(was_sleeping ? 100 : 200);
  }
  flush_input();
  uint32_t wait_0x55 = was_sleeping ? 100 : 200;
  uint32_t t = millis(); bool got = false;
  while (millis() - t < wait_0x55) { if (this->available() && this->read() == 0x55) { got = true; break; } delay(5); }
  if (!got) { delay(was_sleeping ? 40 : 80); }
  int max_retry = was_sleeping ? 1 : 2;
  bool ok = false;
  for (int i = 0; i < max_retry && !ok; i++) { flush_input(); delay(50); if (do_handshake()) { ok = true; break; } if (i < max_retry - 1) delay(200); }
  if (!ok) {
    if (connection_status_) connection_status_->publish_state(false);
    initialized_ = false;
    return;
  }

  if (was_sleeping) {
    if (!load_device_addr_nvs()) {
      flush_input(); delay(10);
      if (!send_command(CMD_READ_SYS_PARA)) return;
      { uint8_t c; uint8_t p[16];
        if (!read_response(&c, p, 16) || c != CONFIRM_OK) return;
        uint32_t da = ((uint32_t)p[8]<<24)|((uint32_t)p[9]<<16)|((uint32_t)p[10]<<8)|p[11];
        device_addr_ = da; char h[16]; snprintf(h, 16, "0x%08X", da); info_.device_addr = h;
        info_.score_level = ((uint16_t)p[6]<<8)|p[7];
        info_.baud_rate = ((uint16_t)p[14]<<8)|p[15] * 9600;
      }
    }
    load_info_nvs();
    load_sensor_check_nvs();
    if (connection_status_) connection_status_->publish_state(true);
    if (fp_identify_sensor_) fp_identify_sensor_->publish_state("-");
    led_all_off();
  } else {
    flush_input(); delay(30); do_read_para(); flush_input(); delay(30); do_read_count();
    load_all_notepads();
    load_settings_from_nvs();
    info_.enroll_count = setting_enroll_max_;
    if (info_.score_level >= 1 && info_.score_level <= 5) apply_score_level_to_module((uint8_t)info_.score_level);
    if (!nvs_has_index_cache_) { flush_input(); delay(10); if (do_read_index_table()) save_index_table_to_nvs(); }
    if (connection_status_) connection_status_->publish_state(true);
    if (do_check_sensor()) {
      if (sensor_check_status_) sensor_check_status_->publish_state(false);
    } else {
      if (sensor_check_status_) sensor_check_status_->publish_state(true);
    }
    if (fp_identify_sensor_) fp_identify_sensor_->publish_state("-");
    {
      uint8_t mode_params[1] = {0x00};
      flush_input(); send_command(CMD_BLN_MODE_SW, mode_params, 1);
      uint8_t c; read_response(&c); delay(50);
    }
    led_all_off();
    {
      flush_input();
      uint8_t id_params[5] = {(uint8_t)info_.score_level, 0xFF, 0xFF, 0x00, 0x00};
      send_command(CMD_AUTO_IDENTIFY, id_params, 5); delay(80);
      flush_input(); send_command(CMD_CANCEL);
      uint8_t dummy; read_response(&dummy, nullptr, 0, 500);
      flush_input(); delay(30);
    }
    save_device_addr_nvs();
    save_info_nvs();
    save_sensor_check_nvs();
    populate_rtc_cache();
  }

  initialized_ = true;
  if (was_sleeping) {
    s_rtc.was_sleeping = false;
  }
  ESP_LOGI(TAG, "ZW111 init complete (was_sleeping=%d)", was_sleeping);

  if (was_sleeping && touch_sense_pin_ != nullptr && touch_sensor_ != nullptr) {
    if (touch_sense_pin_->digital_read()) {
      ESP_LOGI(TAG, "Touch sensor active after wake, auto-triggering identify");
      trigger_identify();
    }
  }
}

void ZW111Component::setup() {
  s_component = this;

  if (touch_sense_pin_ != nullptr) {
    touch_sense_pin_->setup();
  }

  if (s_rtc.was_sleeping) {
    if (power_ctl_pin_ != nullptr) {
      power_ctl_pin_->setup();
      power_ctl_pin_->digital_write(true);
    }
    power_on_start_ms_ = millis();
    s_rtc.touch_pre_state = (touch_sense_pin_ != nullptr) ? touch_sense_pin_->digital_read() : false;
    wake_pending_ = true;
    initialized_ = false;
    ESP_LOGD(TAG, "Wake: power on at %ums, touch=%d, will init later", power_on_start_ms_, (int)s_rtc.touch_pre_state);
    return;
  }

  if (power_ctl_pin_ != nullptr) {
    power_ctl_pin_->setup();
    power_ctl_pin_->digital_write(true);
    delay(200);
  }

  flush_input();
  uint32_t t = millis(); bool got = false;
  while (millis() - t < 200) { if (this->available() && this->read() == 0x55) { got = true; break; } delay(5); }
  if (!got) { delay(80); }
  bool ok = false;
  for (int i = 0; i < 2; i++) { flush_input(); delay(50); if (do_handshake()) { ok = true; break; } delay(200); }
  if (!ok) { if (connection_status_) connection_status_->publish_state(false); mark_failed(); return; }

  flush_input(); delay(30); do_read_para(); flush_input(); delay(30); do_read_count();
  load_all_notepads();
  load_settings_from_nvs();
  info_.enroll_count = setting_enroll_max_;
  if (info_.score_level >= 1 && info_.score_level <= 5) apply_score_level_to_module((uint8_t)info_.score_level);
  if (!nvs_has_index_cache_) { flush_input(); delay(10); if (do_read_index_table()) save_index_table_to_nvs(); }
  if (connection_status_) connection_status_->publish_state(true);

  if (do_check_sensor()) {
    if (sensor_check_status_) sensor_check_status_->publish_state(false);
  } else {
    if (sensor_check_status_) sensor_check_status_->publish_state(true);
  }
  if (fp_identify_sensor_) fp_identify_sensor_->publish_state("-");

  {
    uint8_t mode_params[1] = {0x00};
    flush_input(); send_command(CMD_BLN_MODE_SW, mode_params, 1);
    uint8_t c; read_response(&c); delay(50);
  }
  led_all_off();
  {
    flush_input();
    uint8_t id_params[5] = {(uint8_t)info_.score_level, 0xFF, 0xFF, 0x00, 0x00};
    send_command(CMD_AUTO_IDENTIFY, id_params, 5); delay(80);
    flush_input(); send_command(CMD_CANCEL);
    uint8_t dummy; read_response(&dummy, nullptr, 0, 500);
    flush_input(); delay(30);
  }
  save_device_addr_nvs();
  save_info_nvs();
  save_sensor_check_nvs();
  populate_rtc_cache();
  initialized_ = true;
  ESP_LOGI(TAG, "ZW111 ready");
}

static uint32_t last_touch_read_ms = 0;

// ===== RTC 缓存填充/恢复 =====

void ZW111Component::populate_rtc_cache() {
  s_rtc.valid = true;
  s_rtc.device_addr = device_addr_;
  s_rtc.score_level = (uint8_t)info_.score_level;
  s_rtc.database_size = (uint16_t)info_.database_size;
  s_rtc.template_size = (uint16_t)info_.template_size;
  s_rtc.templates_per_finger = (uint16_t)info_.templates_per_finger;
  s_rtc.baud_rate = (uint32_t)info_.baud_rate;
  s_rtc.enroll_max = setting_enroll_max_;
  s_rtc.dup_block = setting_dup_block_;
  snprintf(s_rtc.product_sn, sizeof(s_rtc.product_sn), "%s", info_.product_sn.c_str());
  snprintf(s_rtc.software_ver, sizeof(s_rtc.software_ver), "%s", info_.software_ver.c_str());
  snprintf(s_rtc.manufacturer, sizeof(s_rtc.manufacturer), "%s", info_.manufacturer.c_str());
  snprintf(s_rtc.sensor_name, sizeof(s_rtc.sensor_name), "%s", info_.sensor_name.c_str());
  snprintf(s_rtc.led_type, sizeof(s_rtc.led_type), "%s", info_.led_type.c_str());
  snprintf(s_rtc.sensor_size, sizeof(s_rtc.sensor_size), "%s", info_.sensor_size.c_str());
  if (!info_.sensor_check_result.empty()) {
    snprintf(s_rtc.sensor_check_result, sizeof(s_rtc.sensor_check_result), "%s", info_.sensor_check_result.c_str());
  }
  memset(s_rtc.enrolled_bitmap, 0, sizeof(s_rtc.enrolled_bitmap));
  for (int i = 0; i < 100; i++) {
    if (enrolled_[i]) s_rtc.enrolled_bitmap[i / 8] |= (1 << (i % 8));
  }
  s_rtc.stored_count = (uint16_t)info_.stored_count;
  s_rtc.touch_pre_state = (touch_sense_pin_ != nullptr) ? touch_sense_pin_->digital_read() : false;
  ESP_LOGD(TAG, "RTC cache populated (%d bytes)", (int)sizeof(s_rtc));
}

void ZW111Component::restore_from_rtc_cache() {
  if (!s_rtc.valid) return;
  device_addr_ = s_rtc.device_addr;
  info_.score_level = s_rtc.score_level;
  info_.database_size = s_rtc.database_size;
  info_.template_size = s_rtc.template_size;
  info_.templates_per_finger = s_rtc.templates_per_finger;
  info_.baud_rate = s_rtc.baud_rate;
  info_.product_sn = s_rtc.product_sn;
  info_.software_ver = s_rtc.software_ver;
  info_.manufacturer = s_rtc.manufacturer;
  info_.sensor_name = s_rtc.sensor_name;
  info_.led_type = s_rtc.led_type;
  info_.sensor_size = s_rtc.sensor_size;
  info_.sensor_check_result = s_rtc.sensor_check_result;
  char h[16]; snprintf(h, 16, "0x%08X", device_addr_); info_.device_addr = h;
  setting_enroll_max_ = s_rtc.enroll_max;
  setting_dup_block_ = s_rtc.dup_block;
  memset(enrolled_, 0, sizeof(enrolled_));
  for (int i = 0; i < 100; i++) {
    enrolled_[i] = (s_rtc.enrolled_bitmap[i / 8] & (1 << (i % 8))) != 0;
  }
  nvs_has_index_cache_ = true;
  info_.stored_count = s_rtc.stored_count;
  ESP_LOGD(TAG, "RTC cache restored (stored_count=%.0f)", info_.stored_count);
}

// ===== 分阶段初始化: 在第一次loop()中完成UART握手 =====

void ZW111Component::initialize_later() {
  uint32_t elapsed = millis() - power_on_start_ms_;
  ESP_LOGD(TAG, "ZW111 has been powered for %dms, starting delayed init", elapsed);

  flush_input();
  uint32_t wait_0x55 = (elapsed > 100) ? 40 : 100;
  uint32_t t = millis(); bool got = false;
  while (millis() - t < wait_0x55) { if (this->available() && this->read() == 0x55) { got = true; break; } delay(2); }
  if (!got) { delay(30); }

  if (!do_handshake()) {
    ESP_LOGW(TAG, "Delayed handshake failed, retrying with fresh init...");
    flush_input(); delay(30);
    if (!do_handshake()) {
      if (connection_status_) connection_status_->publish_state(false);
      ESP_LOGE(TAG, "Delayed init failed after power-on");
      wake_pending_ = false;
      return;
    }
  }

  restore_from_rtc_cache();

  if (connection_status_) connection_status_->publish_state(true);

  if (s_rtc.touch_pre_state) {
    s_rtc.was_sleeping = false;
    initialized_ = true;
    wake_pending_ = false;
    ESP_LOGI(TAG, "ZW111 delayed init complete (touch active, fast identify)");
    if (fp_identify_sensor_) fp_identify_sensor_->publish_state("-");
    // 第一时间闪烁蓝灯, 给用户触控唤醒即开始的视觉反馈
    led_blue_blink();
    trigger_identify();
    return;
  }

  if (fp_identify_sensor_) fp_identify_sensor_->publish_state("-");
  led_all_off();


  s_rtc.was_sleeping = false;
  initialized_ = true;
  wake_pending_ = false;

  ESP_LOGI(TAG, "ZW111 delayed init complete (power-on->ready: %dms)", millis() - power_on_start_ms_);
}

void ZW111Component::trigger_web_refresh() {
  if (initialized_ && !web_refresh_triggered_) {
    web_refresh_triggered_ = true;
    ESP_LOGI(TAG, "Web refresh triggered, will refresh UART data in loop()");
  }
}

void ZW111Component::loop() {
  if (wake_pending_) {
    initialize_later();
    if (!initialized_) return;
  }

  if (initialized_ && web_refresh_triggered_) {
    web_refresh_triggered_ = false;
    ESP_LOGI(TAG, "Executing background UART refresh for web...");
    if (!nvs_has_index_cache_) {
      load_index_table_from_nvs();
    }
    load_settings_from_nvs();
  }

  if (touch_sense_pin_ != nullptr && touch_sensor_ != nullptr) {
    uint32_t interval = initialized_ ? 50 : 10;
    if (millis() - last_touch_read_ms >= interval) {
      last_touch_read_ms = millis();
      touch_sensor_->publish_state(touch_sense_pin_->digital_read());
    }
  }

  if (!initialized_) {
    return;
  }
  flush_dirty_notepad();

  if (!web_started_ && web_port_ > 0) {
#ifdef USE_WIFI
    if (wifi::global_wifi_component->can_proceed())
#endif
    {
      web_started_ = true;
      start_mongoose_server();
    }
  }
  if (mgr_inited_) {
    mg_mgr_poll(&mgr_, 0);
  }
}

void ZW111Component::dump_config() { ESP_LOGCONFIG(TAG, "ZW111 Init: %s Port: %d", initialized_?"YES":"NO", web_port_); }

// ===== Mongoose Auth =====

bool ZW111Component::check_auth(const std::string &header_value) {
  if (web_user_.empty()) return true;
  std::string exp = web_user_ + ":" + web_pass_;
  char enc[128]; size_t elen = 0;
  for (size_t i = 0; i < exp.size(); i += 3) {
    uint32_t bits = ((uint8_t)exp[i]) << 16;
    if (i+1 < exp.size()) bits |= ((uint8_t)exp[i+1]) << 8;
    if (i+2 < exp.size()) bits |= (uint8_t)exp[i+2];
    enc[elen++] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"[(bits>>18)&0x3F];
    enc[elen++] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"[(bits>>12)&0x3F];
    enc[elen++] = (i+1<exp.size()) ? "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"[(bits>>6)&0x3F] : '=';
    enc[elen++] = (i+2<exp.size()) ? "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"[bits&0x3F] : '=';
  }
  enc[elen] = '\0';
  return header_value == std::string(enc);
}

// ===== Mongoose HTTP Handler =====

#define CORS_HDRS "Access-Control-Allow-Origin: *\r\nAccess-Control-Allow-Methods: GET, POST, OPTIONS\r\nAccess-Control-Allow-Headers: Content-Type, Authorization, X-Auth-Credentials\r\n"

static void mg_ev_handler(struct mg_connection *c, int ev, void *ev_data) {
  if (ev == MG_EV_HTTP_MSG) {
    struct mg_http_message *hm = (struct mg_http_message *) ev_data;
    ZW111Component *self = s_component;
    if (!self) { mg_http_reply(c, 500, CORS_HDRS, "{\"error\":\"no component\"}\n"); return; }

    struct mg_str uri = hm->uri;

    if (mg_strcmp(mg_str("OPTIONS"), hm->method) == 0) {
      mg_http_reply(c, 200, CORS_HDRS, "");
      return;
    }

    if (mg_strcmp(uri, mg_str("/")) == 0 || mg_strcmp(uri, mg_str("/index.html")) == 0) {
      self->trigger_web_refresh();
      mg_printf(c, "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n" CORS_HDRS
                "Content-Length: %d\r\n\r\n", (int) strlen(ZW111_HTML));
      mg_send(c, ZW111_HTML, strlen(ZW111_HTML));
      return;
    }

    struct mg_str *auth_hdr = mg_http_get_header(hm, "X-Auth-Credentials");
    std::string auth_str(auth_hdr ? (auth_hdr->buf ? auth_hdr->buf : "") : "", auth_hdr ? auth_hdr->len : 0);
    if (!self->check_auth(auth_str)) {
      mg_http_reply(c, 200, CORS_HDRS, "{\"auth\":false}");
      return;
    }

    if (mg_strcmp(uri, mg_str("/api/state")) == 0) {
      self->handle_get_state(c);
    } else if (mg_strcmp(uri, mg_str("/api/settings")) == 0) {
      if (hm->method.len > 0 && hm->method.buf[0] == 'P') {
        self->handle_post_action(c, "save_settings", std::string(hm->body.buf, hm->body.len).c_str());
      } else {
        self->handle_get_settings(c);
      }
    } else if (mg_strcmp(uri, mg_str("/api/enrolled")) == 0) {
      self->handle_get_enrolled(c);
    } else if (mg_strcmp(uri, mg_str("/api/notepads")) == 0) {
      self->handle_get_all_notepads(c);
    } else if (mg_strcmp(uri, mg_str("/api/init")) == 0) {
      self->handle_get_init(c);
    } else if (mg_strcmp(uri, mg_str("/api/enroll_status")) == 0) {
      self->handle_get_enroll_status(c);
    } else if (mg_strcmp(uri, mg_str("/api/enroll")) == 0) {
      self->handle_post_action(c, "enroll", std::string(hm->body.buf, hm->body.len).c_str());
    } else if (mg_strcmp(uri, mg_str("/api/verify")) == 0) {
      self->handle_post_action(c, "verify", "");
    } else if (mg_strcmp(uri, mg_str("/api/delete")) == 0) {
      self->handle_post_action(c, "delete", std::string(hm->body.buf, hm->body.len).c_str());
    } else if (mg_strcmp(uri, mg_str("/api/clear")) == 0) {
      self->handle_post_action(c, "clear", "");
    } else if (mg_strcmp(uri, mg_str("/api/batch_delete")) == 0) {
      self->handle_post_action(c, "batch_delete", std::string(hm->body.buf, hm->body.len).c_str());
    } else if (mg_strcmp(uri, mg_str("/api/cancel")) == 0) {
      self->handle_post_action(c, "cancel", "");
    } else if (mg_strcmp(uri, mg_str("/api/refresh")) == 0) {
      self->handle_post_action(c, "refresh", "");
    } else if (mg_strcmp(uri, mg_str("/api/sync")) == 0) {
      self->handle_post_action(c, "sync", "");
    } else if (mg_strcmp(uri, mg_str("/api/write_notepad")) == 0) {
      self->handle_post_action(c, "write_notepad", std::string(hm->body.buf, hm->body.len).c_str());
    } else if (mg_strcmp(uri, mg_str("/api/read_notepad")) == 0) {
      self->handle_post_action(c, "read_notepad", std::string(hm->body.buf, hm->body.len).c_str());
    } else {
      mg_http_reply(c, 404, "", "{\"error\":\"not found\"}");
    }
  }
}

void ZW111Component::start_mongoose_server() {
  mg_mgr_init(&mgr_);
  mgr_inited_ = true;
  char addr[32];
  snprintf(addr, sizeof(addr), "http://0.0.0.0:%d", web_port_);
  mg_http_listen(&mgr_, addr, mg_ev_handler, this);
  ESP_LOGI(TAG, "Mongoose HTTP on port %d", web_port_);
}

// ===== REST handlers =====

void ZW111Component::handle_get_state(struct mg_connection *c) {
  char buf[1200];
  const char *scr = info_.sensor_check_result.empty() ? "未检测" : info_.sensor_check_result.c_str();
  snprintf(buf, sizeof(buf),
    "{\"Product_SN\":\"%s\",\"Software_Ver\":\"%s\",\"Manufacturer\":\"%s\",\"Sensor_Name\":\"%s\",\"Device_Address\":\"%s\",\"LED_Type\":\"%s\",\"Sensor_Size\":\"%s\",\"Enroll_Count\":%.0f,\"Template_Size\":%.0f,\"Database_Size\":%.0f,\"Score_Level\":%.0f,\"Baud_Rate\":%.0f,\"Templates_Per_Finger\":%.0f,\"Stored_Count\":%.0f,\"Last_Result\":\"%s\",\"Sensor_Check_Result\":\"%s\"}",
    info_.product_sn.c_str(), info_.software_ver.c_str(), info_.manufacturer.c_str(), info_.sensor_name.c_str(),
    info_.device_addr.c_str(), info_.led_type.c_str(), info_.sensor_size.c_str(),
    info_.enroll_count, info_.template_size, info_.database_size, info_.score_level, info_.baud_rate, info_.templates_per_finger, info_.stored_count, info_.last_result.c_str(),
    scr);
  mg_http_reply(c, 200, "", "%s\n", buf);
}

void ZW111Component::handle_get_settings(struct mg_connection *c) {
  char buf[256];
  snprintf(buf, sizeof(buf),
    "{\"score_level\":%d,\"enroll_max\":%d,\"dup_block\":%d}",
    (int)info_.score_level, setting_enroll_max_, setting_dup_block_);
  mg_http_reply(c, 200, "", "%s\n", buf);
}

void ZW111Component::handle_get_enrolled(struct mg_connection *c) {
  if (!nvs_has_index_cache_) {
    load_index_table_from_nvs();
  }
  std::string json = "{\"enrolled\":[";
  for (int i = 0; i < 100; i++) {
    if (i > 0) json += ",";
    json += enrolled_[i] ? "true" : "false";
  }
  json += "]}";
  mg_http_reply(c, 200, "", "%s\n", json.c_str());
}

void ZW111Component::handle_get_all_notepads(struct mg_connection *c) {
  std::string json = "{\"notepads\":[";
  for (int i = 0; i < 100; i++) {
    if (notepad_cache_[i].empty()) {
      notepad_cache_[i] = read_notepad_nvs(i);
    }
    if (i > 0) json += ",";
    json += "\"";
    for (size_t j = 0; j < notepad_cache_[i].length(); j++) {
      char c = notepad_cache_[i][j];
      if (c == '"' || c == '\\') json += "\\";
      json += c;
    }
    json += "\"";
  }
  json += "]}";
  mg_http_reply(c, 200, "", "%s\n", json.c_str());
}

void ZW111Component::handle_get_init(struct mg_connection *c) {
  if (!nvs_has_index_cache_) {
    load_index_table_from_nvs();
  }

  load_info_nvs();
  load_sensor_check_nvs();

  std::string enrolled_json = "[";
  for (int i = 0; i < 100; i++) {
    if (i > 0) enrolled_json += ",";
    enrolled_json += enrolled_[i] ? "true" : "false";
  }
  enrolled_json += "]";

  std::string notepads_json = "[";
  for (int i = 0; i < 100; i++) {
    if (notepad_cache_[i].empty()) {
      notepad_cache_[i] = read_notepad_nvs(i);
    }
    if (i > 0) notepads_json += ",";
    notepads_json += "\"";
    for (size_t j = 0; j < notepad_cache_[i].length(); j++) {
      char cc = notepad_cache_[i][j];
      if (cc == '"' || cc == '\\') notepads_json += "\\";
      notepads_json += cc;
    }
    notepads_json += "\"";
  }
  notepads_json += "]";

  const char *scr = info_.sensor_check_result.empty() ? "未检测" : info_.sensor_check_result.c_str();
  char state_buf[1200];
  snprintf(state_buf, sizeof(state_buf),
    "{\"Product_SN\":\"%s\",\"Software_Ver\":\"%s\",\"Manufacturer\":\"%s\",\"Sensor_Name\":\"%s\",\"Device_Address\":\"%s\",\"LED_Type\":\"%s\",\"Sensor_Size\":\"%s\",\"Enroll_Count\":%.0f,\"Template_Size\":%.0f,\"Database_Size\":%.0f,\"Score_Level\":%.0f,\"Baud_Rate\":%.0f,\"Templates_Per_Finger\":%.0f,\"Stored_Count\":%.0f,\"Last_Result\":\"%s\",\"Sensor_Check_Result\":\"%s\"}",
    info_.product_sn.c_str(), info_.software_ver.c_str(), info_.manufacturer.c_str(), info_.sensor_name.c_str(),
    info_.device_addr.c_str(), info_.led_type.c_str(), info_.sensor_size.c_str(),
    info_.enroll_count, info_.template_size, info_.database_size, info_.score_level, info_.baud_rate, info_.templates_per_finger, info_.stored_count, info_.last_result.c_str(),
    scr);

  char settings_buf[256];
  snprintf(settings_buf, sizeof(settings_buf),
    "{\"score_level\":%d,\"enroll_max\":%d,\"dup_block\":%d}",
    (int)info_.score_level, setting_enroll_max_, setting_dup_block_);

  std::string resp = "{\"state\":";
  resp += state_buf;
  resp += ",\"enrolled\":";
  resp += enrolled_json;
  resp += ",\"notepads\":";
  resp += notepads_json;
  resp += ",\"settings\":";
  resp += settings_buf;
  resp += "}";
  mg_http_reply(c, 200, "", "%s\n", resp.c_str());
}

void ZW111Component::handle_get_enroll_status(struct mg_connection *c) {
  char buf[384];
  snprintf(buf, sizeof(buf),
    "{\"busy\":%s,\"phase\":%d,\"message\":\"%s\",\"confirm\":\"%s\",\"page_id\":%d,\"times\":%d,\"step\":%d,\"total\":%d}",
    enroll_busy_ ? "true" : "false", (int)enroll_phase_, enroll_message_, enroll_confirm_hex_,
    (int)enroll_page_id_, (int)enroll_times_, (int)enroll_step_, (int)enroll_total_);
  mg_http_reply(c, 200, "", "%s\n", buf);
}

void ZW111Component::handle_post_action(struct mg_connection *c, const char *action, const char *body) {
  int page_id = 0; const char *pid = strstr(body, "\"page\":"); if (pid) page_id = atoi(pid + 7);
  int times = 3; const char *pt = strstr(body, "\"times\":"); if (pt) times = atoi(pt + 8);
  int count = 1; const char *pc = strstr(body, "\"count\":"); if (pc) count = atoi(pc + 8);

  if (strcmp(action, "enroll") == 0) {
    if (enroll_busy_) { mg_http_reply(c, 200, "", "{\"ok\":false,\"msg\":\"Enroll busy\"}\n"); return; }
    if (page_id < 0 || page_id >= 100) { mg_http_reply(c, 200, "", "{\"ok\":false,\"msg\":\"Invalid ID\"}\n"); return; }
    if (times < 1) times = (int)setting_enroll_max_;
    enroll_page_id_ = page_id;
    enroll_times_ = times;
    enroll_cancel_ = false;
    enroll_busy_ = true;
    xTaskCreate(enroll_task_entry, "zw111_en", 4096, this, 5, nullptr);
    mg_http_reply(c, 200, "", "{\"ok\":true,\"msg\":\"Enroll started\"}\n");
    return;
  } else if (strcmp(action, "cancel") == 0) {
    if (enroll_busy_) {
      enroll_cancel_ = true;
      enroll_phase_ = ENROLL_CANCELED;
      snprintf(enroll_message_, sizeof(enroll_message_), "录入已取消");
      mg_http_reply(c, 200, "", "{\"ok\":true,\"msg\":\"Cancel sent\"}\n");
    } else {
      mg_http_reply(c, 200, "", "{\"ok\":true,\"msg\":\"Nothing to cancel\"}\n");
    }
    return;
  } else if (strcmp(action, "verify") == 0) {
    uint8_t p[5] = {0x00,0xFF,0xFF,0x00,0x00}; send_command(CMD_AUTO_IDENTIFY, p, 5);
  } else if (strcmp(action, "delete") == 0) {
    uint8_t p[4] = {(uint8_t)(page_id>>8),(uint8_t)(page_id&0xFF),(uint8_t)(count>>8),(uint8_t)(count&0xFF)};
    flush_input(); send_command(CMD_DELETE_CHAR, p, 4);
    uint8_t confirm = 0xFF;
    if (read_response(&confirm) && confirm == CONFIRM_OK) {
      for (int i = page_id; i < page_id + count && i < 100; i++) {
        notepad_cache_[i] = ""; notepad_dirty_[i] = true;
      }
      sync_index_table_from_uart();
      if (connection_status_) connection_status_->publish_state(true);
    }
  } else if (strcmp(action, "clear") == 0) {
    flush_input(); send_command(CMD_EMPTY);
    uint8_t confirm = 0xFF; read_response(&confirm);
    for (int i = 0; i < 100; i++) { notepad_cache_[i] = ""; notepad_dirty_[i] = true; }
    sync_index_table_from_uart();
    if (connection_status_) connection_status_->publish_state(true);
  } else if (strcmp(action, "refresh") == 0) {
    flush_input(); delay(20); do_read_para(); flush_input(); delay(20); do_read_count(); do_check_sensor();
  } else if (strcmp(action, "sync") == 0) {
    sync_index_table_from_uart();
    if (connection_status_) connection_status_->publish_state(true);
    mg_http_reply(c, 200, "", "{\"ok\":true,\"msg\":\"数据同步完成\"}\n");
    return;
  } else if (strcmp(action, "write_notepad") == 0) {
    std::string content;
    const char *nc = strstr(body, "\"content\":\"");
    if (nc) { nc += 11; while (*nc && *nc != '"') content += *nc++; }
    if (page_id >= 0 && page_id < 100) {
      notepad_cache_[page_id] = content;
      notepad_dirty_[page_id] = true;
      write_notepad_nvs(page_id, content);
    }
  } else if (strcmp(action, "read_notepad") == 0) {
    std::string content;
    if (page_id >= 0 && page_id < 100) {
      if (notepad_cache_[page_id].empty()) {
        notepad_cache_[page_id] = read_notepad_nvs(page_id);
      }
      content = notepad_cache_[page_id];
    }
    char jbuf[160]; snprintf(jbuf, sizeof(jbuf), "{\"ok\":true,\"content\":\"%s\"}", content.c_str());
    mg_http_reply(c, 200, "", "%s\n", jbuf);
    return;
  } else if (strcmp(action, "batch_delete") == 0) {
    int start_id = 0; const char *psi = strstr(body, "\"start_id\":"); if (psi) start_id = atoi(psi + 11);
    int end_id = 0; const char *pei = strstr(body, "\"end_id\":"); if (pei) end_id = atoi(pei + 9);
    if (start_id > end_id) { int tmp = start_id; start_id = end_id; end_id = tmp; }
    if (start_id < 0) start_id = 0; if (end_id >= 100) end_id = 99;
    int rcount = end_id - start_id + 1;
    if (rcount < 1) rcount = 1;
    uint8_t bp[4] = {(uint8_t)(start_id>>8),(uint8_t)(start_id&0xFF),(uint8_t)(rcount>>8),(uint8_t)(rcount&0xFF)};
    flush_input(); send_command(CMD_DELETE_CHAR, bp, 4);
    uint8_t bconfirm = 0xFF;
    if (read_response(&bconfirm) && bconfirm == CONFIRM_OK) {
      for (int i = start_id; i <= end_id; i++) {
        notepad_cache_[i] = ""; notepad_dirty_[i] = true;
      }
      sync_index_table_from_uart();
      if (connection_status_) connection_status_->publish_state(true);
      char jbuf[128]; snprintf(jbuf, sizeof(jbuf), "{\"ok\":true,\"msg\":\"Deleted ID %d-%d (%d)\"}\n", start_id, end_id, rcount);
      mg_http_reply(c, 200, "", jbuf);
    } else {
      mg_http_reply(c, 200, "", "{\"ok\":false,\"msg\":\"Batch delete failed\"}\n");
    }
    return;
  } else if (strcmp(action, "save_settings") == 0) {
    const char *psl = strstr(body, "\"score_level\":");
    if (psl) { int sl = atoi(psl + 14); if (sl >= 1 && sl <= 5) apply_score_level_to_module((uint8_t)sl); }
    const char *pem = strstr(body, "\"enroll_max\":");
    if (pem) { int em = atoi(pem + 13); if (em >= 1 && em <= 8) { setting_enroll_max_ = (uint8_t)em; save_setting_to_nvs("enroll_max", setting_enroll_max_); } }
    const char *pdb = strstr(body, "\"dup_block\":");
    if (pdb) { int db = atoi(pdb + 12); setting_dup_block_ = (uint8_t)(db ? 1 : 0); save_setting_to_nvs("dup_block", setting_dup_block_); }
  }
  mg_http_reply(c, 200, "", "{\"ok\":true}\n");
}

// ===== Enroll =====

void ZW111Component::do_enroll(int page_id, int times) {
  if (times < 1) times = (int)setting_enroll_max_;
  enroll_total_ = times; enroll_step_ = 0;
  bool final_ok = false;
  uint8_t confirm = 0xFF;
  uint32_t step_timeout_ms = 15000;

  flush_input(); delay(150); flush_input();

  if (setting_dup_block_) {
    ESP_LOGI(TAG, "[Enroll] Dup block enabled, performing pre-verify...");
    enroll_phase_ = ENROLL_SETUP;
    snprintf(enroll_message_, sizeof(enroll_message_), "正在检查指纹是否重复...");
    int score_level = (int)info_.score_level;
    if (score_level < 1 || score_level > 5) score_level = 3;
    uint8_t id_params[5] = {(uint8_t)score_level, 0xFF, 0xFF, 0x02, 0x00};
    led_blue_blink(); flush_input();
    send_command(CMD_AUTO_IDENTIFY, id_params, 5);
    uint32_t vstart = millis();
    bool verified = false, is_match = false;
    while (millis() - vstart < 15000) {
      if (enroll_cancel_) { led_all_off(); enroll_phase_ = ENROLL_CANCELED; snprintf(enroll_message_, sizeof(enroll_message_), "录入已取消"); goto cleanup; }
      if (this->available() < 9) { delay(10); continue; }
      uint8_t b1 = this->read(); if (b1 != PACKET_HEADER_HI) continue;
      if (this->available() < 1) { delay(1); continue; }
      uint8_t b2 = this->read(); if (b2 != PACKET_HEADER_LO) continue;
      uint8_t addr[4]; if (!this->read_array(addr, 4)) continue;
      uint8_t pkt_type; if (!this->read_array(&pkt_type, 1)) continue;
      uint8_t len_buf[2]; if (!this->read_array(len_buf, 2)) continue;
      uint16_t pkt_len = ((uint16_t)len_buf[0] << 8) | len_buf[1];
      if (pkt_len < 2 || pkt_len > 32) continue;
      uint8_t payload[32] = {0}; if (!this->read_array(payload, pkt_len)) continue;
      uint16_t plen = pkt_len - 2;
      uint8_t cs[3] = {pkt_type, len_buf[0], len_buf[1]};
      uint16_t csum = calc_checksum(cs, 3) + calc_checksum(payload, plen);
      uint16_t rsum = ((uint16_t)payload[plen] << 8) | payload[plen + 1];
      if (csum != rsum) continue;
      uint8_t vc = payload[0]; uint8_t vp1 = (plen > 1) ? payload[1] : 0;
      if (vc == CONFIRM_OK && vp1 == 0x05) { is_match = true; verified = true; break; }
      else if (vc == 0x09 || vc == 0x24 || vc != CONFIRM_OK) { verified = true; break; }
      delay(5);
    }
    if (!verified) {
      led_red_blink(3); delay(1000); led_all_off();
      enroll_phase_ = ENROLL_FAILED; snprintf(enroll_message_, sizeof(enroll_message_), "超时, 录入取消");
      snprintf(enroll_confirm_hex_, sizeof(enroll_confirm_hex_), "Timeout"); goto cleanup;
    } else if (is_match) {
      led_red_blink(3); delay(1000); led_all_off();
      enroll_phase_ = ENROLL_FAILED; snprintf(enroll_message_, sizeof(enroll_message_), "重复手指! 请勿重复录入");
      snprintf(enroll_confirm_hex_, sizeof(enroll_confirm_hex_), "Dup:MATCH"); goto cleanup;
    } else {
      led_green_steady();
      enroll_phase_ = ENROLL_LIFT_FINGER;
      snprintf(enroll_message_, sizeof(enroll_message_), "检查通过, 请抬起手指");
      uint32_t lift_start = millis();
      while (true) {
        if (enroll_cancel_) { led_all_off(); enroll_phase_ = ENROLL_CANCELED; snprintf(enroll_message_, sizeof(enroll_message_), "录入已取消"); goto cleanup; }
        if (millis() - lift_start > 15000) { enroll_phase_ = ENROLL_FAILED; led_red_blink(3); goto cleanup; }
        flush_input(); if (!send_command(0x01)) { delay(200); continue; }
        if (!read_response(&confirm, nullptr, 0, 3000)) { delay(200); continue; }
        if (confirm == 0x02) { led_all_off(); break; }
        delay(200);
      }
    }
  }

  uint8_t store_params[3];
  store_params[0] = 0x01;
  store_params[1] = (uint8_t)(page_id >> 8);
  store_params[2] = (uint8_t)(page_id & 0xFF);
  flush_input();

  for (int n = 1; n <= times; n++) {
    if (enroll_cancel_) { enroll_phase_ = ENROLL_CANCELED; snprintf(enroll_message_, sizeof(enroll_message_), "录入已取消"); goto cleanup; }
    enroll_step_ = n;
    snprintf(enroll_confirm_hex_, sizeof(enroll_confirm_hex_), "");
    while (true) {
      if (enroll_cancel_) { led_all_off(); enroll_phase_ = ENROLL_CANCELED; snprintf(enroll_message_, sizeof(enroll_message_), "录入已取消"); goto cleanup; }
      enroll_phase_ = ENROLL_WAIT_FINGER;
      snprintf(enroll_message_, sizeof(enroll_message_), "第 %d/%d 次: 请按压手指", n, times);
      led_blue_blink(); flush_input();
      if (!send_command(0x29)) { enroll_phase_ = ENROLL_FAILED; goto cleanup; }
      if (!read_response(&confirm, nullptr, 0, step_timeout_ms)) { enroll_phase_ = ENROLL_FAILED; goto cleanup; }
      if (confirm == CONFIRM_OK) { led_all_off(); led_green_steady(); break; }
      if (confirm != 0x02) {
        led_red_blink(3); snprintf(enroll_confirm_hex_, sizeof(enroll_confirm_hex_), "GetImage:0x%02X", confirm);
        enroll_phase_ = ENROLL_FAILED; goto cleanup;
      }
      delay(200);
    }
    enroll_phase_ = ENROLL_CAPTURED;
    snprintf(enroll_message_, sizeof(enroll_message_), "第 %d/%d 次: 图像采集成功, 生成特征中...", n, times);
    snprintf(enroll_confirm_hex_, sizeof(enroll_confirm_hex_), "GetImg:OK");
    uint8_t gen_params[1] = {(uint8_t)n};
    flush_input(); if (!send_command(0x02, gen_params, 1)) { enroll_phase_ = ENROLL_FAILED; goto cleanup; }
    if (!read_response(&confirm, nullptr, 0, step_timeout_ms) || confirm != CONFIRM_OK) {
      snprintf(enroll_confirm_hex_, sizeof(enroll_confirm_hex_), "GenChar:0x%02X", confirm);
      enroll_phase_ = ENROLL_FAILED; goto cleanup;
    }
    enroll_phase_ = ENROLL_GEN_DONE;
    snprintf(enroll_message_, sizeof(enroll_message_), "第 %d/%d 次: 特征生成完毕", n, times);
    snprintf(enroll_confirm_hex_, sizeof(enroll_confirm_hex_), "GenCh:OK");
    if (n < times) {
      enroll_phase_ = ENROLL_LIFT_FINGER;
      snprintf(enroll_message_, sizeof(enroll_message_), "第 %d/%d 次完成, 请抬起手指", n, times);
      uint32_t lift_start = millis();
      while (true) {
        if (enroll_cancel_) { led_all_off(); enroll_phase_ = ENROLL_CANCELED; snprintf(enroll_message_, sizeof(enroll_message_), "录入已取消"); goto cleanup; }
        if (millis() - lift_start > step_timeout_ms) { enroll_phase_ = ENROLL_FAILED; goto cleanup; }
        flush_input(); if (!send_command(0x01)) { delay(200); continue; }
        if (!read_response(&confirm, nullptr, 0, 3000)) { delay(200); continue; }
        if (confirm == 0x02) break;
        delay(200);
      }
    }
  }

  if (enroll_cancel_) { led_all_off(); enroll_phase_ = ENROLL_CANCELED; snprintf(enroll_message_, sizeof(enroll_message_), "录入已取消"); goto cleanup; }

  enroll_phase_ = ENROLL_MERGE;
  flush_input(); if (!send_command(0x05)) { enroll_phase_ = ENROLL_FAILED; goto cleanup; }
  if (!read_response(&confirm, nullptr, 0, step_timeout_ms) || confirm != CONFIRM_OK) {
    snprintf(enroll_confirm_hex_, sizeof(enroll_confirm_hex_), "Merge:0x%02X", confirm);
    enroll_phase_ = ENROLL_FAILED; goto cleanup;
  }
  enroll_phase_ = ENROLL_DUP_CHECK;
  if (setting_dup_block_ && info_.stored_count > 0) {
    uint16_t db_size = (uint16_t)info_.database_size; if (db_size == 0) db_size = 100;
    uint8_t search_params[5] = {0x01, (uint8_t)(0>>8), (uint8_t)(0&0xFF), (uint8_t)(db_size>>8), (uint8_t)(db_size&0xFF)};
    flush_input();
    if (send_command(0x04, search_params, 5)) {
      uint8_t sc = 0xFF; uint8_t sd[4] = {0};
      if (read_response(&sc, sd, 4, step_timeout_ms) && sc == CONFIRM_OK) {
        enroll_phase_ = ENROLL_FAILED; goto cleanup;
      }
    }
  }

  flush_input(); if (!send_command(0x06, store_params, 3)) { enroll_phase_ = ENROLL_FAILED; goto cleanup; }
  if (!read_response(&confirm, nullptr, 0, step_timeout_ms) || confirm != CONFIRM_OK) {
    enroll_phase_ = ENROLL_FAILED; goto cleanup;
  }
  enroll_phase_ = ENROLL_SUCCESS;
  final_ok = true;
  snprintf(enroll_message_, sizeof(enroll_message_), "ID:%d 录入成功!", page_id);
  snprintf(enroll_confirm_hex_, sizeof(enroll_confirm_hex_), "Store:OK");

cleanup:
  if (enroll_cancel_ || (!final_ok && enroll_phase_ != ENROLL_SUCCESS)) {
    flush_input(); send_command(CMD_CANCEL);
    { uint8_t junk; read_response(&junk, nullptr, 0, 300); }
    flush_input(); delay(80);
  }
  if (final_ok) { delay(1500); delay(3000); led_all_off(); }
  else if (enroll_phase_ == ENROLL_FAILED || enroll_phase_ == ENROLL_CANCELED) { led_all_off(); }
  flush_input();
  if (final_ok) {
    sync_index_table_from_uart();
    if (connection_status_) connection_status_->publish_state(true);
    info_.last_result = enroll_message_;
  }
  enroll_busy_ = false; enroll_cancel_ = false;
  if (!final_ok && enroll_phase_ != ENROLL_CANCELED) enroll_phase_ = ENROLL_FAILED;
  flush_input(); delay(50); flush_input();
}

void ZW111Component::enroll_task_entry(void *arg) {
  ZW111Component *self = static_cast<ZW111Component*>(arg);
  self->do_enroll(self->enroll_page_id_, self->enroll_times_);
  vTaskDelete(nullptr);
}

// ===== Identify =====

void ZW111Component::do_identify() {
  int score_level = (int)info_.score_level;
  if (score_level < 1 || score_level > 5) score_level = 3;
  info_.last_result = "";
  ESP_LOGI(TAG, "[Identify] Starting with score_level=%d", score_level);
  led_blue_blink();
  uint8_t id_params[5] = {(uint8_t)score_level, 0xFF, 0xFF, 0x00, 0x00};
  flush_input(); send_command(CMD_AUTO_IDENTIFY, id_params, 5);
  bool got_result = false, identify_success = false;
  uint32_t start = millis();
  while (millis() - start < 5000) {
    if (millis() - start >= 5000) break;
    if (this->available() < 9) { delay(10); continue; }
    uint8_t b1 = this->read(); if (b1 != PACKET_HEADER_HI) continue;
    if (this->available() < 1) { delay(1); continue; }
    uint8_t b2 = this->read(); if (b2 != PACKET_HEADER_LO) continue;
    // 替代 read_array: 逐字节读取, 每字节最多等 50ms, 超时则 continue 重新解析
    uint8_t addr[4]; bool addr_ok = true;
    for (int _i = 0; _i < 4 && addr_ok; _i++) {
      uint32_t _t = millis() + 50; addr_ok = false;
      while (millis() - start < 5000) { if (this->available()) { addr[_i] = this->read(); addr_ok = true; break; } if (millis() > _t) break; delay(1); }
    }
    if (!addr_ok) continue;
    uint8_t pkt_type = 0; { uint32_t _t = millis() + 50; bool _ok = false; while (millis() - start < 5000) { if (this->available()) { pkt_type = this->read(); _ok = true; break; } if (millis() > _t) break; delay(1); } if (!_ok) continue; }
    uint8_t len_buf[2] = {0, 0}; for (int _i = 0; _i < 2; _i++) { uint32_t _t = millis() + 50; bool _ok = false; while (millis() - start < 5000) { if (this->available()) { len_buf[_i] = this->read(); _ok = true; break; } if (millis() > _t) break; delay(1); } if (!_ok) { len_buf[0] = 0; break; } }
    uint16_t pkt_len = ((uint16_t)len_buf[0] << 8) | len_buf[1];
    if (pkt_len < 2 || pkt_len > 32) continue;
    uint8_t payload[32] = {0}; bool payload_ok = true;
    for (uint16_t _i = 0; _i < pkt_len && payload_ok; _i++) {
      uint32_t _t = millis() + 50; payload_ok = false;
      while (millis() - start < 5000) { if (this->available()) { payload[_i] = this->read(); payload_ok = true; break; } if (millis() > _t) break; delay(1); }
    }
    if (!payload_ok) continue;
    uint16_t plen = pkt_len - 2;
    uint8_t cs[3] = {pkt_type, len_buf[0], len_buf[1]};
    uint16_t csum = calc_checksum(cs, 3) + calc_checksum(payload, plen);
    uint16_t rsum = ((uint16_t)payload[plen] << 8) | payload[plen + 1];
    if (csum != rsum) continue;
    uint8_t confirm = payload[0]; uint8_t param1 = (plen > 1) ? payload[1] : 0;
    if (confirm == CONFIRM_OK && param1 == 0x05) {
      uint16_t match_id = ((uint16_t)payload[2] << 8) | payload[3];
      uint16_t score = ((uint16_t)payload[4] << 8) | payload[5];
      if (notepad_cache_[match_id].empty()) {
        notepad_cache_[match_id] = read_notepad_nvs(match_id);
      }
      std::string note = notepad_cache_[match_id];
      ESP_LOGI(TAG, "[Identify] MATCH! pageID=%d score=%d note='%s'", match_id, score, note.c_str());
      if (!note.empty() && note.length() > 0) {
        if (fp_identify_sensor_) fp_identify_sensor_->publish_state(note);
      } else {
        char idbuf[8]; snprintf(idbuf, sizeof(idbuf), "%d", match_id);
        if (fp_identify_sensor_) fp_identify_sensor_->publish_state(idbuf);
      }
      led_all_off(); led_green_steady();
      info_.last_result = "OK"; got_result = true; identify_success = true; break;
    } else if (confirm == 0x09) {
      ESP_LOGI(TAG, "[Identify] No Match in library");
      info_.last_result = "No Match";
      if (fp_identify_sensor_) fp_identify_sensor_->publish_state("No Match");
      got_result = true; break;
    } else if (confirm == 0x24) {
      ESP_LOGI(TAG, "[Identify] Library is empty");
      info_.last_result = "Library Empty";
      if (fp_identify_sensor_) fp_identify_sensor_->publish_state("Lib Empty");
      got_result = true; break;
    } else if (confirm != CONFIRM_OK) {
      ESP_LOGW(TAG, "[Identify] Failed confirm=0x%02X", confirm);
      info_.last_result = "Verify Failed";
      if (fp_identify_sensor_) fp_identify_sensor_->publish_state("Error");
      got_result = true; break;
    }
    delay(5);
  }
  if (!got_result) {
    ESP_LOGW(TAG, "[Identify] Timeout (no result in 5s)");
    info_.last_result = "No Match";
    if (fp_identify_sensor_) fp_identify_sensor_->publish_state("No Match");
  }
  if (identify_success) { delay(2000); led_all_off(); }
  else { led_all_off(); led_control(0x03, 0x04, 0x00, 0x00); delay(2000); led_all_off(); }
  if (fp_identify_sensor_) fp_identify_sensor_->publish_state("-");
  identify_busy_ = false; flush_input();
  ESP_LOGI(TAG, "[Identify] Done, result=%s", info_.last_result.c_str());
}

void ZW111Component::identify_task_entry(void *arg) {
  ZW111Component *self = static_cast<ZW111Component*>(arg);
  self->do_identify();
  vTaskDelete(nullptr);
}

void ZW111Component::trigger_identify() {
  if (!initialized_ && power_ctl_pin_ != nullptr) {
    power_on_and_init();
    if (!initialized_) return;
  }
  if (!initialized_ || enroll_busy_ || identify_busy_) return;
  flush_input();
  identify_busy_ = true;
  xTaskCreate(identify_task_entry, "zw111_id", 4096, this, 5, nullptr);
}

void ZW111Component::trigger_sleep() {
  if (!initialized_) return;
  ESP_LOGI(TAG, "Entering sleep mode...");
  flush_input(); send_command(CMD_SLEEP);
  uint8_t confirm = 0xFF;
  if (read_response(&confirm)) {
    if (confirm == CONFIRM_OK) ESP_LOGI(TAG, "Sleep command sent successfully");
    else ESP_LOGW(TAG, "Sleep command failed, confirm=0x%02X", confirm);
  } else {
    ESP_LOGW(TAG, "Sleep command: no response");
  }
  initialized_ = false;
  s_rtc.was_sleeping = true;
  if (s_rtc.valid) {
    s_rtc.touch_pre_state = (touch_sense_pin_ != nullptr) ? touch_sense_pin_->digital_read() : false;
    memset(s_rtc.enrolled_bitmap, 0, sizeof(s_rtc.enrolled_bitmap));
    for (int i = 0; i < 100; i++) {
      if (enrolled_[i]) s_rtc.enrolled_bitmap[i / 8] |= (1 << (i % 8));
    }
    s_rtc.enroll_max = setting_enroll_max_;
    s_rtc.dup_block = setting_dup_block_;
    if (!info_.sensor_check_result.empty()) {
      snprintf(s_rtc.sensor_check_result, sizeof(s_rtc.sensor_check_result), "%s", info_.sensor_check_result.c_str());
    }
  }
  s_rtc.valid = true;
  if (power_ctl_pin_ != nullptr) {
    delay(50);
    power_ctl_pin_->digital_write(false);
  }
  if (connection_status_) connection_status_->publish_state(false);
  if (sensor_check_status_) sensor_check_status_->publish_state(true);
}

void ZW111SleepButton::press_action() { this->get_parent()->trigger_sleep(); }
void ZW111Button::press_action() { this->get_parent()->trigger_identify(); }

}  // namespace zw111
}  // namespace esphome
