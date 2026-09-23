// include/core/conversation.h
#pragma once

#include "core/message.h"
#include <cstddef>

// Growable array of Message, grown by doubling. This is the only class in
// the codebase permitted to call new[]/delete[] directly (spec 3.2).
class Conversation {
public:
    Conversation() noexcept = default;
    ~Conversation();

    Conversation(const Conversation& other);
    Conversation& operator=(const Conversation& other);

    Conversation(Conversation&& other) noexcept;
    Conversation& operator=(Conversation&& other) noexcept;

    void append(Message m);

    std::size_t size() const noexcept { return size_; }
    std::size_t capacity() const noexcept { return capacity_; }

    const Message& at(std::size_t i) const;

    const Message* begin() const noexcept { return data_; }
    const Message* end() const noexcept { return data_ + size_; }

private:
    Message* data_ = nullptr;
    std::size_t size_ = 0;
    std::size_t capacity_ = 0;

    void grow();
};
