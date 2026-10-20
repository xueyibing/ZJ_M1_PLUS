# MainControl UART 协议

## 1. 传输层

- UART：115200 波特率，8 个数据位，无校验，1 个停止位，无流控。
- 编码：ASCII。
- 行结束符：`\r\n`。
- 请求行最大长度：384 字节，不包含 `\r\n`。超长行会被丢弃，直至收到下一个 `\n`，且不会返回响应。
- 本协议为一次性升级。MainControl 只接受本文描述的新协议，不接受 `HET 1`、`STA 1`、`TST ...`、`SUN ...` 等旧协议命令行。
- 每帧不包含版本字段，也不使用 CRC。

## 2. 帧格式

请求：

```text
REQ <id> <operation> [key=value ...]\r\n
```

响应：

```text
RSP <id> <operation> result=<ok|accepted|rejected|failed> code=<code> [key=value ...]\r\n
```

事件：

```text
EVT <seq> <event> [key=value ...]\r\n
```

规则：

- `id` 是取值范围为 `1..4294967295` 的 32 位无符号十进制整数。
- `operation` 区分大小写且不能为空。本文中的操作名使用小写点分命名。
- `operation` 后的每个参数都必须是一个非空的 `key=value` 标记，不允许出现重复键。
- 十进制数采用严格解析。只有有符号值允许使用正负号；尾随文本、标记内空白、十六进制格式和数值溢出均会被拒绝。
- 未知参数或多余参数返回 `code=invalid_argument`。
- 对于格式错误但仍能生成响应的请求，返回 `result=rejected code=invalid_argument`。无法获得有效 ID 时，诊断响应使用 ID `0` 和操作名 `protocol`。
- 未知操作返回 `result=rejected code=unsupported_operation`。
- `result=accepted` 表示命令已被接受，但停机、重启或生产流程可能仍在异步执行。
- `code=none` 表示操作成功且没有附加状态。

## 3. 请求去重

MainControl 使用固定环形缓存保存最近完成的 16 个响应。

- 使用相同 `id` 和 `operation` 重复请求时，重新发送缓存的最终 `RSP`，不再次执行操作。
- 缓存中的 `id` 被用于不同操作时，返回 `result=rejected code=id_conflict`。
- 请求身份有意采用 `id + operation` 判定，因此重试时必须保留原请求的全部参数。
- 只缓存最终响应，不重放事件。
- 缓存项移出 16 条响应环形缓存后，其 ID 可以再次使用。

## 4. 生产状态与配置

### `state.get`

```text
REQ 1 state.get
REQ 2 state.get epoch=1787821200
```

`epoch` 为可选的 UI 当前 Unix 时间。提供有效时间时，主控会同步预约调度时钟；不提供时仍可读取状态，但主控重启后在首次校时前不会触发预约。

成功的 `RSP` 包含以下字段：

| 字段 | 含义 |
| --- | --- |
| `start` | 煎蛋流程是否运行，`0` 或 `1` |
| `err` | 当前 `ErrorCode` 整数值 |
| `need` | 待制作数量，向上取整为整枚鸡蛋 |
| `remaining_s` | 当前剩余时间，单位为秒 |
| `total_make_s` | 当前单面制作总时间，单位为秒 |
| `maker_state` | 当前 `MakerState` 整数值 |
| `pot_b_temp_c`、`pot_a_temp_c` | 锅体温度，单位为摄氏度 |
| `pot_b_igbt_c`、`pot_a_igbt_c` | IGBT 温度，单位为摄氏度 |
| `pot_b_dw`、`pot_a_dw` | 当前硬件加热档位 |
| `pot_b_heat_remaining_s`、`pot_a_heat_remaining_s` | 剩余加热时间 |
| `debug_bypass_mask` | 当前调试旁路位掩码 |
| `motion_no_heat` | 仅运动、不加热模式，`0` 或 `1` |
| `reservation_active` | 是否存在待执行预约，`0` 或 `1` |
| `reservation_epoch` | 预约执行 Unix 时间；无预约时为 `0` |
| `reservation_qty` | 预约制作数量 |
| `reservation_status` | `0` 无预约、`1` 待执行、`2` 已启动、`3` 已过期、`4` 急停未恢复、`5` 设备错误 |
| `clock_valid` | 主控是否已从 UI 获得有效时钟，`0` 或 `1` |

### 预约制作

```text
REQ 5 reservation.set epoch=1787821200 qty=3 oil=1 doneness=2
REQ 6 reservation.cancel
```

- `reservation.set` 保存绝对执行时间、数量、油量和熟度快照到主控 NVS。执行时间必须至少晚于当前时间 30 秒，且不能超过 7 天。
- 主控到时后再次检查急停、错误和生产状态。设备忙时会在预约时间后继续等待，最长 5 分钟；超过后标记为过期，不会延迟补做。
- `reservation.cancel` 清除待执行预约。重复取消是幂等操作。
- 状态变化会发送 `reservation.scheduled`、`reservation.cancelled`、`reservation.started`、`reservation.expired` 或 `reservation.failed` 事件。

