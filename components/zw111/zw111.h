#pragma once

#include "esphome/core/component.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/button/button.h"

namespace esphome {
namespace zw111 {

static const uint8_t PACKET_HEADER_HI = 0xEF;
static const uint8_t PACKET_HEADER_LO = 0x01;
static const uint32_t DEFAULT_DEVICE_ADDRESS = 0xFFFFFFFF;
static const uint8_t PKT_TYPE_COMMAND  = 0x01;
static const uint8_t PKT_TYPE_RESPONSE = 0x07;
static const uint8_t PKT_TYPE_DATA     = 0x02;
static const uint8_t PKT_TYPE_END      = 0x08;
static const uint8_t CMD_HANDSHAKE         = 0x35;
static const uint8_t CMD_READ_SYS_PARA     = 0x0F;
static const uint8_t CMD_READ_INF_PAGE     = 0x16;
static const uint8_t CMD_READ_ADD_PARA     = 0x62;
static const uint8_t CMD_VALID_TEMPLATE_NUM = 0x1D;
static const uint8_t CMD_READ_INDEX_TABLE   = 0x1F;
static const uint8_t CMD_AUTO_ENROLL       = 0x31;
static const uint8_t CMD_AUTO_IDENTIFY     = 0x32;
static const uint8_t CMD_DELETE_CHAR       = 0x0C;
static const uint8_t CMD_EMPTY             = 0x0D;
static const uint8_t CMD_WRITE_NOTEPAD     = 0x18;
static const uint8_t CMD_READ_NOTEPAD      = 0x19;
static const uint8_t CMD_CANCEL            = 0x30;
static const uint8_t CMD_SLEEP             = 0x33;
static const uint8_t CMD_WRITE_REG         = 0x0E;
static const uint8_t CMD_WRITE_EM_PARA     = 0x63;
static const uint8_t CMD_CHECK_SENSOR      = 0x36;
static const uint8_t CMD_CONTROL_BLN       = 0x3C;
static const uint8_t CMD_BLN_MODE_SW       = 0x60;
static const uint8_t CONFIRM_OK = 0x00;
static const uint16_t MAX_PACKET_SIZE = 256;
static const uint32_t DEFAULT_TIMEOUT_MS = 1000;
static const uint32_t LONG_TIMEOUT_MS = 5000;
static const uint16_t INF_PAGE_SIZE = 512;
static const char *LED_TYPE_NAMES[] = {"None","RGB 3-Color","Blue","Red","Green","Red+Blue","Red+Green","Blue+Green","White"};
class ZW111Component : public Component, public uart::UARTDevice {
 public:
  ZW111Component() = default;

  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return esphome::setup_priority::BUS; }

  void set_connection_status(esphome::binary_sensor::BinarySensor *s) { connection_status_ = s; }
  void set_sensor_check_status(esphome::binary_sensor::BinarySensor *s) { sensor_check_status_ = s; }
  void set_web_port(uint16_t port) { web_port_ = port; }
  void set_web_username(const std::string &u) { web_user_ = u; }
  void set_web_password(const std::string &p) { web_pass_ = p; }
  void set_fp_identify_sensor(esphome::text_sensor::TextSensor *s) { fp_identify_sensor_ = s; }
  void set_nvs_prefix(const std::string &prefix) { nvs_prefix_ = prefix; }

  void trigger_identify();
  void trigger_sleep();

  void handle_get_state(void *req);
  void handle_get_settings(void *req);
  void handle_get_enrolled(void *req);
  void handle_get_enroll_status(void *req);
  void handle_post_action(void *req, const char *action, const char *body);

  uint16_t web_port_{0};
  bool web_started_{false};
  std::string web_user_;
  std::string web_pass_;

