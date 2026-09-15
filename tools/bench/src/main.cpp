// nexuspc-bench: throughput measurements for the heaviest per-file loops
// (storage duplicate scan, backup run, search indexing) against a synthetic
// dataset, generated fresh each run and cleaned up on exit. Not part of the
// test suite - a manual profiling tool for Milestone 8's performance pass
// (docs/PERFORMANCE.md).
//
// Usage: nexuspc-bench [--files N] [--size-kb N] [--dup-ratio F]

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <system_error>
#include <vector>

#include "nexus/core/id.hpp"
#include "nexus/db/database.hpp"
#include "nexus/db/migration.hpp"
#include "nexus/fs/exclusion_rules.hpp"
#include "nexus/module/backup/backup_engine.hpp"
#include "nexus/module/backup/backup_repository.hpp"
#include "nexus/module/backup/object_store.hpp"
#include "nexus/module/search/search_indexer.hpp"
#include "nexus/module/search/search_repository.hpp"
#include "nexus/module/storage/duplicate_scanner.hpp"
#include "nexus/module/storage/storage_repository.hpp"

namespace fs = std::filesystem;

namespace {

struct Args {
    int files = 500;
    int size_kb = 64;
    double dup_ratio = 0.2;
};

Args parse_args(int argc, char** argv) {
    Args args;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto next = [&]() -> std::string { return (i + 1 < argc) ? std::string(argv[++i]) : std::string{}; };
        if (arg == "--files") {
            args.files = std::stoi(next());
        } else if (arg == "--size-kb") {
            args.size_kb = std::stoi(next());
        } else if (arg == "--dup-ratio") {
            args.dup_ratio = std::stod(next());
        }
    }
    return args;
}

std::string random_word(std::mt19937& rng) {
    static const std::vector<std::string> vocab = {
        "storage",  "backup",       "network",     "vault",    "search",  "index",
        "module",   "system",       "device",      "connect",  "report",  "audit",
        "schedule", "hash",         "duplicate",   "snapshot", "retain",  "throttle",
        "notify",   "alert",        "platform",    "job",      "thread",  "scanner"};
    std::uniform_int_distribution<std::size_t> dist(0, vocab.size() - 1);
    return vocab[dist(rng)];
}

std::string random_text(std::mt19937& rng, std::size_t target_bytes) {
    std::string out;
    out.reserve(target_bytes + 32);
    while (out.size() < target_bytes) {
        out += random_word(rng);
        out += ' ';
    }
    return out;
}

fs::path make_dataset(int file_count, int size_kb, double dup_ratio) {
    const auto tag = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path root = fs::temp_directory_path() / ("nexuspc_bench_" + std::to_string(tag));
    fs::create_directories(root);

    std::mt19937 rng(42); // fixed seed: reproducible runs across comparisons
    std::vector<std::string> written_contents;
    std::uniform_real_distribution<double> coin(0.0, 1.0);

    for (int i = 0; i < file_count; ++i) {
        std::string content;
        if (!written_contents.empty() && coin(rng) < dup_ratio) {
            std::uniform_int_distribution<std::size_t> pick(0, written_contents.size() - 1);
            content = written_contents[pick(rng)];
        } else {
            content = random_text(rng, static_cast<std::size_t>(size_kb) * 1024);
            written_contents.push_back(content);
        }
        std::ofstream out(root / ("file_" + std::to_string(i) + ".txt"), std::ios::binary);
        out.write(content.data(), static_cast<std::streamsize>(content.size()));
    }
    return root;
}

struct Timing {
    std::string label;
    double seconds = 0.0;
    std::uint64_t bytes = 0;
    std::uint64_t files = 0;
};

void print_timing(const Timing& t) {
    const double mb = static_cast<double>(t.bytes) / (1024.0 * 1024.0);
    const double mb_per_s = t.seconds > 0 ? mb / t.seconds : 0.0;
    const double files_per_s = t.seconds > 0 ? static_cast<double>(t.files) / t.seconds : 0.0;
    std::printf("%-28s %8.2fs  %8llu files  %10.1f MB  %10.1f MB/s  %10.1f files/s\n", t.label.c_str(),
               t.seconds, static_cast<unsigned long long>(t.files), mb, mb_per_s, files_per_s);
}

} // namespace

