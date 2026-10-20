# UITouch3.5 UART 协议说明

权威协议规范位于 [MainControl UART 协议](../../MainControl/docs/UART_PROTOCOL.md)。MainControl 实际实现的字段约定由 `MainControl/src/uart_protocol_dispatch.cpp` 和 `MainControl/src/protocol_frame.*` 定义。协议变更应优先修改上述实现和主规范，本文不得成为另一份独立规范。

UART2 使用 115200 波特率、8N1、无流控的 ASCII 通信，行结束符为 `\r\n`：

```text
REQ <id> <operation> [key=value ...]
RSP <id> <operation> result=<ok|accepted|rejected|failed> code=<code> [key=value ...]
EVT <seq> <event> [key=value ...]
```

UITouch 在队列请求首次发送时分配一个非零的 32 位请求 ID。重试时保持相同的 ID、操作名和参数。响应通过 ID 和操作名共同匹配。协议不包含版本字段或 CRC，也不会发送旧协议 UART 行。

双面煎蛋功率设置由 `config.set key=power_offset_first`（第一面/B锅）和 `config.set key=power_offset_second`（第二面/A锅）分别调整，范围均为 `50..500`。旧固件的 `power_offset` 配置响应会被视为两面相同；旧命令 `PWO` 仍设置两面，新命令 `PWO1`/`PWO2` 分别设置第一面/第二面。

当前通信桥使用以下操作组：

- 生产：`state.get`、`config.get`、`config.set`、`maker.start`、`maker.stop`、`maker.recover`、`quantity.set`、`quantity.add`、`quantity.sub`、`error.clear`
- 调试：`debug.get`、`debug.set`、`debug.run`、`debug.stress`、`pot.debug.get`
- 错误历史：`error_log.get`、`error_log.clear`
- 太阳蛋：`sun.state.get`、`sun.config.get`、`sun.config.set`、`sun.clear`、`sun.select`、`sun.start`、`sun.stop`
- 系统：`system.restart`

仅为兼容现有 UI 页面解析器，通信桥在记录原始 `RSP`/`EVT` 后，可能向内部历史记录添加 `MEAS`、`STAT`、`ELG`、`ELE` 或 `PDBG` 等兼容行。这些兼容行绝不会写入 UART。