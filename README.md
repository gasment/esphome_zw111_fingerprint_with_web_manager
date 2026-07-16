# ZW111 指纹模组 ESPHome 外部组件（V2）

[![ESPHome](https://img.shields.io/badge/ESPHome-2026.6+-blue.svg)](https://esphome.io)
[![License](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE)

基于 ESPHome 的 ZW111 海凌科指纹模组外部组件，支持指纹录入、验证、删除、批量管理、Web 管理、低功耗睡眠模式等

## V2与V1差异：
|  | V1 | V2 |
|---------|------|-----------|
| Web实现 | esp_http_server | Mongoose 7.22 |
| 指纹模组睡眠 |基础睡眠 | 完整唤醒逻辑|
| ESP深度睡眠支持 |否 | 是|
| VCC使能模式 | ESPHome通用组件(switch/output) | 专有配置|
| 触摸传感器 | ESPHome通用组件(binary_sensor) | 专有配置 |

* V2对V1存在破坏性更新，无法从V1迁移到V2
* 跳转到[V1](https://github.com/gasment/esphome_zw111_fingerprint_with_web_manager/tree/v1 "V1")


## 功能特性

### 核心功能
- **指纹录入** — 支持多次按压采集，自动合并特征并存储模版，录入过程中伴有实时 GUI 反馈
- **指纹验证** — 1:N 搜索，匹配时输出备注名或 ID，不匹配时提示"No Match"
- **指纹删除** — 单条删除、连续 ID 区间删除、清空全部指纹库
- **备注管理** — 为每个指纹 ID 绑定最多 32 字符的字母/数字备注,NVS持久化存储
- **自定义配置** — 验证置信度(1-5级)、录入采集次数(1-8次)、禁止重复注册开关


### 组件提供
- `connection_status` — `binary_sensor`，指纹模组连接状态
- `sensor_check_status` — `binary_sensor`，传感器校验结果
- `fp_identify_action` — `text_sensor`，验证结果文本（ID/备注、"No Match"等）
- `identify_button` — `button`，手动触发验证流程
- `sleep_button` — `button`，触发休眠
- `touch_sensor` - `binary_sensor`，触摸传感器状态

### Web 管理界面
- 内建 HTTP 服务器，无需第三方工具组件
- 支持STA与AP模式下访问
- Web GU支持指纹管理、备注编辑、系统设置
- 用户名与密码认证保护,防止无授权访问
- 移动端友好的自适应布局



## 硬件接线

| 模组引脚 | 脚位 | 说明 | 连接 ESP32 |
|---------|--------|-----------|-----------|
| V_SENSOR | 1 | 触摸供电（3V3常供电） | --|
| TOUCH_OUT | 2 | 触摸反馈/休眠唤醒 | 任意可用GPIO|
| VCC | 3 | 模组供电（休眠） | 不需休眠则直连3V3,如需休眠需搭配PMOS电路,控制端连接任意可用GPIO|
| TX | 4 |UART 发送 | 任意可用GPIO (RX) |
| RX | 5 |UART 接收 | 任意可用GPIO(TX)  |
| GND | 6 | 地 |--|
<img width="287" height="283" alt="ScreenShot_2026-07-16_110325_420" src="https://github.com/gasment/esphome_zw111_fingerprint_with_web_manager/blob/dev/ScreenShot_2026-07-16_110325_420.png" />
* 官方PMOS电路示例
<img width="287" height="283" alt="ScreenShot_2026-07-16_110325_420" src="https://github.com/gasment/esphome_zw111_fingerprint_with_web_manager/blob/dev/ScreenShot_2026-07-16_110028_647.png" />


## 使用要求
- esp32及其变体
- esp-idf框架

## 安装与使用
* yaml配置->引入外部组件：
  ```
  external_components:
    - source:
        type: git
        url: https://github.com/gasment/esphome_zw111_fingerprint_with_web_manager
        ref: v2
      components: [ zw111 ]
  ```

* yaml配置->uart组件
  ```
  uart:
    - id: zw111_uart #自定义ID
      tx_pin: GPIOx  #任意可用GPIO
      rx_pin: GPIOx  #任意可用GPIO
      baud_rate: 57600
      data_bits: 8
      stop_bits: 1
      parity: NONE
  ```

* yaml配置->zw111组件
  ```
  zw111:
    - id: zw111_fp  #自定义ID
      uart_id: zw111_uart  #对应上方uart ID
      web_port: 8088  #自定义web访问端口
      web_username: admin  #自定义web登录用户
      web_password: admin  #自定义web登录密码
      power_ctl_pin: GPIO10  #VCC供电控制，任意可用GPIO
      touch_sense_pin: GPIO6  #触摸反馈，任意可用GPIO

      connection_status:
        id: zw111_connection_status

      sensor_check_status:
        id: zw111_sensor_check
      
      fp_identify_action:
        id: zw111_fp_identify_action

      identify_button:
        id: zw111_identify_button

      sleep_button:
        id: zw111_sleep_button

      touch_sensor:
        id: zw111_touch_sensor
        filters:
          - delayed_off: 1000ms
  ```
* yaml配置->自动化示例
  - `fp_identify_action` 是一个 `text_sensor`，可以在 YAML 中配置 `on_value` 触发器：
    ```
    fp_identify_action:
      name: "Fingerprint ID"
      id: zw111_identify_action
      on_value:
        then:
          - if:
              condition:
                lambda: 'return x == "myfinger";'
              then:
                - switch.toggle: my_switch
    ```



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

访问 `http://device_ip:xxxx`进入（AP模式默认为`http://192.168.4.1:xxxx`）：

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

### 数据存储
- 录入的指纹位于zw111内置Flash，更换主控或清空主控flash，不会丢失已录入的指纹
- 指纹备注和配置数据存储在主控NVS分区，更换主控或清空主控flash会丢失以上信息
- 触发sleep睡眠，会在主控的RTC内存写入睡眠标志，用于深度睡眠唤醒时的zw111快速初始化

