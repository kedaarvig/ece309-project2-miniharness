// include/core/sentinel_scanner.h
#pragma once

#include <string>
#include <string_view>

// Incrementally scans a chunked character stream for a fixed sentinel
// string that may be split arbitrarily across feed() calls, holding back
// at most sentinel.size() - 1 trailing bytes at any time.
class SentinelScanner {
public:
    explicit SentinelScanner(std::string sentinel) : sentinel_(std::move(sentinel)) {}

    struct Out {
        std::string safe_text;
        bool sentinel_found;
    };

    Out feed(std::string_view chunk);
    Out flush();

    // Exposed for the bounded-memory test (spec 5, item 9); not required by
    // the public interface the harness itself calls against.
    std::size_t pending_size() const noexcept { return pending_.size(); }

private:
    std::string sentinel_;
    std::string pending_;
};
