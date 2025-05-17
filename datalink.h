
/* FRAME kind */
#define FRAME_DATA 1
#define FRAME_ACK  2
#define FRAME_NAK  3

/*  
    DATA Frame
    +=========+========+========+===============+========+
    | KIND(1) | SEQ(1) | ACK(1) | DATA(240~256) | CRC(4) |
    +=========+========+========+===============+========+

    ACK Frame
    +=========+========+========+
    | KIND(1) | ACK(1) | CRC(4) |
    +=========+========+========+

    NAK Frame
    +=========+========+========+
    | KIND(1) | ACK(1) | CRC(4) |
    +=========+========+========+
*/
     /*if (between(ack_expected, f.ack, next_frame_to_send)) {
                     acked[f.ack] = 1;  // 标记ACK已确认
                     stop_timer(f.ack);  // 停止ACK计时器
                    // 尝试滑动窗口（只有当最早的未确认帧被确认时才滑动）
                        while (acked[ack_expected]) {
                            // 释放资源
                            nbuffered--;
                            // 标记为未确认(为下一次循环使用)
                            acked[ack_expected] = 0;
                            // 滑动窗口
                            advance_sequence_number(&ack_expected);
                        }
                }*/   

