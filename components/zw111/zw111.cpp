#include "zw111.h"
#include "esphome/core/log.h"
#include "zw111_web.h"
#include <esp_http_server.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <map>
#include <nvs_flash.h>
#include <nvs.h>

#ifdef USE_WIFI
#include "esphome/components/wifi/wifi_component.h"
#endif

namespace esphome {
namespace zw111 {

static const char *const TAG = "zw111";
static std::map<httpd_handle_t, ZW111Component*> g_instances;

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
  ESP_LOGI(TAG, "Settings loaded: enroll_max=%d dup_block=%d",
           setting_enroll_max_, setting_dup_block_);
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

// ===== LED 控制 (PS_ControlBLN 多功能三色灯) =====

void ZW111Component::led_control(uint8_t func, uint8_t color, uint8_t cycles, uint8_t time_10ms) {
  uint8_t params[7] = {func, color, color, 0x00, cycles, time_10ms};
  flush_input();
  send_command(CMD_CONTROL_BLN, params, 6);
  uint8_t c;
  read_response(&c);
  delay(30);
}

void ZW111Component::led_all_off() { led_control(0x04, 0x00, 0x00, 0x00); }
void ZW111Component::led_green_steady() { led_control(0x03, 0x02, 0x00, 0x00); }
void ZW111Component::led_red_blink(uint8_t times) { led_control(0x02, 0x04, times, 3); }
void ZW111Component::led_blue_blink() { led_control(0x01, 0x01, 0x00, 10); }

// ===== 诊断读取 =====

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
    ESP_LOGI(TAG, "IndexTable page0 loaded, enrolled check done");
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

// ===== 记事本持久化 =====

void ZW111Component::write_notepad_flash(int page, const std::string &content) {
  uint8_t pw[34]; pw[0] = (uint8_t)page; memset(pw + 1, 0, 33);
  uint16_t clen = content.length() > 32 ? 32 : content.length();
  memcpy(pw + 1, content.c_str(), clen);
  send_command(CMD_WRITE_NOTEPAD, pw, 33);
  uint8_t c; read_response(&c);
}

std::string ZW111Component::read_notepad_flash(int page) {
  uint8_t pn[1] = {(uint8_t)page};
  if (send_command(CMD_READ_NOTEPAD, pn, 1)) {
    uint8_t c; uint8_t nd[32] = {0};
    if (read_response(&c, nd, 32) && c == CONFIRM_OK) return extract_ascii_string(nd, 32);
  }
  return "";
}

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
  for (int i = 0; i < 16; i++) {
    notepad_cache_[i] = read_notepad_flash(i);
    if (i % 4 == 3) { flush_input(); delay(15); }
  }
  for (int i = 16; i < 100; i++) {
    notepad_cache_[i] = read_notepad_nvs(i);
  }
  ESP_LOGI(TAG, "Notepad cache loaded (100 entries)");
}

void ZW111Component::flush_dirty_notepad() {
  for (int i = 0; i < 100; i++) {
    if (notepad_dirty_[i]) {
      notepad_dirty_[i] = false;
      if (i < 16) {
        ESP_LOGI(TAG, "Notepad save: id=%d content='%s' target=ZW111_FLASH(page%d)", i, notepad_cache_[i].c_str(), i);
        write_notepad_flash(i, notepad_cache_[i]);
        flush_input(); delay(15);
      } else {
        ESP_LOGI(TAG, "Notepad save: id=%d content='%s' target=NVS(n%d)", i, notepad_cache_[i].c_str(), i);
        write_notepad_nvs(i, notepad_cache_[i]);
      }
      return;
    }
  }
}

// ===== 生命周期 =====

void ZW111Component::setup() {
  ESP_LOGI(TAG, "ZW111 setup start, port=%d, nvs_prefix=%s",
           web_port_, nvs_prefix_.c_str());
  flush_input();
  uint32_t t = millis(); bool got = false;
  while (millis() - t < 200) { if (this->available() && this->read() == 0x55) { got = true; break; } delay(5); }
  if (!got) { delay(80); }
  bool ok = false;
  for (int i = 0; i < 2; i++) { flush_input(); delay(50); if (do_handshake()) { ok = true; break; } delay(200); }
  if (!ok) { if (connection_status_) connection_status_->publish_state(false); mark_failed(); return; }
  // 握手成功仅表示基本通信正常, 精确连接状态由后续 ReadIndexTable 判定
  flush_input(); delay(30); do_read_para(); flush_input(); delay(30); do_read_count();
  load_all_notepads();
  load_settings_from_nvs();
  // 用 NVS 中持久化的注册次数覆盖模组参数表中的默认值 (PS_WriteEMPara 不掉电保存)
  info_.enroll_count = setting_enroll_max_;
  if (info_.score_level >= 1 && info_.score_level <= 5) {
    apply_score_level_to_module((uint8_t)info_.score_level);
  }
  // 使用 ReadIndexTable 可靠性来判断连接状态 (比握手更严谨)
  if (do_read_index_table()) {
    if (connection_status_) connection_status_->publish_state(true);
  } else {
    if (connection_status_) connection_status_->publish_state(false);
  }
  // 校验传感器并发布状态 (device_class="problem": true=异常, false=正常)
  if (do_check_sensor()) {
    if (sensor_check_status_) sensor_check_status_->publish_state(false);
  } else {
    if (sensor_check_status_) sensor_check_status_->publish_state(true);
  }
  if (fp_identify_sensor_) fp_identify_sensor_->publish_state("-");

  // 设置 LED 为手动模式 + 关闭所有灯
  {
    uint8_t mode_params[1] = {0x00};  // 手动模式
    flush_input();
    send_command(CMD_BLN_MODE_SW, mode_params, 1);
    uint8_t c;
    read_response(&c);
    delay(50);
  }
  led_all_off();

  // 初始化触摸检测: 通过执行一次短时 AutoIdentify 来激活模组的受控触摸模式,
  // 随后立即 Cancel 确保模块回到空闲待机状态.
  // 此操作与验证完成后 touch_out 自动恢复低电平的逻辑一致。
  {
    flush_input();
    uint8_t id_params[5] = {(uint8_t)info_.score_level, 0xFF, 0xFF, 0x00, 0x00};
    send_command(CMD_AUTO_IDENTIFY, id_params, 5);
    delay(80);
    flush_input();
    send_command(CMD_CANCEL);
    uint8_t dummy;
    read_response(&dummy, nullptr, 0, 500);
    flush_input();
    delay(30);
    ESP_LOGD(TAG, "Touch detection init done");
  }

  initialized_ = true;
  ESP_LOGI(TAG, "ZW111 ready");
}

void ZW111Component::loop() {
  if (!initialized_) return;
  flush_dirty_notepad();
  if (!web_started_ && web_port_ > 0) {
#ifdef USE_WIFI
    if (wifi::global_wifi_component->is_connected())
#endif
    { web_started_ = true; start_web_server(); }
  }
}

void ZW111Component::dump_config() { ESP_LOGCONFIG(TAG, "ZW111 Init: %s Port: %d", initialized_?"YES":"NO", web_port_); }

// ===== HTTP Server =====

static bool http_auth_check(httpd_req *req, ZW111Component *self) {
  if (self->web_user_.empty()) return true;
  size_t alen = httpd_req_get_hdr_value_len(req, "X-Auth-Credentials");
  if (alen == 0) return false;
  char abuf[alen + 1]; httpd_req_get_hdr_value_str(req, "X-Auth-Credentials", abuf, sizeof(abuf));
  std::string creds(abuf);
  std::string exp = self->web_user_ + ":" + self->web_pass_;
  char enc[128]; size_t elen = 0;
  const char *b64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  for (size_t i = 0; i < exp.size(); i += 3) {
    uint32_t bits = ((uint8_t)exp[i]) << 16;
    if (i+1 < exp.size()) bits |= ((uint8_t)exp[i+1]) << 8;
    if (i+2 < exp.size()) bits |= (uint8_t)exp[i+2];
    enc[elen++] = b64[(bits>>18)&0x3F]; enc[elen++] = b64[(bits>>12)&0x3F];
    enc[elen++] = (i+1<exp.size()) ? b64[(bits>>6)&0x3F] : '=';
    enc[elen++] = (i+2<exp.size()) ? b64[bits&0x3F] : '=';
  }
  enc[elen] = '\0';
  return creds == std::string(enc);
}

static void send_json(httpd_req *req, const char *json) { httpd_resp_set_type(req, "application/json"); httpd_resp_send(req, json, strlen(json)); }

static bool uri_match_all(const char *, const char *, size_t) { return true; }

static esp_err_t http_options_handler(httpd_req *req) {
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type, Authorization");
  httpd_resp_send(req, nullptr, 0); return ESP_OK;
}

static esp_err_t http_get_handler(httpd_req *req) {
  auto it = g_instances.find(req->handle);
  ZW111Component *self = (it != g_instances.end()) ? it->second : nullptr;
  if (!self) { httpd_resp_send_500(req); return ESP_FAIL; }
  std::string uri(req->uri);
  if (uri == "/" || uri == "/index.html") { httpd_resp_set_type(req, "text/html"); httpd_resp_send(req, ZW111_HTML, strlen(ZW111_HTML)); return ESP_OK; }
  if (!http_auth_check(req, self)) { send_json((httpd_req*)req, "{\"auth\":false}"); return ESP_OK; }
  if (uri == "/api/state") { self->handle_get_state(req); return ESP_OK; }
  if (uri == "/api/settings") { self->handle_get_settings(req); return ESP_OK; }
  if (uri == "/api/enrolled") { self->handle_get_enrolled(req); return ESP_OK; }
  if (uri == "/api/enroll_status") { self->handle_get_enroll_status(req); return ESP_OK; }
  httpd_resp_send_404(req); return ESP_OK;
}

static esp_err_t http_post_handler(httpd_req *req) {
  auto it = g_instances.find(req->handle);
  ZW111Component *self = (it != g_instances.end()) ? it->second : nullptr;
  if (!self) { httpd_resp_send_500(req); return ESP_FAIL; }
  if (!http_auth_check(req, self)) { send_json((httpd_req*)req, "{\"auth\":false}"); return ESP_OK; }
  char body[512] = {0}; int r = req->content_len; if (r > 511) r = 511;
  if (httpd_req_recv(req, body, r) <= 0) { httpd_resp_send_500(req); return ESP_FAIL; } body[r] = '\0';
  std::string uri(req->uri);
  if (uri == "/api/enroll") self->handle_post_action(req, "enroll", body);
  else if (uri == "/api/verify") self->handle_post_action(req, "verify", body);
  else if (uri == "/api/delete") self->handle_post_action(req, "delete", body);
  else if (uri == "/api/clear") self->handle_post_action(req, "clear", body);
  else if (uri == "/api/batch_delete") self->handle_post_action(req, "batch_delete", body);
  else if (uri == "/api/cancel") self->handle_post_action(req, "cancel", body);
  else if (uri == "/api/refresh") self->handle_post_action(req, "refresh", body);
  else if (uri == "/api/write_notepad") self->handle_post_action(req, "write_notepad", body);
  else if (uri == "/api/read_notepad") self->handle_post_action(req, "read_notepad", body);
  else if (uri == "/api/settings") self->handle_post_action(req, "save_settings", body);
  else httpd_resp_send_404(req);
  return ESP_OK;
}

void ZW111Component::start_web_server() {
  httpd_handle_t hd = nullptr;
  httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
  cfg.server_port = web_port_; cfg.ctrl_port = web_port_ + 1;
  cfg.max_open_sockets = 10; cfg.lru_purge_enable = 1; cfg.task_priority = tskIDLE_PRIORITY + 1; cfg.stack_size = 8192;
  cfg.uri_match_fn = uri_match_all;
  if (httpd_start(&hd, &cfg) != ESP_OK) { ESP_LOGE(TAG, "HTTP fail port %d", web_port_); return; }
  httpd_uri_t u_get  = {.uri = "", .method = HTTP_GET,     .handler = http_get_handler,  .user_ctx = this};
  httpd_uri_t u_post = {.uri = "", .method = HTTP_POST,    .handler = http_post_handler, .user_ctx = this};
  httpd_uri_t u_opt  = {.uri = "", .method = HTTP_OPTIONS, .handler = http_options_handler, .user_ctx = this};
  httpd_register_uri_handler(hd, &u_get);
  httpd_register_uri_handler(hd, &u_post);
  httpd_register_uri_handler(hd, &u_opt);
  g_instances[hd] = this;
  esp_log_level_set("httpd", ESP_LOG_WARN);
  ESP_LOGI(TAG, "HTTP on port %d", web_port_);
}

// ===== REST handlers =====

void ZW111Component::handle_get_state(void *req) {
  char buf[1200];
  snprintf(buf, sizeof(buf),
    "{\"Product_SN\":\"%s\",\"Software_Ver\":\"%s\",\"Manufacturer\":\"%s\",\"Sensor_Name\":\"%s\",\"Device_Address\":\"%s\",\"LED_Type\":\"%s\",\"Sensor_Size\":\"%s\",\"Enroll_Count\":%.0f,\"Template_Size\":%.0f,\"Database_Size\":%.0f,\"Score_Level\":%.0f,\"Baud_Rate\":%.0f,\"Templates_Per_Finger\":%.0f,\"Stored_Count\":%.0f,\"Last_Result\":\"%s\",\"Sensor_Check_Result\":\"%s\"}",
    info_.product_sn.c_str(), info_.software_ver.c_str(), info_.manufacturer.c_str(), info_.sensor_name.c_str(),
    info_.device_addr.c_str(), info_.led_type.c_str(), info_.sensor_size.c_str(),
    info_.enroll_count, info_.template_size, info_.database_size, info_.score_level, info_.baud_rate, info_.templates_per_finger, info_.stored_count, info_.last_result.c_str(),
    info_.sensor_check_result.c_str());
  send_json((httpd_req*)req, buf);
}

void ZW111Component::handle_get_settings(void *req) {
  char buf[256];
  snprintf(buf, sizeof(buf),
    "{\"score_level\":%d,\"enroll_max\":%d,\"dup_block\":%d}",
    (int)info_.score_level, setting_enroll_max_, setting_dup_block_);
  send_json((httpd_req*)req, buf);
}

void ZW111Component::handle_get_enrolled(void *req) {
  // 不重复读取索引表: enrolled_ 已由 setup/delete/clear/enroll 等操作实时维护
  std::string json = "{\"enrolled\":[";
  for (int i = 0; i < 100; i++) {
    if (i > 0) json += ",";
    json += enrolled_[i] ? "true" : "false";
  }
  json += "]}";
  send_json((httpd_req*)req, json.c_str());
}

void ZW111Component::handle_post_action(void *req, const char *action, const char *body) {
  int page_id = 0; const char *pid = strstr(body, "\"page\":"); if (pid) page_id = atoi(pid + 7);
  int times = 3; const char *pt = strstr(body, "\"times\":"); if (pt) times = atoi(pt + 8);
  int count = 1; const char *pc = strstr(body, "\"count\":"); if (pc) count = atoi(pc + 8);

  if (strcmp(action, "enroll") == 0) {
    if (enroll_busy_) {
      send_json((httpd_req*)req, "{\"ok\":false,\"msg\":\"Enroll busy\"}");
      return;
    }
    if (page_id < 0 || page_id >= 100) {
      send_json((httpd_req*)req, "{\"ok\":false,\"msg\":\"Invalid ID\"}");
      return;
    }
    if (times < 1) times = (int)setting_enroll_max_;
    enroll_page_id_ = page_id;
    enroll_times_ = times;
    enroll_cancel_ = false;
    enroll_busy_ = true;
    xTaskCreate(enroll_task_entry, "zw111_en", 4096, this, 5, nullptr);
    send_json((httpd_req*)req, "{\"ok\":true,\"msg\":\"Enroll started\"}");
    return;
  } else if (strcmp(action, "cancel") == 0) {
    if (enroll_busy_) {
      enroll_cancel_ = true;
      enroll_phase_ = ENROLL_CANCELED;
      snprintf(enroll_message_, sizeof(enroll_message_), "录入已取消");
      send_json((httpd_req*)req, "{\"ok\":true,\"msg\":\"Cancel sent\"}");
    } else {
      send_json((httpd_req*)req, "{\"ok\":true,\"msg\":\"Nothing to cancel\"}");
    }
    return;
  } else if (strcmp(action, "verify") == 0) {
    uint8_t p[5] = {0x00,0xFF,0xFF,0x00,0x00}; send_command(CMD_AUTO_IDENTIFY, p, 5);
  } else if (strcmp(action, "delete") == 0) {
    uint8_t p[4] = {(uint8_t)(page_id>>8),(uint8_t)(page_id&0xFF),(uint8_t)(count>>8),(uint8_t)(count&0xFF)};
    flush_input(); send_command(CMD_DELETE_CHAR, p, 4);
    uint8_t confirm = 0xFF;
    if (read_response(&confirm) && confirm == CONFIRM_OK) {
      ESP_LOGI(TAG, "Delete ID=%d count=%d success", page_id, count);
      for (int i = page_id; i < page_id + count && i < 100; i++) {
        notepad_cache_[i] = "";
        notepad_dirty_[i] = true;
      }
    } else {
      ESP_LOGW(TAG, "Delete failed, confirm=0x%02X", confirm);
    }
    if (confirm == CONFIRM_OK) {
      flush_input(); delay(30);
      do_read_count(); flush_input(); delay(10);
      if (do_read_index_table()) {
        if (connection_status_) connection_status_->publish_state(true);
      } else {
        if (connection_status_) connection_status_->publish_state(false);
      }
    }
  } else if (strcmp(action, "clear") == 0) {
    flush_input(); send_command(CMD_EMPTY);
    uint8_t confirm = 0xFF;
    if (read_response(&confirm) && confirm == CONFIRM_OK) {
      ESP_LOGI(TAG, "Clear all success");
    } else {
      ESP_LOGW(TAG, "Clear failed, confirm=0x%02X", confirm);
    }
    flush_input(); delay(30); do_read_count();
    if (do_read_index_table()) {
      if (connection_status_) connection_status_->publish_state(true);
    } else {
      if (connection_status_) connection_status_->publish_state(false);
    }
    for (int i = 0; i < 100; i++) { notepad_cache_[i] = ""; notepad_dirty_[i] = true; }
  } else if (strcmp(action, "refresh") == 0) {
    flush_input(); delay(20); do_read_para(); flush_input(); delay(20); do_read_count(); do_read_index_table(); do_check_sensor();
  } else if (strcmp(action, "write_notepad") == 0) {
    std::string content;
    const char *nc = strstr(body, "\"content\":\"");
    if (nc) { nc += 11; while (*nc && *nc != '"') content += *nc++; }
    if (page_id >= 0 && page_id < 100) { notepad_cache_[page_id] = content; notepad_dirty_[page_id] = true; }
  } else if (strcmp(action, "read_notepad") == 0) {
    std::string content;
    if (page_id >= 0 && page_id < 100) content = notepad_cache_[page_id];
    char jbuf[160]; snprintf(jbuf, sizeof(jbuf), "{\"ok\":true,\"content\":\"%s\"}", content.c_str());
    send_json((httpd_req*)req, jbuf); return;
  } else if (strcmp(action, "batch_delete") == 0) {
    int start_id = 0; const char *psi = strstr(body, "\"start_id\":"); if (psi) start_id = atoi(psi + 11);
    int end_id = 0; const char *pei = strstr(body, "\"end_id\":"); if (pei) end_id = atoi(pei + 9);
    if (start_id > end_id) { int tmp = start_id; start_id = end_id; end_id = tmp; }
    if (start_id < 0) start_id = 0;
    if (end_id >= 100) end_id = 99;
    int rcount = end_id - start_id + 1;
    if (rcount < 1) rcount = 1;
    uint8_t bp[4] = {(uint8_t)(start_id>>8),(uint8_t)(start_id&0xFF),(uint8_t)(rcount>>8),(uint8_t)(rcount&0xFF)};
    flush_input(); send_command(CMD_DELETE_CHAR, bp, 4);
    uint8_t bconfirm = 0xFF;
    if (read_response(&bconfirm) && bconfirm == CONFIRM_OK) {
      ESP_LOGI(TAG, "BatchDelete ID=%d..%d count=%d success", start_id, end_id, rcount);
      for (int i = start_id; i <= end_id; i++) { notepad_cache_[i] = ""; notepad_dirty_[i] = true; }
      flush_input(); delay(30); do_read_count(); flush_input(); delay(10);
      if (do_read_index_table()) {
        if (connection_status_) connection_status_->publish_state(true);
      } else {
        if (connection_status_) connection_status_->publish_state(false);
      }
      char jbuf[128]; snprintf(jbuf, sizeof(jbuf), "{\"ok\":true,\"msg\":\"Deleted ID %d-%d (%d)\"}", start_id, end_id, rcount);
      send_json((httpd_req*)req, jbuf);
    } else {
      ESP_LOGW(TAG, "BatchDelete failed, confirm=0x%02X", bconfirm);
      send_json((httpd_req*)req, "{\"ok\":false,\"msg\":\"Batch delete failed\"}");
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
  send_json((httpd_req*)req, "{\"ok\":true}");
}

// ===== Enroll 状态机 =====

void ZW111Component::handle_get_enroll_status(void *req) {
  char buf[384];
  snprintf(buf, sizeof(buf),
    "{\"busy\":%s,\"phase\":%d,\"message\":\"%s\",\"confirm\":\"%s\",\"page_id\":%d,\"times\":%d,\"step\":%d,\"total\":%d}",
    enroll_busy_ ? "true" : "false", (int)enroll_phase_, enroll_message_, enroll_confirm_hex_,
    (int)enroll_page_id_, (int)enroll_times_, (int)enroll_step_, (int)enroll_total_);
  send_json((httpd_req*)req, buf);
}

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

    led_blue_blink();
    flush_input();
    send_command(CMD_AUTO_IDENTIFY, id_params, 5);
    ESP_LOGI(TAG, "[Enroll] Pre-verify AutoIdentify sent");

    uint32_t vstart = millis();
    bool verified = false;
    bool is_match = false;

    while (millis() - vstart < 15000) {
      if (enroll_cancel_) {
        ESP_LOGI(TAG, "[Enroll] canceled during pre-verify");
        led_all_off();
        enroll_phase_ = ENROLL_CANCELED;
        snprintf(enroll_message_, sizeof(enroll_message_), "录入已取消");
        goto cleanup;
      }
      if (this->available() < 9) { delay(10); continue; }

      uint8_t b1 = this->read();
      if (b1 != PACKET_HEADER_HI) continue;
      if (this->available() < 1) { delay(1); continue; }
      uint8_t b2 = this->read();
      if (b2 != PACKET_HEADER_LO) continue;

      uint8_t addr[4]; if (!this->read_array(addr, 4)) continue;
      uint8_t pkt_type; if (!this->read_array(&pkt_type, 1)) continue;
      uint8_t len_buf[2]; if (!this->read_array(len_buf, 2)) continue;
      uint16_t pkt_len = ((uint16_t)len_buf[0] << 8) | len_buf[1];
      if (pkt_len < 2 || pkt_len > 32) continue;

      uint8_t payload[32] = {0};
      if (!this->read_array(payload, pkt_len)) continue;

      uint16_t plen = pkt_len - 2;
      uint8_t cs[3] = {pkt_type, len_buf[0], len_buf[1]};
      uint16_t csum = calc_checksum(cs, 3) + calc_checksum(payload, plen);
      uint16_t rsum = ((uint16_t)payload[plen] << 8) | payload[plen + 1];
      if (csum != rsum) continue;

      uint8_t vc = payload[0];
      uint8_t vp1 = (plen > 1) ? payload[1] : 0;

      if (vc == CONFIRM_OK && vp1 == 0x05) {
        uint16_t mid = ((uint16_t)payload[2] << 8) | payload[3];
        ESP_LOGI(TAG, "[Enroll] Pre-verify MATCH pageID=%d", mid);
        is_match = true; verified = true; break;
      } else if (vc == 0x09) { verified = true; break; }
      else if (vc == 0x24) { verified = true; break; }
      else if (vc != CONFIRM_OK) { verified = true; break; }
      delay(5);
    }

    if (!verified) {
      led_red_blink(3); delay(1000); led_all_off();
      ESP_LOGW(TAG, "[Enroll] Pre-verify timeout, cancel enroll");
      snprintf(enroll_message_, sizeof(enroll_message_), "超时, 录入取消");
      enroll_phase_ = ENROLL_FAILED;
      snprintf(enroll_confirm_hex_, sizeof(enroll_confirm_hex_), "Timeout");
      goto cleanup;
    } else if (is_match) {
      led_red_blink(3); delay(1000); led_all_off();
      ESP_LOGW(TAG, "[Enroll] Fingerprint already enrolled, cancel enroll");
      snprintf(enroll_message_, sizeof(enroll_message_), "重复手指! 请勿重复录入");
      enroll_phase_ = ENROLL_FAILED;
      snprintf(enroll_confirm_hex_, sizeof(enroll_confirm_hex_), "Dup:MATCH");
      goto cleanup;
    } else {
      led_green_steady();
      ESP_LOGI(TAG, "[Enroll] Pre-verify passed, no duplicate");
      ESP_LOGI(TAG, "[Enroll] Pre-verify passed, waiting finger lift...");
      enroll_phase_ = ENROLL_LIFT_FINGER;
      snprintf(enroll_message_, sizeof(enroll_message_), "检查通过, 请抬起手指");
      uint32_t lift_start = millis();
      while (true) {
        if (enroll_cancel_) {
          ESP_LOGI(TAG, "[Enroll] canceled in lift wait after pre-verify");
          led_all_off();
          enroll_phase_ = ENROLL_CANCELED;
          snprintf(enroll_message_, sizeof(enroll_message_), "录入已取消");
          goto cleanup;
        }
        if (millis() - lift_start > 15000) {
          ESP_LOGW(TAG, "[Enroll] finger lift timeout after pre-verify");
          snprintf(enroll_message_, sizeof(enroll_message_), "等待手指离开超时");
          enroll_phase_ = ENROLL_FAILED; led_red_blink(3); goto cleanup;
        }
        flush_input();
        if (!send_command(0x01)) { delay(200); continue; }
        if (!read_response(&confirm, nullptr, 0, 3000)) { delay(200); continue; }
        if (confirm == 0x02) { ESP_LOGI(TAG, "[Enroll] finger lifted after pre-verify"); led_all_off(); break; }
        delay(200);
      }
    }
  }

  uint8_t store_params[3];
  store_params[0] = 0x01;
  store_params[1] = (uint8_t)(page_id >> 8);
  store_params[2] = (uint8_t)(page_id & 0xFF);

  ESP_LOGI(TAG, "[Enroll] START pageID=%d times=%d dup_block=%d", page_id, times, setting_dup_block_);
  flush_input();

  for (int n = 1; n <= times; n++) {
    if (enroll_cancel_) {
      ESP_LOGI(TAG, "[Enroll] canceled at step %d/%d", n, times);
      enroll_phase_ = ENROLL_CANCELED;
      snprintf(enroll_message_, sizeof(enroll_message_), "录入已取消");
      goto cleanup;
    }
    enroll_step_ = n;
    snprintf(enroll_confirm_hex_, sizeof(enroll_confirm_hex_), "");

    while (true) {
      if (enroll_cancel_) {
        ESP_LOGI(TAG, "[Enroll] canceled in GetImage wait (step %d)", n);
        led_all_off();
        enroll_phase_ = ENROLL_CANCELED;
        snprintf(enroll_message_, sizeof(enroll_message_), "录入已取消");
        goto cleanup;
      }
      enroll_phase_ = ENROLL_WAIT_FINGER;
      snprintf(enroll_message_, sizeof(enroll_message_), "第 %d/%d 次: 请按压手指", n, times);
      led_blue_blink();
      flush_input();
      if (!send_command(0x29)) {
        ESP_LOGW(TAG, "[Enroll] PS_GetEnrollImage send fail step %d", n);
        enroll_phase_ = ENROLL_FAILED; goto cleanup;
      }
      if (!read_response(&confirm, nullptr, 0, step_timeout_ms)) {
        ESP_LOGW(TAG, "[Enroll] PS_GetEnrollImage timeout step %d", n);
        enroll_phase_ = ENROLL_FAILED; goto cleanup;
      }
      if (confirm == CONFIRM_OK) { led_all_off(); led_green_steady(); break; }
      if (confirm != 0x02) {
        ESP_LOGW(TAG, "[Enroll] PS_GetEnrollImage error step %d confirm=0x%02X", n, confirm);
        led_red_blink(3);
        snprintf(enroll_confirm_hex_, sizeof(enroll_confirm_hex_), "GetImage:0x%02X", confirm);
        enroll_phase_ = ENROLL_FAILED; goto cleanup;
      }
      delay(200);
    }
    ESP_LOGI(TAG, "[Enroll] GetImage OK step %d/%d confirm=0x%02X", n, times, confirm);

    enroll_phase_ = ENROLL_CAPTURED;
    snprintf(enroll_message_, sizeof(enroll_message_), "第 %d/%d 次: 图像采集成功, 生成特征中...", n, times);
    snprintf(enroll_confirm_hex_, sizeof(enroll_confirm_hex_), "GetImg:OK");

    uint8_t gen_params[1] = {(uint8_t)n};
    flush_input();
    if (!send_command(0x02, gen_params, 1)) { enroll_phase_ = ENROLL_FAILED; goto cleanup; }
    if (!read_response(&confirm, nullptr, 0, step_timeout_ms) || confirm != CONFIRM_OK) {
      ESP_LOGW(TAG, "[Enroll] PS_GenChar fail step %d confirm=0x%02X", n, confirm);
      snprintf(enroll_confirm_hex_, sizeof(enroll_confirm_hex_), "GenChar:0x%02X", confirm);
      enroll_phase_ = ENROLL_FAILED; goto cleanup;
    }
    ESP_LOGI(TAG, "[Enroll] GenChar OK step %d/%d bufID=%d", n, times, n);
    enroll_phase_ = ENROLL_GEN_DONE;
    snprintf(enroll_message_, sizeof(enroll_message_), "第 %d/%d 次: 特征生成完毕", n, times);
    snprintf(enroll_confirm_hex_, sizeof(enroll_confirm_hex_), "GenCh:OK");

    if (n < times) {
      ESP_LOGI(TAG, "[Enroll] waiting finger lift step %d/%d", n, times);
      enroll_phase_ = ENROLL_LIFT_FINGER;
      snprintf(enroll_message_, sizeof(enroll_message_), "第 %d/%d 次完成, 请抬起手指", n, times);
      uint32_t lift_start = millis();
      while (true) {
        if (enroll_cancel_) {
          ESP_LOGI(TAG, "[Enroll] canceled in lift wait (step %d)", n);
          led_all_off();
          enroll_phase_ = ENROLL_CANCELED;
          snprintf(enroll_message_, sizeof(enroll_message_), "录入已取消");
          goto cleanup;
        }
        if (millis() - lift_start > step_timeout_ms) {
          ESP_LOGW(TAG, "[Enroll] finger lift timeout step %d", n);
          enroll_phase_ = ENROLL_FAILED; goto cleanup;
        }
        flush_input();
        if (!send_command(0x01)) { delay(200); continue; }
        if (!read_response(&confirm, nullptr, 0, 3000)) { delay(200); continue; }
        if (confirm == 0x02) break;
        delay(200);
      }
      ESP_LOGI(TAG, "[Enroll] finger lifted step %d/%d", n, times);
    }
  }

  if (enroll_cancel_) {
    ESP_LOGI(TAG, "[Enroll] canceled before merge");
    led_all_off();
    enroll_phase_ = ENROLL_CANCELED;
    snprintf(enroll_message_, sizeof(enroll_message_), "录入已取消");
    goto cleanup;
  }

  ESP_LOGI(TAG, "[Enroll] PS_RegModel start...");
  enroll_phase_ = ENROLL_MERGE;
  flush_input();
  if (!send_command(0x05)) { enroll_phase_ = ENROLL_FAILED; goto cleanup; }
  if (!read_response(&confirm, nullptr, 0, step_timeout_ms) || confirm != CONFIRM_OK) {
    ESP_LOGW(TAG, "[Enroll] PS_RegModel fail confirm=0x%02X", confirm);
    snprintf(enroll_confirm_hex_, sizeof(enroll_confirm_hex_), "Merge:0x%02X", confirm);
    enroll_phase_ = ENROLL_FAILED; goto cleanup;
  }
  ESP_LOGI(TAG, "[Enroll] PS_RegModel OK");
  enroll_phase_ = ENROLL_DUP_CHECK;

  if (setting_dup_block_ && info_.stored_count > 0) {
    uint16_t db_size = (uint16_t)info_.database_size;
    if (db_size == 0) db_size = 100;
    uint8_t search_params[5] = {0x01, (uint8_t)(0>>8), (uint8_t)(0&0xFF), (uint8_t)(db_size>>8), (uint8_t)(db_size&0xFF)};
    flush_input();
    if (send_command(0x04, search_params, 5)) {
      uint8_t sc = 0xFF; uint8_t sd[4] = {0};
      if (read_response(&sc, sd, 4, step_timeout_ms) && sc == CONFIRM_OK) {
        uint16_t fp = ((uint16_t)sd[0] << 8) | sd[1];
        ESP_LOGI(TAG, "[Enroll] PS_Search MATCH pageID=%d", fp);
        enroll_phase_ = ENROLL_FAILED; goto cleanup;
      }
    }
  }

  ESP_LOGI(TAG, "[Enroll] PS_StoreChar start pageID=%d", page_id);
  flush_input();
  if (!send_command(0x06, store_params, 3)) { enroll_phase_ = ENROLL_FAILED; goto cleanup; }
  if (!read_response(&confirm, nullptr, 0, step_timeout_ms) || confirm != CONFIRM_OK) {
    ESP_LOGW(TAG, "[Enroll] PS_StoreChar fail confirm=0x%02X pageID=%d", confirm, page_id);
    enroll_phase_ = ENROLL_FAILED; goto cleanup;
  }
  ESP_LOGI(TAG, "[Enroll] PS_StoreChar OK pageID=%d", page_id);

  enroll_phase_ = ENROLL_SUCCESS;
  final_ok = true;
  snprintf(enroll_message_, sizeof(enroll_message_), "ID:%d 录入成功!", page_id);
  snprintf(enroll_confirm_hex_, sizeof(enroll_confirm_hex_), "Store:OK");

cleanup:
  ESP_LOGI(TAG, "[Enroll] cleanup: final_ok=%d phase=%d", final_ok, (int)enroll_phase_);
  if (enroll_cancel_ || (!final_ok && enroll_phase_ != ENROLL_SUCCESS)) {
    flush_input(); send_command(CMD_CANCEL);
    { uint8_t junk; read_response(&junk, nullptr, 0, 300); }
    flush_input(); delay(80);
  }
  if (final_ok) { delay(1500); delay(3000); led_all_off(); }
  else if (enroll_phase_ == ENROLL_FAILED) { led_all_off(); }
  else if (enroll_phase_ == ENROLL_CANCELED) { led_all_off(); }
  flush_input();

  if (final_ok) {
    ESP_LOGI(TAG, "[Enroll] reading real state from module...");
    do_read_count();
    if (do_read_index_table()) {
      if (connection_status_) connection_status_->publish_state(true);
    } else {
      if (connection_status_) connection_status_->publish_state(false);
    }
    info_.last_result = enroll_message_;
  }
  enroll_busy_ = false; enroll_cancel_ = false;
  if (!final_ok && enroll_phase_ != ENROLL_CANCELED) enroll_phase_ = ENROLL_FAILED;
  flush_input(); delay(50); flush_input();
  ESP_LOGI(TAG, "[Enroll] END final_ok=%d phase=%d", final_ok, (int)enroll_phase_);
}

void ZW111Component::enroll_task_entry(void *arg) {
  ZW111Component *self = static_cast<ZW111Component*>(arg);
  self->do_enroll(self->enroll_page_id_, self->enroll_times_);
  vTaskDelete(nullptr);
}

// ===== 指纹验证 (PS_AutoIdentify) =====

void ZW111Component::do_identify() {
  int score_level = (int)info_.score_level;
  if (score_level < 1 || score_level > 5) score_level = 3;
  info_.last_result = "";

  // 蓝色闪烁表示正在验证
  led_blue_blink();
  uint8_t id_params[5] = {(uint8_t)score_level, 0xFF, 0xFF, 0x00, 0x00};
  flush_input(); send_command(CMD_AUTO_IDENTIFY, id_params, 5);
  ESP_LOGI(TAG, "[Identify] AutoIdentify sent (blue LED)");

  bool got_result = false, identify_success = false;
  uint32_t start = millis();

  while (millis() - start < 10000) {
    if (this->available() < 9) { delay(10); continue; }
    uint8_t b1 = this->read(); if (b1 != PACKET_HEADER_HI) continue;
    if (this->available() < 1) { delay(1); continue; }
    uint8_t b2 = this->read(); if (b2 != PACKET_HEADER_LO) continue;
    uint8_t addr[4]; if (!this->read_array(addr, 4)) continue;
    uint8_t pkt_type; if (!this->read_array(&pkt_type, 1)) continue;
    uint8_t len_buf[2]; if (!this->read_array(len_buf, 2)) continue;
    uint16_t pkt_len = ((uint16_t)len_buf[0] << 8) | len_buf[1];
    if (pkt_len < 2 || pkt_len > 32) continue;
    uint8_t payload[32] = {0};
    if (!this->read_array(payload, pkt_len)) continue;
    uint16_t plen = pkt_len - 2;
    uint8_t cs[3] = {pkt_type, len_buf[0], len_buf[1]};
    uint16_t csum = calc_checksum(cs, 3) + calc_checksum(payload, plen);
    uint16_t rsum = ((uint16_t)payload[plen] << 8) | payload[plen + 1];
    if (csum != rsum) continue;
    uint8_t confirm = payload[0];
    uint8_t param1 = (plen > 1) ? payload[1] : 0;

    if (confirm == CONFIRM_OK && param1 == 0x05) {
      uint16_t match_id = ((uint16_t)payload[2] << 8) | payload[3];
      uint16_t score = ((uint16_t)payload[4] << 8) | payload[5];
      ESP_LOGI(TAG, "[Identify] MATCH pageID=%d score=%d", match_id, score);
      std::string note = notepad_cache_[match_id];
      if (!note.empty() && note.length() > 0) {
        if (fp_identify_sensor_) fp_identify_sensor_->publish_state(note);
      } else {
        char idbuf[8]; snprintf(idbuf, sizeof(idbuf), "%d", match_id);
        if (fp_identify_sensor_) fp_identify_sensor_->publish_state(idbuf);
      }
      led_all_off(); led_green_steady();
      info_.last_result = "OK";
      got_result = true; identify_success = true; break;
    } else if (confirm == 0x09) {
      info_.last_result = "No Match";
      if (fp_identify_sensor_) fp_identify_sensor_->publish_state("No Match");
      got_result = true; break;
    } else if (confirm == 0x24) {
      info_.last_result = "Library Empty";
      if (fp_identify_sensor_) fp_identify_sensor_->publish_state("Lib Empty");
      got_result = true; break;
    } else if (confirm != CONFIRM_OK) {
      info_.last_result = "Verify Failed";
      if (fp_identify_sensor_) fp_identify_sensor_->publish_state("Error");
      got_result = true; break;
    }
    delay(5);
  }
  if (!got_result) {
    info_.last_result = "No Match";
    if (fp_identify_sensor_) fp_identify_sensor_->publish_state("No Match");
  }
  if (identify_success) { delay(2000); led_all_off(); }
  else { led_all_off(); led_control(0x03, 0x04, 0x00, 0x00); delay(2000); led_all_off(); }
  if (fp_identify_sensor_) fp_identify_sensor_->publish_state("-");
  identify_busy_ = false; flush_input();
}

void ZW111Component::identify_task_entry(void *arg) {
  ZW111Component *self = static_cast<ZW111Component*>(arg);
  self->do_identify();
  vTaskDelete(nullptr);
}

void ZW111Component::trigger_identify() {
  if (!initialized_ || enroll_busy_ || identify_busy_) return;
  flush_input();
  ESP_LOGI(TAG, "Identify triggered by button");
  identify_busy_ = true;
  xTaskCreate(identify_task_entry, "zw111_id", 4096, this, 5, nullptr);
}

void ZW111Component::trigger_sleep() {
  if (!initialized_) return;
  flush_input(); send_command(CMD_SLEEP);
  uint8_t confirm = 0xFF;
  if (read_response(&confirm)) {
    if (confirm == CONFIRM_OK) ESP_LOGI(TAG, "Sleep command sent successfully, module entering sleep mode");
    else ESP_LOGW(TAG, "Sleep command failed, confirm=0x%02X", confirm);
  } else {
    ESP_LOGW(TAG, "Sleep command: no response (module may have entered sleep)");
  }
  initialized_ = false;
  // 休眠后连接断开，发布诊断状态
  if (connection_status_) connection_status_->publish_state(false);
  if (sensor_check_status_) sensor_check_status_->publish_state(true);
}

void ZW111SleepButton::press_action() { this->get_parent()->trigger_sleep(); }
void ZW111Button::press_action() { this->get_parent()->trigger_identify(); }

}  // namespace zw111
}  // namespace esphome