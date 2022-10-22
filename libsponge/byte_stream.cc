#include "byte_stream.hh"

// Dummy implementation of a flow-controlled in-memory byte stream.

// For Lab 0, please replace with a real implementation that passes the
// automated checks run by `make check_lab0`.

// You will need to add private members to the class declaration in `byte_stream.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

ByteStream::ByteStream(const size_t capacity) { length = capacity; }

size_t ByteStream::write(const string &data) {
    size_t new_length = byte_stream.length() + data.length();
    if (new_length > length) {
        string temp = data.substr(0, length - byte_stream.length());
        byte_stream += temp;
        byteswritten += temp.length();
        return temp.length();
    } else {
        byte_stream += data;
        byteswritten += data.length();
        return data.length();
    }
}

//! \param[in] len bytes will be copied from the output side of the buffer
string ByteStream::peek_output(const size_t len) const {
    string temp;
    if (len > byte_stream.length())
        temp = byte_stream;
    else
        temp = byte_stream.substr(0, len);
    return temp;
}

//! \param[in] len bytes will be removed from the output side of the buffer
void ByteStream::pop_output(const size_t len) {
    if (len > byte_stream.length()) {
        bytesread += byte_stream.length();
        byte_stream = "";
    }
    else {
        byte_stream.erase(0, len);
        bytesread += len;
    }
}

//! Read (i.e., copy and then pop) the next "len" bytes of the stream
//! \param[in] len bytes will be popped and returned
//! \returns a string
std::string ByteStream::read(const size_t len) {
    string temp;
    if (len > byte_stream.length()) {
        temp = byte_stream;
        pop_output(byte_stream.length());
    }
    else {
        temp = peek_output(len);
        pop_output(len);
    }
    return temp;
}

void ByteStream::end_input() { is_end = true; }

bool ByteStream::input_ended() const { return is_end; }

size_t ByteStream::buffer_size() const { return byte_stream.size(); }

bool ByteStream::buffer_empty() const { return byte_stream.size() == 0; }

bool ByteStream::eof() const { return byte_stream.size() == 0 && is_end; }

size_t ByteStream::bytes_written() const { return byteswritten; }

size_t ByteStream::bytes_read() const { return bytesread; }

size_t ByteStream::remaining_capacity() const { return length - byte_stream.size(); }