int main(int argc, char** argv) {
    const Args args = parse_args(argc, argv);
    std::printf("NexusPC benchmark: %d files, %d KB each, %.0f%% duplicate ratio\n\n", args.files,
               args.size_kb, args.dup_ratio * 100.0);

    const fs::path root = make_dataset(args.files, args.size_kb, args.dup_ratio);
    std::uint64_t total_bytes = 0;
    std::uint64_t total_files = 0;
    for (const auto& entry : fs::recursive_directory_iterator(root)) {
        if (entry.is_regular_file()) {
            total_bytes += entry.file_size();
            ++total_files;
        }
    }

    auto db = nexus::db::Database::open_in_memory();
    nexus::db::migrate(db, "core", nexus::db::core_migrations());
    nexus::db::migrate(db, "storage", nexus::module::storage::storage_migrations());
    nexus::db::migrate(db, "backup", nexus::module::backup::backup_migrations());
    nexus::db::migrate(db, "search", nexus::module::search::search_migrations());

    std::vector<Timing> results;
    const auto rules = nexus::fs::ExclusionRules::defaults();

    // Storage: duplicate scan.
    {
        nexus::module::storage::StorageRepository repo(db);
        nexus::module::storage::DuplicateScanner scanner(&repo);
        const auto start = std::chrono::steady_clock::now();
        const auto summary = scanner.scan(root, rules);
        const auto elapsed =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        results.push_back({"Storage scan (dup detect)", elapsed, total_bytes, summary.files_seen});
        std::printf("  -> %zu duplicate group(s), %llu bytes reclaimable\n", summary.groups.size(),
                   static_cast<unsigned long long>(summary.reclaimable_bytes()));
    }

    // Backup: cold run, then a repeat run that should be nearly all dedup hits.
    fs::path store_dir;
    {
        store_dir = root.string() + "_objects";
        nexus::module::backup::BackupRepository repo(db);
        nexus::module::backup::ObjectStore store(store_dir);
        nexus::module::backup::BackupEngine engine(store, &repo);

        nexus::module::backup::BackupJob job;
        job.name = "bench";
        job.source_root = root.string();
        job.destination = store_dir.string();
        const auto job_id = repo.upsert_job(job);

        const auto start = std::chrono::steady_clock::now();
        const auto summary = engine.run(job_id, root, rules);
        const auto elapsed =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        results.push_back({"Backup run (first, cold)", elapsed, summary.total_bytes, summary.file_count});
        std::printf("  -> %llu new bytes written (dedup saved %llu bytes)\n",
                   static_cast<unsigned long long>(summary.new_bytes),
                   static_cast<unsigned long long>(summary.total_bytes - summary.new_bytes));

        const auto start2 = std::chrono::steady_clock::now();
        const auto summary2 = engine.run(job_id, root, rules);
        const auto elapsed2 =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - start2).count();
        results.push_back(
            {"Backup run (repeat, dedup)", elapsed2, summary2.total_bytes, summary2.file_count});
    }

    // Search: index the same tree.
    {
        nexus::module::search::SearchRepository repo(db);
        nexus::module::search::SearchIndexer indexer(repo);
        const auto start = std::chrono::steady_clock::now();
        const auto summary = indexer.index_tree(root, rules);
        const auto elapsed =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        results.push_back({"Search indexing", elapsed, total_bytes, summary.files_indexed});
        std::printf("  -> %llu indexed, %llu skipped, %zu term(s)\n",
                   static_cast<unsigned long long>(summary.files_indexed),
                   static_cast<unsigned long long>(summary.files_skipped), indexer.indexed_terms());
    }

    std::printf("\n%-28s %9s  %13s  %12s  %13s  %12s\n", "Operation", "Time", "Files", "Data",
               "Throughput", "Rate");
    for (const auto& t : results) {
        print_timing(t);
    }

    std::error_code ec;
    fs::remove_all(root, ec);
    fs::remove_all(store_dir, ec);
    return 0;
}
