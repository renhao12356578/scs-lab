根据你提供的 `datalink.c` 文件内容，下面是对**2.2 数据结构**、**2.3 模块结构**、**2.4 算法流程**的分析：

---

## 2.2 数据结构

### 1. 基本类型定义
- `frame_kind`：帧类型（`unsigned char`），用于区分数据帧、确认帧、否认帧（NAK）。
- `seq_nr`：序列号类型（`unsigned char`），用于帧的编号，实现滑动窗口协议。
- `packet`：数据包类型（`unsigned char[PKT_LEN]`），用于存储从网络层接收或发送的数据。
- `boolean`：布尔类型（`unsigned char`），用于逻辑判断。
## 1. MAX_SEQ (最大序列号)
- **原理**：增大序列号空间可防止序列号过快回绕，降低"序列号混淆"风险
- **限制**：序列号字段长度（通常为8位，最大值255）

### 2. 帧结构体
```c
typedef struct {
    frame_kind kind;              // 帧类型: FRAME_DATA/FRAME_ACK/FRAME_NAK
    seq_nr ack;                   // 确认号(捎带确认)
    seq_nr seq;                   // 序列号
    unsigned char data[PKT_LEN];  // 数据字段
    unsigned int padding;         // 填充位
} Frame;
```
- 用于封装数据链路层的各种帧，支持数据、确认、否认三种类型。

### 3. 全局状态变量
- `phl_ready`：物理层是否准备好发送数据。
- `no_nak`：是否已发送NAK，防止重复发送NAK。

### 4. 窗口与缓冲区
- `ack_expected`、`next_frame_to_send`、`nbuffered`：发送窗口相关变量。
- `frame_expected`、`too_far`：接收窗口相关变量。
- `in_buf[NR_BUFS]`、`out_buf[NR_BUFS]`：接收和发送缓冲区。
- `arrived[NR_BUFS]`：标记接收缓冲区中帧是否到达。

---
## 优化建议汇总
1. **带宽较高场景**：
   - 增大MAX_SEQ 
   - 增大NR_BUFS (与带宽延迟积匹配)
   - DATA_TIMER设为稳健的RTT估计值
   - 启用累积确认和选择性重传

## 2.3 模块结构

### 1. 辅助函数
- `advance_sequence_number(seq_nr*)`：循环递增序列号。
- `between(seq_nr a, seq_nr b, seq_nr c)`：判断序号b是否在[a, c)区间（循环序号）。
- `prepare_and_transmit_frame(unsigned char*, int)`：添加CRC并通过物理层发送帧。
- `construct_and_send_frame(frame_kind, seq_nr, seq_nr, packet[])`：构建并发送指定类型的帧。

### 2. 主控流程
- `main()`：数据链路层主循环，负责事件驱动的协议处理。

### 3. 事件处理
- `NETWORK_LAYER_READY`：网络层有数据可发。
- `PHYSICAL_LAYER_READY`：物理层可发送新帧。
- `FRAME_RECEIVED`：收到新帧，进行CRC校验、窗口判断、缓存、递交等处理。
- `DATA_TIMEOUT`：数据帧超时，重传。
- `ACK_TIMEOUT`：ACK超时，单独发送ACK。

### 4. 物理层/网络层接口
- `send_frame`、`recv_frame`、`enable_network_layer`、`disable_network_layer`、`get_packet`、`put_packet` 等。

---

## 2.4 算法流程

### 1. 初始化
- 初始化协议参数、缓冲区、窗口变量。
- 禁用网络层，等待物理层准备。

### 2. 主循环（事件驱动）
- 通过 `wait_for_event` 等待事件发生，根据事件类型分支处理。

#### 2.1 发送数据
- 网络层准备好数据时，从网络层取包，缓存到发送缓冲区，构建并发送数据帧，更新窗口和计数。

#### 2.2 物理层准备
- 物理层准备好后，允许网络层继续发送。

#### 2.3 接收帧
- 收到帧后，先做CRC校验，错误则发送NAK。
- 正确则根据帧类型处理：
  - **ACK帧**：累计确认，滑动发送窗口，停止对应定时器。
  - **DATA帧**：判断是否在接收窗口内且未到达，缓存数据，递交连续数据到网络层，移动接收窗口，启动ACK定时器。
  - **NAK帧**：重传指定帧。
- 处理帧中的累计确认信息。

#### 2.4 超时处理
- 数据帧超时，重传对应帧。
- ACK超时，单独发送ACK帧。

#### 2.5 流量控制
- 根据发送窗口和物理层状态，动态使能/禁用网络层。

---

### 总结

本实现采用**选择重传ARQ（Selective Repeat ARQ）**协议，利用滑动窗口机制和事件驱动方式，实现了可靠的数据链路层通信。数据结构设计紧凑，模块划分清晰，算法流程严谨，能有效处理丢包、乱序、重复等链路层常见问题。
3. **高差错率场景**：
   - 使用较小的窗口尺寸
   - 更积极地使用NAK和选择性重传
   - 考虑较短的DATA_TIMER值
