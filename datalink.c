#include <stdio.h>
#include <string.h>

#include "datalink.h"
#include "protocol.h"

/* 协议参数 */
#define DATA_TIMER 2000              // 数据帧的超时时间(毫秒)
#define ACK_TIMER 300                // 确认帧ACK的超时时间(毫秒)
#define MAX_SEQ 63                   // 最大帧序号
#define NR_BUFS ((MAX_SEQ + 1) / 2)  // 滑动窗口大小


/* 数据类型定义 */
typedef unsigned char frame_kind;    // 帧类型
typedef unsigned char seq_nr;        // 序列号类型
typedef unsigned char packet[PKT_LEN]; // 数据包类型
typedef unsigned char boolean;       // 布尔类型

/* 帧结构定义 */
typedef struct {
    frame_kind kind;              // 帧类型: FRAME_DATA/FRAME_ACK/FRAME_NAK
    seq_nr ack;                   // 确认号(捎带确认)
    seq_nr seq;                   // 序列号
    unsigned char data[PKT_LEN];  // 数据字段
    unsigned int padding;         // 填充位
} Frame;

/* 全局状态变量 */
static int phl_ready = 0;   // 物理层是否准备好
static boolean no_nak = 1;  // 是否已发送NAK

/**
 * 递增序列号，实现循环序列号机制
 * 当序列号达到最大值(MAX_SEQ)时，重置为0
 *
 * @param seq_ptr 指向序列号的指针
 */
static void advance_sequence_number(seq_nr* seq_ptr) {
    // 确保输入参数有效
    if (seq_ptr == NULL) {
        return;
    }

    // 增加序列号，如果达到最大值则环回到0
    if (*seq_ptr < MAX_SEQ) {
        *seq_ptr += 1;
    }
    else {
        *seq_ptr = 0;
    }
}

/**
 * 判断序号b是否在序号范围[a,c)内(考虑循环)
 * @param a 下界(包含)
 * @param b 待判断序号
 * @param c 上界(不包含)
 * @return 如果b在[a,c)范围内返回1，否则返回0
 */
static boolean between(seq_nr a, seq_nr b, seq_nr c) {
    return ((a <= b && b < c) || (c < a && b < c) || (c < a && a <= b));
}

/**
 * 添加CRC校验和并通过物理层发送帧
 * @param frame 帧数据指针
 * @param len   帧数据长度(不含CRC)
 */
static void prepare_and_transmit_frame(unsigned char* frame, int len) {
    *(unsigned int*)(frame + len) = crc32(frame, len);  // 添加32位(4字节)CRC校验码
    send_frame(frame, len + 4);     // 通过物理层发送
    phl_ready = 0;                  // 假设物理层缓存已满，等待下一次就绪信号
}

/**
 * 构建并发送指定类型的帧(DATA/ACK/NAK)
 * @param frame_type  帧类型(FRAME_DATA/FRAME_ACK/FRAME_NAK)
 * @param frame_nr    帧序号(用于DATA帧)
 * @param rx_expected 接收窗口下限(用于生成确认号)
 * @param buffer      数据缓冲区(用于DATA帧)
 */
static void construct_and_send_frame(frame_kind frame_type, seq_nr frame_nr,seq_nr rx_expected, packet buffer[]) {
    Frame s;

    // 设置基本帧头信息
    s.kind = frame_type;
    s.seq = frame_nr;
    s.ack = (rx_expected + MAX_SEQ) % (MAX_SEQ + 1);  // 计算确认号
    stop_ack_timer();  // 由于捎带了确认，停止ACK定时器

    // 根据帧类型进行差异化处理
    switch (frame_type) {
        case FRAME_DATA:
            // 复制数据并启动计时器
            memcpy(s.data, buffer[frame_nr % NR_BUFS], PKT_LEN);
            dbg_frame("Send DATA %d %d, ID %d\n", s.seq, s.ack, *(short*)s.data);
            prepare_and_transmit_frame((unsigned char*)&s, 3 + PKT_LEN);
            start_timer(frame_nr, DATA_TIMER);
            break;
            
        case FRAME_ACK:
            // 发送ACK帧(只有2字节的有效载荷)
            dbg_frame("Send ACK  %d\n", s.ack);
            prepare_and_transmit_frame((unsigned char*)&s, 2);
            break;
            
        case FRAME_NAK:
            // 发送NAK帧，请求重传期望的帧
            dbg_frame("Send NAK  %d\n", rx_expected);
            no_nak = 0;  // 标记已发送NAK
            prepare_and_transmit_frame((unsigned char*)&s, 2);
            break;
    }
}

/**
 * 数据链路层主函数
 */
