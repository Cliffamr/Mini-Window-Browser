#pragma once

#include <windows.h>

#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <string_view>
#include <system_error>

namespace xiaochuang {

// Deliberately accepts only developer-supplied event names and numeric metadata.
// Callers must never pass page URLs, titles, user text, cookies or script output.
class LogStore {
public:
    explicit LogStore(std::filesystem::path configPath)
        : directory_(std::move(configPath).parent_path() / L"logs") {}

    const std::filesystem::path& Directory() const noexcept { return directory_; }

    bool EnsureDirectory() const {
        std::error_code error;
        std::filesystem::create_directories(directory_, error);
        return !error && std::filesystem::is_directory(directory_, error) && !error;
    }

    void SetRetention(bool enabled, int days) {
        std::lock_guard<std::mutex> lock(mutex_);
        cleanupEnabled_ = enabled;
        retentionDays_ = days == 3 || days == 7 || days == 30 ? days : 7;
        // Apply the new policy immediately, including when a disabled policy
        // becomes enabled. User files and unrelated files are never touched.
        CleanupLocked();
    }

    void Cleanup() {
        std::lock_guard<std::mutex> lock(mutex_);
        CleanupLocked();
    }

    void Write(std::string_view event, std::string_view fields = {}) {
        std::lock_guard<std::mutex> lock(mutex_);
        try {
            SYSTEMTIME utc{};
            GetSystemTime(&utc);
            char stamp[64]{};
            char date[24]{};
            sprintf_s(stamp, sizeof(stamp), "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ",
                      utc.wYear, utc.wMonth, utc.wDay, utc.wHour, utc.wMinute,
                      utc.wSecond, utc.wMilliseconds);
            wchar_t wideDate[24]{};
            swprintf_s(wideDate, L"%04u-%02u-%02u", utc.wYear, utc.wMonth, utc.wDay);
            const std::wstring today(wideDate);
            if (cleanupDate_ != today) {
                CleanupLocked();
                cleanupDate_ = today;
            }
            if (!EnsureDirectory()) return;
            const auto path = directory_ / (L"XiaoChuang-" + today + L".log");
            std::error_code error;
            const auto size = std::filesystem::file_size(path, error);
            // Protect users against runaway logs if a bug repeats an event.
            if (!error && size >= 8 * 1024 * 1024) return;
            std::ofstream file(path, std::ios::binary | std::ios::app);
            if (!file) return;
            file << stamp << " pid=" << GetCurrentProcessId()
                 << " tid=" << GetCurrentThreadId() << " " << event;
            if (!fields.empty()) file << " " << fields;
            file << "\r\n";
        } catch (...) {
            // Logging is best effort and must never crash or block startup.
        }
    }

private:
    void CleanupLocked() {
        if (!cleanupEnabled_) return;
        try {
            if (!EnsureDirectory()) return;
            const auto cutoff = std::filesystem::file_time_type::clock::now() -
                std::chrono::hours(24 * retentionDays_);
            std::error_code error;
            for (std::filesystem::directory_iterator iter(directory_, error), end;
                 !error && iter != end; iter.increment(error)) {
                const auto& entry = *iter;
                if (!entry.is_regular_file(error) || error) { error.clear(); continue; }
                const auto name = entry.path().filename().wstring();
                // Only own daily log files, never user-provided files.
                if (name.size() != 24 || name.compare(0, 11, L"XiaoChuang-") != 0 ||
                    name.compare(21, 4, L".log") != 0) continue;
                const auto modified = entry.last_write_time(error);
                if (error) { error.clear(); continue; }
                if (modified < cutoff) {
                    std::filesystem::remove(entry.path(), error);
                    error.clear();
                }
            }
        } catch (...) {
        }
    }

    std::filesystem::path directory_;
    std::mutex mutex_;
    bool cleanupEnabled_ = true;
    int retentionDays_ = 7;
    std::wstring cleanupDate_;
};

} // namespace xiaochuang
