# ZW111 指纹模组 ESPHome 外部组件

[![ESPHome](https://img.shields.io/badge/ESPHome-2024.6+-blue.svg)](https://esphome.io)
[![License](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE)

基于 ESPHome 的 ZW111 海凌科指纹模组外部组件，支持指纹录入、验证、删除、批量管理、Web 管理界面等功能。

---

## 功能特性

### 核心功能
- **指纹录入** — 支持多次按压采集，自动合并特征并存储模版，录入过程中伴有实时 GUI 反馈
- **指纹验证** — 1:N 搜索，匹配时输出备注名或 ID，不匹配时提示"No Match"
- **指纹删除** — 单条删除、连续 ID 区间删除、清空全部指纹库
- **备注管理** — 为每个指纹 ID 绑定最多 32 字符的字母/数字备注,持久化存储
- **自定义配置** — 验证置信度(1-5级)、录入采集次数(1-8次)、禁止重复注册开关
- **多实例支持** — 支持配置多个zw111组件，用于单个主控连接多个指纹模块



### 自动化集成
- `connection_status` — `binary_sensor`，指纹模组连接状态
- `sensor_check_status` — `binary_sensor`，传感器校验结果
- `fp_identify_action` — `text_sensor`，验证结果文本（ID/备注、"No Match"等），支持 `on_value` 自动化触发器
- `identify_button` — `button`，触发验证流程
- `sleep_button` — `button`，触发休眠（可选）

### Web 管理界面
- 内建 HTTP 服务器，无需第三方工具组件
- Web GU支持指纹管理、备注编辑、系统设置
- Basic-Auth 认证保护,防止无授权访问
- 移动端友好的自适应布局



## 硬件接线

| 模组引脚 | 说明 | 连接 ESP32 |
|---------|------|-----------|
| VCC | 3.3V 模组供电 | 3.3V |
| GND | 地 | GND |
| TX | UART 发送 | GPIO5 (RX) |
| RX | UART 接收 | GPIO4 (TX) |
| TOUCH_OUT | 触摸唤醒 IRQ | 任意可用GPIO |
| V_SENSOR | 触摸反馈 3.3V | 3.3V (常供电) |



## 使用要求
- 仅支持esp32及其变体，不支持esp8266
- 只支持esp-idf框架



## 安装与使用
### 1. 配置参数

| 参数 | 类型 | 默认值 | 描述 |
|------|------|--------|------|
| `id` | string | — | 组件 ID,与备注储存绑定，更改id将导致指纹备注丢失 |
| `uart_id` | ID | — | UART 总线 ID |
| `web_port` | int | 8080 | Web 管理页面端口 |
| `web_username` | string | "" | Web 认证用户名 |
| `web_password` | string | "" | Web 认证密码 |
| `connection_status` | binary_sensor | — | 模组连接状态 |
| `sensor_check_status` | binary_sensor | — | 传感器校验结果 |
| `fp_identify_action` | text_sensor | — | 验证结果文本 |
| `identify_button` | button | — | 触发验证按钮 |
| `sleep_button` | button | — | 触发休眠按钮（可选） |

### 2. 基础配置

```yaml
external_components:
  - source:
      type: git
      url: https://github.com/gasment/esphome_zw111_fingerprint_with_web_manager
      ref: main
    components: [ zw111 ]
esp32:
  framework:
    type: esp-idf
    sdkconfig_options:
      CONFIG_ESP_TASK_WDT_TIMEOUT_S: "30"
      CONFIG_ESP_INT_WDT_TIMEOUT_MS: "800"
    advanced:
      loop_task_stack_size: 10240
uart:
  - id: zw111_uart  
    tx_pin: GPIO4  #任意可分配GPIO
    rx_pin: GPIO5  #任意可分配GPIO
    baud_rate: 57600
    data_bits: 8
    stop_bits: 1
    parity: NONE

zw111:
  - id: zw111_main
    uart_id: zw111_uart
    web_port: 8088 ##任意可分配端口
    web_username: "admin"
    web_password: "admin"

    connection_status:
      name: "ZW111 Connection"
      id: zw111_main_connection_status

    sensor_check_status:
      name: "ZW111 Sensor Check"
      id: zw111_main_sensor_check

    fp_identify_action:
      name: "Fingerprint ID"
      id: zw111_main_fp_identify_action

    identify_button:
      name: "Identify"
      id: zw111_main_identify_button

    sleep_button: 
      name: "Sleep"
      id: zw111_main_sleep_button
```

### 3. 配合 GPIO 触发验证

```yaml
binary_sensor:
  - platform: gpio
    pin: GPIO6  #任意可分配GPIO
    name: "Fingerprint Pressed"
    icon: "mdi:fingerprint"
    filters:
      - delayed_off: 1500ms
    on_press:
      then:
        - button.press: zw111_main_identify_button
```

### 4. 自动化示例

`fp_identify_action` 是一个 `text_sensor`，可以在 YAML 中配置 `on_value` 触发器：

```yaml
fp_identify_action:
  name: "Fingerprint ID"
  id: zw111_main_fp_identify_action
  on_value:
    then:
      - if:
          condition:
            lambda: 'return x == "myfinger";'
          then:
            - switch.toggle: my_switch
```

---



## LED 指示说明

| 场景 | LED 效果 |
|------|----------|
| 验证进行中（Identify） | 🔵 蓝灯呼吸慢闪 |
| 等待按压手指（录入采集） | 🔵 蓝灯呼吸慢闪 |
| 图像采集成功 | 🟢 绿灯常亮 |
| 验证匹配成功 | 🟢 绿灯常亮 2s |
| 验证匹配失败 | 🔴 红灯常亮 2s |
| 录入错误/失败 | 🔴 红灯快闪 3 次 |
| 指纹重复（防重开启） | 🔴 红灯快闪 3 次 |
| 录入完成 | 🟢 绿灯常亮 3s → 熄灭 |

---



## Web 管理页面

默认访问 `http://device_ip:8080`进入：

- **指纹管理标签页**
  - 100 个 ID 的网格视图，已注册 ID 绿色高亮
  - 分页浏览（每页 16 个）
  - 点击 ID 弹出详情：录入指纹、编辑备注、删除
  - 批量删除：连续 ID 删除 / 全部清空
  - 录入弹窗实时显示进度（采集次数、LED 动画、状态文字）

- **设置/关于标签页**
  - 模块信息展示（型号、版本、厂商、传感器、注册数等）
  - 验证置信度、录入采集次数、禁止重复注册开关
<img src= "https://github.com/gasment/esphome_zw111_fingerprint_with_web_manager/blob/main/zw111.webp" />

---


## 其他说明

### NVS 命名空间隔离

组件支持多实例，每个实例的 NVS 键值使用实例的 `id` 作为前缀隔离（如 `zw111_main_n`、`zw111_main_c`），避免多模组场景下的数据冲突。

- 指纹备注信息（ID 0-15）：存储在模组 Flash
- 指纹备注信息（ID 16-99）：存储主控NVS分区
- 配置数据：存储主控NVS分区。
- 因此，更换主控或清空主控flash不会丢失0-15号指纹的备注信息，优先使用前16个位置。同时注意不要随意更改zw111组件的id配置,会丢失nvs数据
- 指纹全部位于模组 Flash内，除非手动清空，否则不会丢失

