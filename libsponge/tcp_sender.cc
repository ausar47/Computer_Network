#include "tcp_sender.hh"

#include "tcp_config.hh"

#include <random>

// Dummy implementation of a TCP sender

// For Lab 3, please replace with a real implementation that passes the
// automated checks run by `make check_lab3`.

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}

using namespace std;

//! \param[in] capacity the capacity of the outgoing byte stream
//! \param[in] retx_timeout the initial amount of time to wait before retransmitting the oldest outstanding segment
//! \param[in] fixed_isn the Initial Sequence Number to use, if set (otherwise uses a random ISN)
TCPSender::TCPSender(const size_t capacity, const uint16_t retx_timeout, const std::optional<WrappingInt32> fixed_isn)
        : _isn(fixed_isn.value_or(WrappingInt32{random_device()()}))
        , _initial_retransmission_timeout{retx_timeout}
        , _stream(capacity) {
    _retransmission_timeout = _initial_retransmission_timeout;
}

uint64_t TCPSender::bytes_in_flight() const { return _bytes_int_flight; }

void TCPSender::fill_window() {
    // 如果远程窗口大小为 0, 则把其视为 1 进行操作
    size_t window_size = _window_size ? _window_size : 1;
    // 循环填充窗口
    while (window_size > _bytes_int_flight) {
        // 尝试构造单个数据包
        // 如果此时尚未发送 SYN 数据包，则立即发送
        TCPSegment segment;
        if (!_set_syn_flag) {
            segment.header().syn = true;
            _set_syn_flag = true;
        }
        // 设置 seqno
        segment.header().seqno = next_seqno();

        // 装入 payload.
        const size_t payload_size = min(TCPConfig::MAX_PAYLOAD_SIZE, window_size - _bytes_int_flight - segment.header().syn);
        std::string payload = _stream.read(payload_size);

        /**
         * 读取好后，如果满足以下条件，则增加 FIN
         *  1. 从来没发送过 FIN
         *  2. 输入字节流处于 EOF
         *  3. window 减去 payload 大小后，仍然可以存放下 FIN
         */
        if (!_set_fin_flag && _stream.eof() && payload.size() + _bytes_int_flight < window_size)
            _set_fin_flag = segment.header().fin = true;

        segment.payload() = Buffer(std::move(payload));

        // 如果没有任何数据，则停止数据包的发送
        if (segment.length_in_sequence_space() == 0)
            break;

        // 如果没有正在等待的数据包，则重设更新时间
        if (_segments_in_flight.empty()) {
            _retransmission_timeout = _initial_retransmission_timeout;
            _retransmission_timer = 0;
        }

        // 发送
        _segments_out.push(segment);

        // 追踪这些数据包
        _bytes_int_flight += segment.length_in_sequence_space();
        _segments_in_flight.insert(make_pair(_next_seqno, segment));
        // 更新待发送 abs seqno
        _next_seqno += segment.length_in_sequence_space();

        // 如果设置了 fin，则直接退出填充 window 的操作
        if (segment.header().fin)
            break;
    }
}

//! \param ackno The remote receiver's ackno (acknowledgment number)
//! \param window_size The remote receiver's advertised window size
void TCPSender::ack_received(const WrappingInt32 ackno, const uint16_t window_size) {
    bool has_set_flag = false;
    size_t abs_seqno = unwrap(ackno, _isn, _next_seqno);
    // 如果传入的 ack 是不可靠的，则直接丢弃
    if (abs_seqno > _next_seqno)
        return;
    // 遍历数据结构，将已经接收到的数据包丢弃
    for (auto iter = _segments_in_flight.begin(); iter != _segments_in_flight.end();) {
        // 如果一个发送的数据包已经被成功接收
        const TCPSegment &seg = iter->second;
        if (iter->first + seg.length_in_sequence_space() <= abs_seqno) {
            _bytes_int_flight -= seg.length_in_sequence_space();
            iter = _segments_in_flight.erase(iter);

            if (!has_set_flag) {
                _retransmission_timeout = _initial_retransmission_timeout;
                _retransmission_timer = 0;
                _consecutive_retransmissions_count = 0;
                has_set_flag = true;
            }

        }
        else
            break;
    }
    _window_size = window_size;
    fill_window();
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void TCPSender::tick(const size_t ms_since_last_tick) {
    _retransmission_timer += ms_since_last_tick;

    auto iter = _segments_in_flight.begin();
    // 如果存在发送中的数据包，并且定时器超时
    if (iter != _segments_in_flight.end() && _retransmission_timer >= _retransmission_timeout) {
        _segments_out.push(iter->second);
        // 如果窗口大小不为0还超时，则说明网络拥堵
        if (_window_size > 0) {
            _retransmission_timeout *= 2;
            _consecutive_retransmissions_count++;
        }
        _retransmission_timer = 0;
    }
}

unsigned int TCPSender::consecutive_retransmissions() const { return _consecutive_retransmissions_count; }

void TCPSender::send_empty_segment() {
    TCPSegment segment;
    segment.header().seqno = next_seqno();
    _segments_out.push(segment);
}