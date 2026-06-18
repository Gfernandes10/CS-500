#ifndef TELLO_LOGGER_HPP
#define TELLO_LOGGER_HPP

#include <string>
#include <memory>

namespace tello {

// ============================================================================
// Logger Interface
// ============================================================================
/// Simple logging interface with levels (DEBUG, INFO, WARNING, ERROR).
class Logger {
public:
    enum class Level {
        DEBUG = 0,
        INFO = 1,
        WARNING = 2,
        ERROR = 3,
    };

    /// Constructor
    Logger() = default;
    
    /// Destructor
    virtual ~Logger() = default;

    /// Log a message with specified level.
    /// @param level Log level.
    /// @param message Message to log.
    virtual void log(Level level, const std::string& message) = 0;

    /// Log debug message.
    virtual void debug(const std::string& message) = 0;

    /// Log info message.
    virtual void info(const std::string& message) = 0;

    /// Log warning message.
    virtual void warning(const std::string& message) = 0;

    /// Log error message.
    virtual void error(const std::string& message) = 0;

    /// Set minimum log level (messages below this level are ignored).
    /// @param level Minimum log level to display.
    virtual void setLevel(Level level) = 0;
};

// ============================================================================
// Concrete Logger Implementation
// ============================================================================
/// Console logger that prints to stdout/stderr with timestamps.
class ConsoleLogger : public Logger {
public:
    /// Constructor with optional minimum log level.
    /// @param min_level Minimum level to display (default: DEBUG).
    explicit ConsoleLogger(Level min_level = Level::DEBUG);

    ~ConsoleLogger() override = default;

    void log(Level level, const std::string& message) override;
    void debug(const std::string& message) override;
    void info(const std::string& message) override;
    void warning(const std::string& message) override;
    void error(const std::string& message) override;
    void setLevel(Level level) override;

private:
    Level min_level_;

    /// Convert level enum to string.
    /// @param level The log level.
    /// @return String representation (e.g., "DEBUG", "INFO").
    std::string levelToString(Level level) const;

    /// Get current timestamp as string.
    /// @return Timestamp (e.g., "2026-06-15 14:30:45").
    std::string getCurrentTime() const;
};

// ============================================================================
// Global logger instance
// ============================================================================
/// Global logger instance used throughout the library.
extern std::shared_ptr<Logger> g_logger;

/// Initialize global logger (called by library initialization).
/// @param logger Logger instance to use globally.
void initializeGlobalLogger(std::shared_ptr<Logger> logger);

} // namespace tello

#endif // TELLO_LOGGER_HPP