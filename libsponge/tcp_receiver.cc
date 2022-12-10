#include "tcp_receiver.hh"

// Dummy implementation of a TCP receiver

// For Lab 2, please replace with a real implementation that passes the
// automated checks run by `make check_lab2`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

void TCPReceiver::segment_received(const TCPSegment &seg) {
    if (!_set_syn_flag) {
        if (!seg.header().syn)
            return;
        _set_syn_flag = true;
        _isn = seg.header().seqno;
    }
    uint64_t absolute_ackno = _reassembler.stream_out().bytes_written() + 1;
    uint64_t seg_absolute_seqno = unwrap(seg.header().seqno, _isn, absolute_ackno);
    uint64_t stream_index = seg_absolute_seqno - 1 + seg.header().syn;
    _reassembler.push_substring(seg.payload().copy(), stream_index, seg.header().fin);
}

optional<WrappingInt32> TCPReceiver::ackno() const {
    if (!_set_syn_flag)
        return nullopt;
    uint64_t absolute_ackno = _reassembler.stream_out().bytes_written() + 1;
    if (_reassembler.stream_out().input_ended())
        absolute_ackno++;
    return wrap(absolute_ackno, _isn);
}

size_t TCPReceiver::window_size() const { return _capacity - _reassembler.stream_out().buffer_size(); }
