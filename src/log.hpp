#pragma once

#include <fstream>
#include <string>
#include <sstream>
#include <mutex>
#include <filesystem>
#include <chrono>
#include <iomanip>
#include <algorithm>
#include <vector>
#include <locale>
#include <codecvt>
#include <type_traits>

namespace fs = std::filesystem;

enum class Level {
    DEBUG,
    INFO,
    WARNING,
    ERROR_
};

class Logger {
public:

    // Get singleton instance
    static Logger& getInstance() {
        static Logger instance;
        return instance;
    }

    // Delete copy/move constructors
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;
    Logger(Logger&&) = delete;
    Logger& operator=(Logger&&) = delete;

    // Stream-like logging interface
    class LogStream {
    public:
        LogStream(Logger& logger, Level level) 
            : logger_(logger), level_(level) {}

        ~LogStream() {
            logger_.write(level_, stream_.str());
        }

        // Generic template for most types (excludes wide strings)
        template<typename T>
        typename std::enable_if<
            !std::is_same<typename std::decay<T>::type, std::wstring>::value &&
            !(std::is_pointer<typename std::decay<T>::type>::value &&
              std::is_same<typename std::remove_cv<typename std::remove_pointer<typename std::decay<T>::type>::type>::type, wchar_t>::value),
            LogStream&
        >::type
        operator<<(const T& value) {
            stream_ << value;
            return *this;
        }

        // Support for std::wstring
        LogStream& operator<<(const std::wstring& wstr) {
            stream_ << wstringToString(wstr);
            return *this;
        }

        // Support for wide string literals (const wchar_t*)
        LogStream& operator<<(const wchar_t* wstr) {
            if (wstr) {
                stream_ << wstringToString(std::wstring(wstr));
            }
            return *this;
        }

        // Support for std::endl and other manipulators
        LogStream& operator<<(std::ostream& (*manip)(std::ostream&)) {
            manip(stream_);
            return *this;
        }

    private:
        Logger& logger_;
        Level level_;
        std::ostringstream stream_;

        static std::string wstringToString(const std::wstring& wstr) {
            if (wstr.empty()) return std::string();
            
            std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
            return converter.to_bytes(wstr);
        }
    };

    // Logging methods
    LogStream debug() { return LogStream(*this, Level::DEBUG); }
    LogStream info() { return LogStream(*this, Level::INFO); }
    LogStream warning() { return LogStream(*this, Level::WARNING); }
    LogStream error() { return LogStream(*this, Level::ERROR_); }

    // Direct write (for internal use)
    void write(Level level, const std::string& message) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        // Check if we need to rotate the log file (date changed)
        auto now = std::chrono::system_clock::now();
        auto now_date = getDateString(now);
        
        if (now_date != current_date_) {
            rotateLog();
        }

        // Write log entry
        if (log_file_.is_open()) {
            std::string levelString = levelToString(level);
            log_file_ << "[" << getTimestamp(now) << "] "
                     << "[" << levelString << "] " << (levelString.length() == 4 ? " " : "")
                     << message;
            // Append newline if not already present
            if (!message.empty() && message.back() != '\n') {
                log_file_ << std::endl;
            }
            log_file_.flush();
        }
    }

private:
    Logger() {
        initializeLogger();
    }

    ~Logger() {
        if (log_file_.is_open()) {
            log_file_.close();
        }
    }

    void initializeLogger() {
        // Create logs directory if it doesn't exist
        if (!fs::exists("logs")) {
            fs::create_directory("logs");
        }

        // Clean up old log files
        cleanupOldLogs();

        // Open current log file
        rotateLog();
    }

    void rotateLog() {
        if (log_file_.is_open()) {
            log_file_.close();
        }

        auto now = std::chrono::system_clock::now();
        current_date_ = getDateString(now);
        
        std::string log_path = "logs/" + current_date_ + ".log";
        log_file_.open(log_path, std::ios::app);
        
        if (!log_file_.is_open()) {
            throw std::runtime_error("Failed to open log file: " + log_path);
        }
    }

    void cleanupOldLogs() {
        const size_t MAX_LOG_FILES = 5;
        
        std::vector<fs::path> log_files;
        
        // Collect all .log files in the logs directory
        if (fs::exists("logs") && fs::is_directory("logs")) {
            for (const auto& entry : fs::directory_iterator("logs")) {
                if (entry.is_regular_file() && entry.path().extension() == ".log") {
                    log_files.push_back(entry.path());
                }
            }
        }

        // Sort by modification time (newest first)
        std::sort(log_files.begin(), log_files.end(),
            [](const fs::path& a, const fs::path& b) {
                return fs::last_write_time(a) > fs::last_write_time(b);
            });

        // Delete files beyond the MAX_LOG_FILES limit
        for (size_t i = MAX_LOG_FILES; i < log_files.size(); ++i) {
            fs::remove(log_files[i]);
        }
    }

    std::string getDateString(std::chrono::system_clock::time_point time_point) {
        auto time_t = std::chrono::system_clock::to_time_t(time_point);
        auto tm = *std::localtime(&time_t);
        
        std::ostringstream oss;
        oss << std::put_time(&tm, "%Y-%m-%d");
        return oss.str();
    }

    std::string getTimestamp(std::chrono::system_clock::time_point time_point) {
        auto time_t = std::chrono::system_clock::to_time_t(time_point);
        auto tm = *std::localtime(&time_t);
        
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            time_point.time_since_epoch()) % 1000;
        
        std::ostringstream oss;
        oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S")
            << '.' << std::setfill('0') << std::setw(3) << ms.count();
        return oss.str();
    }

    std::string levelToString(Level level) {
        switch (level) {
            case Level::DEBUG:   return "DEBUG";
            case Level::INFO:    return "INFO";
            case Level::WARNING: return "WARN";
            case Level::ERROR_:  return "ERROR";
            default:             return "UNKNOWN";
        }
    }

    std::ofstream log_file_;
    std::string current_date_;
    std::mutex mutex_;
};

// Convenience macro for common usage
#define LOG_DEBUG   Logger::getInstance().debug()
#define LOG_INFO    Logger::getInstance().info()
#define LOG_WARN    Logger::getInstance().warning()
#define LOG_ERROR   Logger::getInstance().error()
