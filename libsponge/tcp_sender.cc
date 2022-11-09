#include "tcp_sender.hh"

#include "tcp_config.hh"

#include <random>

// Dummy implementation of a TCP sender

// For Lab 3, please replace with a real implementation that passes the
// automated checks run by `make check_lab3`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

//! \param[in] capacity the capacity of the outgoing byte stream
//! \param[in] retx_timeout the initial amount of time to wait before retransmitting the oldest outstanding segment
//! \param[in] fixed_isn the Initial Sequence Number to use, if set (otherwise uses a random ISN)
TCPSender::TCPSender(const size_t capacity, const uint16_t retx_timeout, const std::optional<WrappingInt32> fixed_isn)
    : _isn(fixed_isn.value_or(WrappingInt32{random_device()()}))
    , _initial_retransmission_timeout{retx_timeout}
    , _stream(capacity) {}

uint64_t TCPSender::bytes_in_flight() const { return _bytes_in_flight; }

void TCPSender::fill_window() {
    TCPSegment _tcp_segment;
    if (_has_fin) {
        return;
    }
    if (!_has_isn) {
        _has_isn = true;
        _tcp_segment.header().syn = true;
        send_tcp_segment(_tcp_segment);
        // cerr << "payload " << _tcp_segment.payload().str();
        return;
    }
    // if (!_segments_outstanding.empty() && _segments_outstanding.front().header().syn)
    // {
    //     return;
    // }
    // If _stream is empty but input has not ended, do nothing.
    if (!_stream.buffer_size() && !_stream.eof())
    {
        // Lab4 behavior: if incoming_seg.length_in_sequence_space() is not zero, send ack.
        return;
    }

    uint16_t _window_size = (_receiver_window_size > 0 ? _receiver_window_size : 1); //!!!
    //! stream is empty and the payload is full, but the fin is not sent
    if (_stream.eof() && _window_size - _next_seqno + _receive_ackno > 0) {
        _tcp_segment.header().fin = true;
        _has_fin = true;
        send_tcp_segment(_tcp_segment);
        return;
    }
    //! continuely sned segments until stream is empty
    while (!_stream.buffer_empty() && _window_size - _next_seqno + _receive_ackno > 0) {
        //! receiver window is full, break the loop
        // if (_window_size - _next_seqno + _receive_ackno < 1) {
        //     break;
        // }
        size_t _payload_size = min({TCPConfig::MAX_PAYLOAD_SIZE, _stream.buffer_size(),
                                    static_cast<size_t>(_window_size - _next_seqno + _receive_ackno)});
        _tcp_segment.payload() = Buffer(_stream.read(_payload_size));
        //! judge the situation where fin can be sent
        if (_stream.eof() && _window_size > _tcp_segment.length_in_sequence_space()) {
            _tcp_segment.header().fin = true;
            _has_fin = true;
        }
        // cerr << "payload " << _tcp_segment.payload().str();
        send_tcp_segment(_tcp_segment);
    }
}

//! \param ackno The remote receiver's ackno (acknowledgment number)
//! \param window_size The remote receiver's advertised window size
void TCPSender::ack_received(const WrappingInt32 ackno, const uint16_t window_size) {
    // DUMMY_CODE(ackno, window_size);
    uint64_t _abs_ackno = unwrap(ackno, _isn, _next_seqno);
    // if (!ackno_vaild(_abs_ackno)) {
    //     // cout<<"DEBUG::error"<<endl;
    //     return;
    // }
    if (_abs_ackno > _next_seqno) {
        return;
    }
    if (_abs_ackno >= _receive_ackno) {
        _receiver_window_size = window_size;
        _receive_ackno = _abs_ackno;
    }
    bool _has_pop_segments = false;
    while (!_segments_outstanding.empty()) {
        TCPSegment _unackno_seg = _segments_outstanding.front();
        uint64_t _abs_unackno_seg_seqno = unwrap(_unackno_seg.header().seqno, _isn, _next_seqno);
        if (_abs_unackno_seg_seqno + _unackno_seg.length_in_sequence_space() - 1 < _abs_ackno) {
            _segments_outstanding.pop();
            _bytes_in_flight -= _unackno_seg.length_in_sequence_space();
            _retransmission_time_elapsed = 0;
            _retansmission_consecutive = 0;
            _rto = _initial_retransmission_timeout;

            _has_pop_segments = true;
        }
        else {
            break;
        }
    }
    if (_has_pop_segments) {
        fill_window();
    }
    if (_segments_outstanding.empty()) {
        _retransmission_running = false;
    }
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void TCPSender::tick(const size_t ms_since_last_tick) {
    // DUMMY_CODE(ms_since_last_tick);
    if (!_retransmission_running) {
        return;
    }
    _retransmission_time_elapsed += ms_since_last_tick;

    if (_retransmission_time_elapsed >= _rto) {
        TCPSegment _resend_seg = _segments_outstanding.front();
        _segments_out.push(_resend_seg);
        if (_receiver_window_size || _resend_seg.header().syn) {
            _retansmission_consecutive += 1;
            _rto <<= 1;
        }
        _retransmission_time_elapsed = 0;
    }
}

unsigned int TCPSender::consecutive_retransmissions() const { return _retansmission_consecutive; }

void TCPSender::send_empty_segment() {
    TCPSegment seg;
    seg.header().seqno = wrap(_next_seqno, _isn);
    _segments_out.push(seg);
}

// bool TCPSender::ackno_vaild(uint64_t _abs_ackno) {
//     if (_segments_outstanding.empty()) {
//         if (_abs_ackno <= _next_seqno) {
//             return true;
//         }
//     }
//     if (unwrap(_segments_outstanding.front().header().seqno, _isn, _next_seqno) <= _abs_ackno && _abs_ackno <= _next_seqno) {
//         return true;
//     }
//     return false;
// }

void TCPSender::send_tcp_segment(TCPSegment &seg) {
    seg.header().seqno = wrap(_next_seqno, _isn);
    _segments_out.push(seg);
    _segments_outstanding.push(seg);
    _next_seqno += seg.length_in_sequence_space();
    _bytes_in_flight += seg.length_in_sequence_space();
    //! if retransmission timer is false, just set it == true
    if (!_retransmission_running) {
        _retransmission_running = true;
        _retransmission_time_elapsed = 0;
    }
    // cerr << "payload " << seg.payload().str();
}
