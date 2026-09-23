// tests/p2/test_p2.cpp

#include "core/conversation.h"
#include "core/message.h"
#include "core/sentinel_scanner.h"
#include "harness/harness.h"
#include "model/replay_client.h"
#include "model/scripted_client.h"

#include <cassert>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr const char* kSentinel = "<|end_conversation|>";

// ---- test doubles for Harness --------------------------------------------

class ListInputSource : public InputSource {
public:
    explicit ListInputSource(std::vector<std::string> lines) : lines_(std::move(lines)) {}

    std::string read_line() override {
        if (index_ >= lines_.size()) {
            eof_ = true;
            return "";
        }
        return lines_[index_++];
    }

    bool is_eof() const override { return eof_; }

private:
    std::vector<std::string> lines_;
    std::size_t index_ = 0;
    bool eof_ = false;
};

class CapturingOutputSink : public OutputSink {
public:
    void write(std::string_view text) override { captured_ += text; }
    const std::string& captured() const { return captured_; }

private:
    std::string captured_;
};

void write_script(const std::string& path, const std::string& body) {
    std::ofstream f(path);
    f << body;
}

// Minimal transcript writer matching Appendix A's block format, used only
// to build fixtures for the round-trip test (main.cpp's writer isn't
// exported for reuse).
void write_transcript(const std::string& path, const Conversation& conv) {
    std::ofstream f(path);
    bool first = true;
    for (const Message* m = conv.begin(); m != conv.end(); ++m) {
        if (!first) f << "---\n";
        first = false;
        const char* role = m->role() == Role::System   ? "system"
                            : m->role() == Role::User   ? "user"
                                                          : "assistant";
        f << "role: " << role << "\n" << m->content() << "\n";
    }
}

}  // namespace

// ---- Conversation ----------------------------------------------------------

void test_empty_conversation_bounds() {
    Conversation conv;
    assert(conv.size() == 0);
    assert(conv.begin() == conv.end());

    bool threw = false;
    try {
        conv.at(0);
    } catch (const std::out_of_range&) {
        threw = true;
    }
    assert(threw);
}

void test_append_preserves_order() {
    Conversation conv;
    conv.append(Message(Role::User, "first"));
    conv.append(Message(Role::Assistant, "second"));
    conv.append(Message(Role::User, "third"));

    assert(conv.size() == 3);
    assert(conv.at(0).content() == "first");
    assert(conv.at(1).content() == "second");
    assert(conv.at(2).content() == "third");
}

void test_system_message_pinned_at_front() {
    Conversation conv;
    conv.append(Message(Role::System, "be concise"));
    for (int i = 0; i < 10; ++i) {
        conv.append(Message(Role::User, "turn"));
        conv.append(Message(Role::Assistant, "reply"));
    }

    assert(conv.at(0).role() == Role::System);
    assert(conv.at(0).content() == "be concise");
}

void test_copy_constructor_deep_copy() {
    Conversation original;
    original.append(Message(Role::User, "hello"));
    original.append(Message(Role::Assistant, "world"));

    Conversation copy(original);
    assert(copy.begin() != original.begin());
    assert(copy.size() == original.size());
    assert(copy.at(0).content() == "hello");
    assert(copy.at(1).content() == "world");

    // Mutating one must not affect the other.
    Conversation mutated(original);
    mutated.append(Message(Role::User, "extra"));
    assert(original.size() == 2);
    assert(mutated.size() == 3);
}

void test_copy_assignment_deep_copy() {
    Conversation a;
    a.append(Message(Role::User, "a-message"));

    Conversation b;
    b.append(Message(Role::User, "b-message-1"));
    b.append(Message(Role::User, "b-message-2"));

    const Message* a_original_begin = a.begin();
    b = a;

    assert(b.size() == 1);
    assert(b.at(0).content() == "a-message");
    assert(a.begin() == a_original_begin);
    assert(b.begin() != a.begin());

    // Self-assignment must be a no-op, not a use-after-free.
    b = b;
    assert(b.size() == 1);
    assert(b.at(0).content() == "a-message");
}

void test_move_constructor_steals_buffer() {
    Conversation original;
    original.append(Message(Role::User, "hello"));
    original.append(Message(Role::Assistant, "world"));

    const Message* original_ptr = original.begin();
    Conversation moved(std::move(original));

    assert(moved.begin() == original_ptr);
    assert(moved.size() == 2);
    assert(original.size() == 0);
    assert(original.begin() == nullptr);
}

void test_move_assignment_steals_buffer() {
    Conversation source;
    source.append(Message(Role::User, "keep-me"));
    const Message* source_ptr = source.begin();

    Conversation target;
    target.append(Message(Role::User, "discard-me"));

    target = std::move(source);

    assert(target.begin() == source_ptr);
    assert(target.size() == 1);
    assert(target.at(0).content() == "keep-me");
    assert(source.size() == 0);
    assert(source.begin() == nullptr);
}

void test_growth_doubles_capacity() {
    Conversation conv;
    std::size_t expected[] = {1, 2, 4, 4, 8, 8, 8, 8, 16, 16};

    for (std::size_t i = 0; i < 10; ++i) {
        conv.append(Message(Role::User, std::to_string(i)));
        assert(conv.capacity() == expected[i]);
        assert(conv.size() == i + 1);
    }

    for (std::size_t i = 0; i < 10; ++i) {
        assert(conv.at(i).content() == std::to_string(i));
    }
}

// ---- SentinelScanner --------------------------------------------------------

void test_scanner_clean_text_no_sentinel() {
    SentinelScanner scanner(kSentinel);
    auto out1 = scanner.feed("Hello, how can I help?");
    auto out2 = scanner.flush();

    assert(!out1.sentinel_found);
    assert(!out2.sentinel_found);
    assert(out1.safe_text + out2.safe_text == "Hello, how can I help?");
}

