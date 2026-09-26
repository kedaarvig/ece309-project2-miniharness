// src/conversation.cpp

#include "core/conversation.h"
#include <stdexcept>
#include <utility>

namespace {

// Allocates n Messages and copies src[0..n) into them. If any copy throws,
// the new buffer is released before the exception propagates, so a failed
// copy never leaks.
Message* clone_buffer(const Message* src, std::size_t n) {
    if (n == 0) return nullptr;
    Message* buf = new Message[n];
    try {
        for (std::size_t i = 0; i < n; ++i) buf[i] = src[i];
    } catch (...) {
        delete[] buf;
        throw;
    }
    return buf;
}

}  // namespace

Conversation::~Conversation() {
    delete[] data_;
}

Conversation::Conversation(const Conversation& other)
    : data_(clone_buffer(other.data_, other.size_)),
      size_(other.size_),
      capacity_(other.size_) {}

Conversation& Conversation::operator=(const Conversation& other) {
    if (this == &other) return *this;

    Message* new_data = clone_buffer(other.data_, other.size_);

    delete[] data_;
    data_ = new_data;
    size_ = other.size_;
    capacity_ = other.size_;
    return *this;
}

Conversation::Conversation(Conversation&& other) noexcept
    : data_(other.data_), size_(other.size_), capacity_(other.capacity_) {
    other.data_ = nullptr;
    other.size_ = 0;
    other.capacity_ = 0;
}

Conversation& Conversation::operator=(Conversation&& other) noexcept {
    if (this == &other) return *this;

    delete[] data_;
    data_ = other.data_;
    size_ = other.size_;
    capacity_ = other.capacity_;

    other.data_ = nullptr;
    other.size_ = 0;
    other.capacity_ = 0;
    return *this;
}

void Conversation::grow() {
    std::size_t new_capacity = (capacity_ == 0) ? 1 : capacity_ * 2;
    Message* new_data = new Message[new_capacity];
    try {
        for (std::size_t i = 0; i < size_; ++i) {
            new_data[i] = std::move(data_[i]);
        }
    } catch (...) {
        delete[] new_data;
        throw;
    }
    delete[] data_;
    data_ = new_data;
    capacity_ = new_capacity;
}

void Conversation::append(Message m) {
    if (size_ == capacity_) grow();
    data_[size_++] = std::move(m);
}

const Message& Conversation::at(std::size_t i) const {
    if (i >= size_) throw std::out_of_range("Conversation::at: index out of range");
    return data_[i];
}
