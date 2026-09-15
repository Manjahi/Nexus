# Performance pass (Milestone 8)

Methodology, findings, and the one fix this pass produced. Numbers are from
one development machine (Intel Core i5-6300U @ 2.40GHz, 2 cores/4 threads,
Windows 10) - useful for spotting regressions and gross architectural
mismatches, not as an absolute performance claim. Re-run before trusting
these numbers on different hardware.

## Tool

`tools/bench` (`nexuspc-bench.exe`) generates a synthetic dataset of text
files (a fixed 42-word vocabulary, so files compress/index realistically)
with a configurable duplicate ratio, then times:

1. **Storage scan** - `DuplicateScanner::scan()` over the dataset.
2. **Backup run (cold)** - `BackupEngine::run()` into a fresh `ObjectStore`.
3. **Backup run (repeat)** - the same job run again over the same tree
   (should be almost entirely dedup hits).
4. **Search indexing** - `SearchIndexer::index_tree()` over the same tree.

All four run in-process against the actual module code (not the desktop
app), so results measure the algorithms, not the UI. Usage:

```powershell
cmake --build build/ci-windows --config Release --target nexuspc_bench
build/ci-windows/tools/bench/Release/nexuspc-bench.exe --files 2000 --size-kb 16 --dup-ratio 0.15
```

**Always benchmark a Release build.** An early Debug-build run showed search
indexing at ~20 files/s, ~9x slower than everything else and looking like a
real bug; the same code in Release ran at ~325 files/s, faster than storage
scan. MSVC Debug's unoptimized allocation-heavy string code (the tokenizer
builds one `std::string` per token) dominates the picture in a way that has
nothing to do with the actual algorithm - don't draw architectural
conclusions from Debug timings.

## Results (Release, 2000 files, 16 KB each, 15% exact-duplicate ratio)

| Operation | Time | Rate | Throughput |
|---|---|---|---|
| Storage scan (duplicate detect) | 3.59s | 557 files/s | 8.7 MB/s |
| Backup run (cold) | 13.68s | 146 files/s | 2.3 MB/s |
| Backup run (repeat, dedup) | 5.86s | 341 files/s | 5.3 MB/s |
| Search indexing | 6.14s | 326 files/s | 5.1 MB/s |

**Backup is the slowest operation, and that's expected, not a bug**: it
BLAKE3-hashes and copies every single file into content-addressed storage,
while storage scan only fully hashes files that already share an exact size
with another file (most synthetic files here are unique-sized and never get
past the cheap size-bucketing phase). Backup's own repeat-run number (341
files/s) shows the dedup path working as designed - it still has to hash
every file to check, but skips the write when the blob's already stored.

## Finding: search indexing committed once per document

Root-caused during this pass, from the earlier Debug-run number before
realizing the Debug/Release gap explained most of it: `SearchIndexer::
index_tree()` called `SearchRepository::upsert_file()` and `replace_postings
()` per file, and `replace_postings()` opened its own `BEGIN`/`COMMIT`
transaction - one fsync-triggering commit per file, versus storage scan
(writes only at the very end, per duplicate group) and backup (writes its
whole file-record batch in one call after the loop). Fixed by adding
`SearchRepository::begin_batch()` / `replace_postings_in_batch()` and having
`index_tree()` commit every 200 documents instead of every 1 - bounded so a
cancel or crash mid-run loses at most one batch, not the whole run. The
Debug-build improvement from this alone was modest (~12%, within this
machine's run-to-run noise); the real win was the Debug/Release gap above,
but the batching change is correct practice regardless and is kept.

## Known gaps

- No throughput number yet for `NetworkScanner` (ICMP-bound, not disk-bound,
  so a different kind of benchmark - sweeping a CIDR range against loopback
  or a LAN target - would be needed) or `RestoreEngine`.
- No UI-responsiveness-under-load measurement (e.g. does the GUI thread stay
  interactive while `Throttle::Low` is active during a large scan). The
  thread-pool + queued-invoke architecture (every heavy job runs off the GUI
  thread) makes this architecturally unlikely to regress, but it hasn't been
  measured directly.
- Single-machine numbers, single hardware profile, no automated regression
  tracking (no CI job runs `nexuspc-bench` and fails on drift). Worth adding
  if performance becomes an ongoing concern.