### `config.get`

```text
REQ 2 config.get
```

成功的 `RSP` 包含 `oil`、`doneness`、`oil_offset`、`doneness_offset`、`max_temp_c`、`power_offset`、`power_offset_first`、`power_offset_second`、`init_dw`、`laser_check`、`second_oil`、`debug_bypass_mask` 和 `motion_no_heat`。双面煎蛋中，`power_offset_first` 调节第一面（B锅），`power_offset_second` 调节第二面（A锅）；兼容字段 `power_offset` 等于第一面值。`second_oil` 表示A锅第二面开煎前是否补油，默认开启；设置保存在主控 NVS。太阳蛋仍使用独立配置。

### `config.set`

```text
REQ 3 config.set key=oil value=1
```

支持的键及其允许值：

| 键 | 范围 |
| --- | --- |
| `oil` | `0..2` |
| `doneness` | `0..3`（0=全熟，1=糖心，2=软嫩，3=太阳蛋） |
| `oil_offset` | `60..200` |
| `doneness_offset` | `60..200` |
| `max_temp_c` | `180..300` |
| `power_offset` | `50..500`；旧兼容键，设置时同时应用于两面 |
| `power_offset_first` | `50..500`；第一面/B锅降档间隔倍率 |
| `power_offset_second` | `50..500`；第二面/A锅降档间隔倍率 |
| `init_dw` | `4..8` |
| `laser_check` | `0` 或 `1` |
| `second_oil` | `0` 或 `1`；是否启用双面煎蛋第二面给油 |

### 制作流程控制

```text
REQ 10 maker.start
REQ 11 maker.stop
REQ 12 maker.recover
```

- `maker.start`：清除之前的煎蛋错误并启动现有制作流程。返回 `accepted`；适用时可能以 `busy_sun` 或 `recovery_required` 拒绝。
- `maker.stop`：请求执行现有紧急停止和退蛋流程。在所有协作任务完全结束前即返回 `accepted`。
- `maker.recover`：在所有受管操作停止后确认恢复。停机尚未完成时以 `stopping` 拒绝。

### 数量与错误

```text
REQ 20 quantity.set value=5
REQ 21 quantity.add value=1
REQ 22 quantity.sub value=1
REQ 23 error.clear
```

- `quantity.set`：在空闲状态下将待制作数量设置为绝对值，范围为 `0..6`。制作中以 `code=running` 拒绝。该操作是幂等的，适用于“选择数量”后开始制作。
- `quantity.add`、`quantity.sub`：相对增减待制作数量，值必须是正的 32 位有符号整数，结果统一限制在 `0..6`，适用于制作过程中追加或取消后续数量。
- 所有数量操作的成功响应都包含变更后的 `need` 值。
- `error.clear` 只清除当前煎蛋错误。

## 5. 调试控制

### `debug.get`

返回 `bypass`、`mask`、`no_heat`、`no_heat_a` 和 `no_heat_b`。

### `debug.set`

```text
REQ 30 debug.set key=mask value=3
```

| 键 | 范围/作用 |
| --- | --- |
| `bypass` | `0` 或 `1`，关闭/开启旁路行为 |
| `mask` | `0..63`，旁路位掩码 |
| `no_heat` | `0` 或 `1`，保留运动的同时开启/关闭加热 |
| `all` | `0` 或 `1`，同时设置旁路和不加热模式 |

任一生产流程运行时，修改请求以 `code=busy` 拒绝。成功修改的旁路位掩码和不加热模式会写入主控 NVS，并在主控重新上电或重启后自动恢复。

### `debug.run`

```text
REQ 40 debug.run action=DWA arg1=5 arg2=30
```

该操作取代旧协议的 `TST`。`arg1` 和 `arg2` 保留原有位置参数的含义：

| 动作 | 参数 |
| --- | --- |
| `HOME` | 无 |
| `DWA`、`DWB` | `arg1=dw`（`0..8`），`arg2=seconds`（`>=0`） |
| `GIVEOIL` | 可选 `arg1=milliseconds`（`>0`） |
| `TRANS`、`PUSH`、`POTA`、`POTB` | `arg1=angle` |
| `FAN` | 可选 `arg1=speed`；省略时切换状态，`0` 表示停止 |
| `CUR` | `arg1=<PUSH|TRANS|POTB|POTA>`，`arg2=current_ma`（`0..65535`） |
| `MST` | `arg1=<PUSH|TRANS|POTB|POTA>`，`arg2=<8|16|32|64|128|256>` |
| `BACKOIL`、`GIVEEGG`、`GIVEEGGOPEN`、`GIVEEGGCLOSE`、`BEAT` | 无 |
| `GO_POTB`、`GO_POTA`、`SERVO_A`、`SERVO_B`、`SERVO_ZERO` | 无 |
| `TRANS_DUMP`、`TRANS_DUMP_RETRY`、`SHAKE_EGG` | 无 |
| `PUSHGO`、`PUSHSTEP`、`PUSHBACK`、`PUSHSHAKE` | 无 |
| `TURNA`、`TURNB` | 无 |
| `PUBS`、`TRANSS`、`OILS` | 无；发送 `EVT ... debug.status name=<action> value=<0|1>` |
| `DIST`、`ENV_TMP`、`MENV_TMP` | 无；发送 `EVT ... debug.measure name=<name> value=<n>` |

