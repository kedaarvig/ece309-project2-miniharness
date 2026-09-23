// src/sentinel_scanner.cpp

#include "core/sentinel_scanner.h"

SentinelScanner::Out SentinelScanner::feed(std::string_view chunk) {
    pending_.append(chunk.data(), chunk.size());

    std::size_t match = pending_.find(sentinel_);
    if (match != std::string::npos) {
        std::string safe = pending_.substr(0, match);
        pending_.clear();
        return {std::move(safe), true};
    }

    std::size_t hold_back = sentinel_.size() - 1;
    if (pending_.size() <= hold_back) {
        return {std::string(), false};
    }

    std::size_t safe_len = pending_.size() - hold_back;
    std::string safe = pending_.substr(0, safe_len);
    pending_.erase(0, safe_len);
    return {std::move(safe), false};
}

SentinelScanner::Out SentinelScanner::flush() {
    std::string safe = std::move(pending_);
    pending_.clear();
    return {std::move(safe), false};
}