  // Enroll 状态机 (供 WebGUI 轮询)
  enum EnrollPhase : uint8_t {
    ENROLL_IDLE = 0,
    ENROLL_SETUP,        // 正在配置参数
    ENROLL_WAIT_FINGER,  // 请放置手指
    ENROLL_CAPTURED,     // 图像已采集, 正在生成特征
    ENROLL_GEN_DONE,     // 特征生成完毕
    ENROLL_LIFT_FINGER,  // 请抬起手指
    ENROLL_MERGE,        // 正在合并模板
    ENROLL_DUP_CHECK,    // 正在检查重复
    ENROLL_STORE,        // 正在存储模板
    ENROLL_SUCCESS,      // 录入成功
    ENROLL_FAILED,       // 录入失败
    ENROLL_CANCELED      // 已被取消
  };
  volatile bool enroll_busy_{false};
  volatile bool enroll_cancel_{false};
  volatile EnrollPhase enroll_phase_{ENROLL_IDLE};
  volatile int enroll_page_id_{0};
  volatile int enroll_times_{0};
  volatile int enroll_step_{0};
  volatile int enroll_total_{0};
  char enroll_message_[160]{};
  char enroll_confirm_hex_[16]{};
  void do_enroll(int page_id, int times);
  void do_identify();
  static void enroll_task_entry(void *arg);
  static void identify_task_entry(void *arg);
  bool is_enroll_busy() const { return enroll_busy_; }
  bool is_identify_busy() const { return identify_busy_; }

  // LED 控制 (PS_ControlBLN 多功能三色灯)
  void led_control(uint8_t func, uint8_t color, uint8_t cycles, uint8_t time_10ms);
  void led_all_off();
  void led_green_steady();
  void led_red_blink(uint8_t times);
  void led_blue_blink();

 protected:
  uint16_t calc_checksum(const uint8_t *data, uint16_t len);
  bool send_command(uint8_t cmd, const uint8_t *params = nullptr, uint16_t param_len = 0);
  bool read_response(uint8_t *confirm, uint8_t *data = nullptr, uint16_t data_len = 0, uint32_t timeout_ms = DEFAULT_TIMEOUT_MS);
  bool read_data_packets(uint8_t *buffer, uint16_t buffer_size, uint16_t &total_read, uint32_t timeout_ms = DEFAULT_TIMEOUT_MS);
  void flush_input();
  bool do_handshake();
  bool do_read_para();
  bool do_read_count();
  bool do_read_index_table();
  bool do_check_sensor();
  std::string nvs_ns();
  std::string nvs_cfg();
  std::string extract_ascii_string(const uint8_t *data, uint16_t len);
  const char *led_type_to_name(uint16_t lt);
  void start_web_server();

  struct DeviceInfo {
    std::string product_sn;
    std::string software_ver;
    std::string manufacturer;
    std::string sensor_name;
    std::string device_addr;
    std::string led_type;
    std::string sensor_size;
    float enroll_count = 0;
    float template_size = 0;
    float database_size = 0;
    float score_level = 0;
    float baud_rate = 0;
    float templates_per_finger = 0;
    float stored_count = 0;
    std::string last_result;
    std::string sensor_check_result;
  } info_;

  std::string notepad_cache_[100];
  bool        notepad_dirty_[100]{};

  void flush_dirty_notepad();
  void load_all_notepads();
  void write_notepad_flash(int page, const std::string &content);
  std::string read_notepad_flash(int page);
  void write_notepad_nvs(int id, const std::string &content);
  std::string read_notepad_nvs(int id);

  bool initialized_{false};
  std::string nvs_prefix_;
  uint32_t device_addr_{DEFAULT_DEVICE_ADDRESS};
  uint8_t tx_buffer_[MAX_PACKET_SIZE];
  uint8_t rx_buffer_[MAX_PACKET_SIZE];

  int dirty_index_{0};

  bool enrolled_[100]{};

  uint8_t setting_enroll_max_{5};
  uint8_t setting_dup_block_{0};
  void load_settings_from_nvs();
  void save_setting_to_nvs(const char *key, uint8_t value);
  void apply_score_level_to_module(uint8_t level);

  esphome::binary_sensor::BinarySensor *connection_status_{nullptr};
  esphome::binary_sensor::BinarySensor *sensor_check_status_{nullptr};

  // 指纹验证
  bool identify_busy_{false};
  esphome::text_sensor::TextSensor *fp_identify_sensor_{nullptr};
};

class ZW111Button : public button::Button, public Parented<ZW111Component> {
 public:
  void press_action() override;
};

class ZW111SleepButton : public button::Button, public Parented<ZW111Component> {
 public:
  void press_action() override;
};

}  // namespace zw111
}  // namespace esphome