int main(int argc, char** argv) {
    /* 发送窗口变量 */
    seq_nr ack_expected = 0;        // 发送窗口下限 表示发送方期望收到的最小确认号  该变量跟踪最早发送但尚未确认的帧序号
    seq_nr next_frame_to_send = 0;  // 下一个待发送帧的序号 发送窗口的上限+1 发送窗口范围是从ack_expected到next_frame_to_send-1
    seq_nr nbuffered = 0;           // 已发送未确认的帧数量
    
    /* 接收窗口变量 */
    seq_nr frame_expected = 0;      // 接收窗口下限 表示接收方期望接收的下一个帧序号 用于按序递交数据给网络层
    seq_nr too_far = NR_BUFS;       // 接收窗口上限+1 初始设置为NR_BUFS(即(MAX_SEQ+1)/2) 限制接收方接收的帧的最大序号
    
    /* 数据缓冲区 */
    packet in_buf[NR_BUFS];         // 接收缓冲区
    packet out_buf[NR_BUFS];        // 发送缓冲区
    boolean arrived[NR_BUFS];       // 标记帧是否已到达
    
    /* 临时变量 */
    int event;                      // 事件类型
    int arg;                        // 事件参数
    Frame f;                        // 接收到的帧
    int len = 0;                    // 接收到的帧长度
    
    // 初始化
    protocol_init(argc, argv);
    lprintf("Designed by Ren Hao, build: " __DATE__"  "__TIME__"\n");
    
    // 初始化接收缓冲状态
    for (int i = 0; i < NR_BUFS; ++i) {
        arrived[i] = 0;
    }
    
    disable_network_layer();  // 初始禁用网络层

    // 主循环
    for (;;) {
        // 等待事件发生
        event = wait_for_event(&arg);

        switch (event) {
            case NETWORK_LAYER_READY:
                // 从网络层获取数据包并发送
                get_packet(out_buf[next_frame_to_send % NR_BUFS]);
                nbuffered++;
                construct_and_send_frame(FRAME_DATA, next_frame_to_send, frame_expected, out_buf);
                advance_sequence_number(&next_frame_to_send);
                break;

            case PHYSICAL_LAYER_READY:
                // 物理层准备好发送新帧
                phl_ready = 1;
                break;

            case FRAME_RECEIVED:
                // 接收帧并处理
                len = recv_frame((unsigned char*)&f, sizeof f);
                
                // CRC校验
                if (len < 6 || crc32((unsigned char*)&f, len) != 0) {
                    dbg_event("**** Receiver Error, Bad CRC Checksum\n");
                    // 发送NAK请求重传(如果尚未发送过)
                    if (no_nak) {
                        construct_and_send_frame(FRAME_NAK, 0, frame_expected, out_buf);
                    }
                    break;
                }
                
                // 处理不同类型的帧
                // 处理ACK帧
                if (f.kind == FRAME_ACK) {
                    dbg_frame("Recv ACK  %d\n", f.ack);
                }
                
                // 处理数据帧
                if (f.kind == FRAME_DATA) {
                    dbg_frame("Recv DATA %d %d, ID %d\n", f.seq, f.ack, *(short*)f.data);
                    
                    // 收到不在期望序号的帧且尚未发送NAK
                    if (f.seq != frame_expected && no_nak) {
                        dbg_event("Recv frame is not lower bound, NAK sent back\n");
                        construct_and_send_frame(FRAME_NAK, 0, frame_expected, out_buf);
                    }
                    
                    // 判断帧是否在接收窗口内且该位置尚未被占用
                    if (between(frame_expected, f.seq, too_far) && !arrived[f.seq % NR_BUFS]) {
                        // 缓存帧数据
                        arrived[f.seq % NR_BUFS] = 1;
                        memcpy(in_buf[f.seq % NR_BUFS], f.data, len - 7);
                        
                        // 尝试向上递交连续的帧
                        while (arrived[frame_expected % NR_BUFS]) {
                            dbg_event("Put packet to network layer seq:%d, ID: %d\n", 
                                     frame_expected, *(short*)(in_buf[frame_expected % NR_BUFS]));
                            
                            // 向网络层递交数据
                            put_packet(in_buf[frame_expected % NR_BUFS], len - 7);
                            
                            // 更新状态
                            arrived[frame_expected % NR_BUFS] = 0;
                            no_nak = 1;
                            
                            // 移动接收窗口
                            advance_sequence_number(&frame_expected);
                            advance_sequence_number(&too_far);
                            
                            // 启动ACK计时器
                            start_ack_timer(ACK_TIMER);
                        }
                    }
                }
                
                // 处理NAK帧
                if (f.kind == FRAME_NAK && 
                    between(ack_expected, (f.ack + 1) % (MAX_SEQ + 1), next_frame_to_send)) {
                    dbg_frame("Recv NAK  %d --%dbyte\n", (f.ack + 1) % (MAX_SEQ + 1), len);
                    // 重传NAK指定的帧
                    construct_and_send_frame(FRAME_DATA, (f.ack + 1) % (MAX_SEQ + 1), 
                                          frame_expected, out_buf);
                }
                
                // 处理帧中的确认信息(累计确认)
                while (between(ack_expected, f.ack, next_frame_to_send)) {
                    nbuffered--;
                    stop_timer(ack_expected);
                    advance_sequence_number(&ack_expected);
                }
                break;

            case DATA_TIMEOUT:
                // 数据帧超时，重传
                dbg_event("---- DATA %d timeout, resend ----\n", arg);
                construct_and_send_frame(FRAME_DATA, arg, frame_expected, out_buf);
                break;

            case ACK_TIMEOUT:
                // 确认超时，发送单独的ACK
                construct_and_send_frame(FRAME_ACK, 0, frame_expected, out_buf);
                break;
        }

        // 流量控制：根据发送缓冲区状态控制网络层
        if (nbuffered < NR_BUFS && phl_ready)
            enable_network_layer();
        else
            disable_network_layer();
    }
}