# Design Log — Project 2

## Growth factor and amortized cost

`Conversation` grows by doubling: `new_capacity = capacity_ == 0 ? 1 : capacity_ * 2`.
Capacity starts at 0 and changes only inside `grow()`, which runs exactly
when `append()` is called with `size_ == capacity_`.

**Claim:** over `n` calls to `append`, the total element-copy work across
all reallocations is `O(n)`, so each `append` is `O(1)` amortized.

**Proof (accounting argument).** Charge each `append` 3 credits: 1 pays for
writing the new element, and 2 are banked on it. A reallocation at size `k`
(capacity `k` doubling to `2k`) costs exactly `k` element moves. Those `k`
moves are paid for by the `k/2` insertions since the previous reallocation
(from `k/2` to `k`), each carrying 2 banked credits — `k` credits banked,
`k` credits needed. Every reallocation is paid for out of credit already
collected, so total real work across `n` appends is bounded by total
credit issued, `3n = O(n)`. Dividing by `n` gives `O(1)` amortized per
call.

Concretely, reallocations occur at sizes 1, 2, 4, 8, ..., so total copying
is `1 + 2 + 4 + ... + n ≈ 2n` — a geometric series, `O(n)`, not the
`O(n²)` you'd get from growing by a fixed increment (which reallocates on
*every* append: `1 + 2 + ... + n`).

## Rule of Five evidence

`Conversation` is the only class holding a raw owning pointer (`Message*
data_`), so it's the only class requiring all five special members by
hand; `Message` and `SentinelScanner` own only `std::string`s and get
correct behavior from the compiler-generated members.

- **Destructor:** `delete[] data_`, safe on a moved-from object since move
  sets `data_ = nullptr` and `delete[] nullptr` is a no-op.
- **Copy ctor/assignment:** both allocate a *new* buffer sized to
  `other.size_` and copy every `Message` into it before touching
  `this->data_`. Assignment allocates and copies first, then frees the old
  buffer and swaps in the new pointer, so a self-assignment (checked
  explicitly, and safe regardless since the new buffer never aliases the
  old one) never observes a half-freed state. `begin()` on a copy is
  provably a different address, since the buffer is always freshly
  allocated.
- **Move ctor/assignment:** copy the three fields (`data_`, `size_`,
  `capacity_`) verbatim, no per-element work, then zero the source's
  fields — leaving it in a valid, destructible, re-appendable state.

`test_copy_*` asserts distinct pointer addresses and independent mutation;
`test_move_*` asserts the destination inherits the exact source pointer
and the source is zeroed. Compiling and running under
`-fsanitize=address,undefined` is the real proof of no double-free, no
leak, no use-after-move — the argument above is *why* it holds, not a
substitute for running it.

## Sentinel scanner: bounded pending_ proof

Let `k = sentinel_.size()`. Invariant: after any `feed()` call returns,
`pending_.size() <= k - 1`.

**Proof.** `feed(chunk)` appends `chunk` to `pending_`, then:

1. If `sentinel_` is found in the combined string, `pending_` is cleared
   (`0 <= k - 1`) and the call returns immediately.
2. Otherwise, let `s = pending_.size()`. If `s <= k - 1`, nothing is
   trimmed and the bound holds directly. Otherwise, the leading
   `s - (k - 1)` characters are moved into the returned `safe_text`, and
   `pending_` is left holding exactly the last `k - 1` characters.

Both branches leave `pending_.size() <= k - 1`, so by induction the bound
holds after every call. `flush()` only shrinks `pending_` (to empty), so
it can't violate it either.

This is safe to trim because any occurrence of `sentinel_` spanning this
call and future ones must include at least one of its first `k - 1`
characters, and that overlap can only begin within the last `k - 1`
characters seen so far — anything earlier cannot be part of a future
match, so it's genuinely safe to emit. The bound doesn't depend on chunk
size or stream length, which is what makes the scanner O(1) in space
regardless of whether the 4MB stress-test stream arrives as one chunk or
one byte at a time.

## What I would change differently

`feed()` always holds back the last `k - 1` bytes even when most of them
plainly can't be a sentinel prefix (e.g., after a run of `'a'`, none of it
overlaps `<`). A KMP-style scan tracking the actual longest-matching-prefix
length would release more text immediately instead of waiting for the next
call, at the cost of a failure-function table built once from `sentinel_`.
For a sentinel this short it doesn't matter, but on the adversarial
`<|end_<|end_<|end_...` input from the spec's stretch goal, the naive
`substr`/`find` approach re-scans overlapping regions of `pending_` on
every call, which KMP would avoid.