void test_scanner_whole_sentinel_one_chunk() {
    SentinelScanner scanner(kSentinel);
    auto out = scanner.feed(std::string("Goodbye.") + kSentinel);

    assert(out.sentinel_found);
    assert(out.safe_text == "Goodbye.");
}

void test_scanner_catches_sentinel_at_every_boundary() {
    const std::string sentinel = kSentinel;
    const std::string text = "Goodbye." + sentinel;

    for (std::size_t split = 0; split <= text.size(); ++split) {
        SentinelScanner scanner(sentinel);
        auto out1 = scanner.feed(text.substr(0, split));
        auto out2 = scanner.feed(text.substr(split));
        auto out3 = scanner.flush();

        assert((out1.sentinel_found || out2.sentinel_found) &&
               "sentinel must be caught regardless of split point");
        assert(out1.safe_text + out2.safe_text + out3.safe_text == "Goodbye.");
    }
}

void test_scanner_false_alarm_on_partial_match() {
    SentinelScanner scanner(kSentinel);
    const std::string text = "Reading <|end_world|> now.";

    std::string collected;
    bool found = false;
    for (char c : text) {
        auto out = scanner.feed(std::string(1, c));
        collected += out.safe_text;
        found = found || out.sentinel_found;
    }
    auto tail = scanner.flush();
    collected += tail.safe_text;

    assert(!found);
    assert(!tail.sentinel_found);
    assert(collected == text);
}

void test_scanner_pending_bounded_across_large_stream() {
    const std::string sentinel = kSentinel;
    SentinelScanner scanner(sentinel);
    const std::size_t bound = sentinel.size() - 1;

    const std::size_t stream_size = 4 * 1024 * 1024;  // 4MB, one byte at a time
    for (std::size_t i = 0; i < stream_size; ++i) {
        char c = "abcdefghijklmnopqrstuvwxyz"[i % 26];
        scanner.feed(std::string(1, c));
        assert(scanner.pending_size() <= bound);
    }
    scanner.flush();
    assert(scanner.pending_size() == 0);
}

// ---- Harness (wiring, not our logic) ---------------------------------------

void test_harness_stops_at_turn_limit() {
    const std::string script_path = "test_turn_limit.script";
    write_script(script_path,
                 "role: assistant\n"
                 "Sure thing.\n"
                 "---\n"
                 "role: assistant\n"
                 "Anything else?\n"
                 "---\n"
                 "role: assistant\n"
                 "Happy to help.\n");

    auto model = std::make_unique<ScriptedModelClient>(script_path);
    HarnessConfig cfg;
    cfg.max_turns = 2;
    Harness harness(std::move(model), cfg);

    ListInputSource in({"hi", "there", "more"});
    CapturingOutputSink out;
    StopReason reason = harness.run(in, out);

    assert(reason.kind == StopReason::Kind::TurnLimit);
    assert(harness.conversation().size() == 4);  // 2 user + 2 assistant turns

    std::remove(script_path.c_str());
}

void test_harness_stops_at_sentinel() {
    const std::string script_path = "test_sentinel_halt.script";
    write_script(script_path,
                 "role: assistant\n"
                 "Sure.\n"
                 "---\n"
                 "chunk: 5\n"
                 "role: assistant\n"
                 "Goodbye.<|end_conversation|>\n");

    auto model = std::make_unique<ScriptedModelClient>(script_path);
    HarnessConfig cfg;
    cfg.max_turns = 20;
    Harness harness(std::move(model), cfg);

    ListInputSource in({"hello", "bye"});
    CapturingOutputSink out;
    StopReason reason = harness.run(in, out);

    assert(reason.kind == StopReason::Kind::Sentinel);
    assert(out.captured().find("<|end_conversation|>") == std::string::npos);

    const Conversation& conv = harness.conversation();
    assert(conv.at(conv.size() - 1).content().find("<|end_conversation|>") !=
           std::string::npos);

    std::remove(script_path.c_str());
}

void test_transcript_round_trip() {
    Conversation conv;
    conv.append(Message(Role::System, "Be concise."));
    conv.append(Message(Role::User, "hello"));
    conv.append(Message(Role::Assistant, "Hi! What can I do for you today?"));
    conv.append(Message(Role::User, "goodbye"));
    conv.append(Message(Role::Assistant, std::string("Goodbye.") + kSentinel));

    const std::string transcript_path = "test_transcript_round_trip.txt";
    write_transcript(transcript_path, conv);

    ReplayModelClient replay(transcript_path);
    assert(replay.system_message() == "Be concise.");

    Conversation dummy;
    Message reply1 = replay.generate(dummy);
    assert(reply1.content() == "Hi! What can I do for you today?");

    Message reply2 = replay.generate(dummy);
    assert(reply2.content() == std::string("Goodbye.") + kSentinel);

    bool threw = false;
    try {
        replay.generate(dummy);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    assert(threw);

    std::remove(transcript_path.c_str());
}

int main() {
    test_empty_conversation_bounds();
    test_append_preserves_order();
    test_system_message_pinned_at_front();
    test_copy_constructor_deep_copy();
    test_copy_assignment_deep_copy();
    test_move_constructor_steals_buffer();
    test_move_assignment_steals_buffer();
    test_growth_doubles_capacity();

    test_scanner_clean_text_no_sentinel();
    test_scanner_whole_sentinel_one_chunk();
    test_scanner_catches_sentinel_at_every_boundary();
    test_scanner_false_alarm_on_partial_match();
    test_scanner_pending_bounded_across_large_stream();

    test_harness_stops_at_turn_limit();
    test_harness_stops_at_sentinel();
    test_transcript_round_trip();

    std::cout << "All tests passed.\n";
    return 0;
}