每个可识别动作最终都会返回一个 `RSP`。硬件动作失败时返回 `result=failed code=action_failed`；未知动作返回 `result=rejected code=unsupported_action`。通过此操作时，UART2 不会发送旧协议的 `MEAS`、`STAT` 或 `TST` 行。

### `debug.stress`

```text
REQ 50 debug.stress action=GIVEEGG
```

支持的动作包括 `GIVEEGG`、`BEAT`、`TRANS`、`TURN_POTB`、`TURN_POTA` 和 `GIVE_OIL`。该操作取代旧协议的 `STS`，返回 `result=ok`，或返回 `result=failed` 以及 `code=action_failed`/`unsupported_action`。

## 6. 错误日志

```text
REQ 60 error_log.get
REQ 61 error_log.clear
```

`error_log.get` 首先返回 `RSP ... count=n`，随后为每条可用记录发送一个事件：

```text
EVT <seq> error_log.entry request_id=<id> index=<index> seq=<log_seq> code=<error_code> boot_id=<boot_id> uptime_s=<seconds>
```

记录沿用数据管理器现有的从新到旧顺序。`error_log.clear` 清除持久化错误历史。

## 7. 太阳蛋流程

太阳蛋按数量制作：1 个固定使用 B 锅，2 个使用 B 锅和 A 锅。旧串口命令使用 `SUN QTY <1|2>` 设置数量，不再允许用户选择锅位。

| 新操作 | 参数/响应 |
| --- | --- |
| `sun.state.get` | 返回 `start`、`err`、`quantity`、`maker_state`、`remaining_s`、`total_make_s`，以及两口锅的温度、DW 档位和加热时间 |
| `sun.config.get` | 返回 `oil`、`doneness`、偏移量、`max_temp_c`、`power_offset`、`max_dw`、`power_dw_fall_gap_s` |
| `sun.clear` | 清除数量和当前错误；运行期间以 `running` 拒绝 |
| `sun.quantity.set` | `value=<1|2>`；1 个映射到 B 锅，2 个映射到 B 锅和 A 锅 |
| `sun.start` | 要求数量已经设置且没有普通煎蛋任务，返回 `accepted` |
| `sun.stop` | 停止现有太阳蛋流程，返回 `accepted` |
| `sun.config.set` | `key=<key> value=<n>`；运行期间以 `running` 拒绝 |

`sun.config.set` 支持的键包括：`oil`（`0..2`），`doneness`（`0..3`），`oil_offset`/`doneness_offset`（`60..200`），`max_temp_c`（`180..240`），以及 `power_offset`（`50..500`）。太阳蛋完成后不执行自动出蛋，用户需要从锅中夹取。

## 8. 锅具诊断

```text
REQ 70 pot.debug.get
```

该操作取代旧协议的 `HET 2`/`PDBG`。响应使用以下命名字段：

```text
pot_a_addr pot_a_temp_ad pot_a_temp_c pot_a_igbt_ad pot_a_igbt_c
pot_a_voltage pot_a_current pot_a_ppg pot_a_state
pot_b_addr pot_b_temp_ad pot_b_temp_c pot_b_igbt_ad pot_b_igbt_c
pot_b_voltage pot_b_current pot_b_ppg pot_b_state
```

## 9. 系统重启

```text
REQ 80 system.restart
```

MainControl 在调用 `esp_restart()` 前，先发送并刷新 `RSP ... result=accepted code=none`。

## 10. 常用返回码

| 返回码 | 含义 |
| --- | --- |
| `none` | 操作成功 |
| `invalid_argument` | 参数缺失、多余、格式错误、重复或超出范围 |
| `unsupported_operation` | 未知操作 |
| `unsupported_action` | 未知调试动作 |
| `id_conflict` | 缓存中的 ID 被用于不同操作 |
| `busy`、`busy_sun`、`busy_jiandan` | 存在正在运行或已选择的冲突流程 |
| `recovery_required`、`stopping` | 紧急停止恢复状态阻止当前请求 |
| `running` | 太阳蛋运行期间尝试修改配置或清除状态 |
| `no_selection` | 未选择锅时请求启动太阳蛋 |
| `action_failed` | 同步调试/压力测试动作执行失败 |
| `response_too_large` | 触发内部固定响应缓冲区保护 |
