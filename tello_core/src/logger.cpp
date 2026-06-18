#include "tello/logger.hpp"

namespace tello {

std::shared_ptr<Logger> g_logger = nullptr;

ConsoleLogger::ConsoleLogger(Level min_level)
    : min_level_(min_level) {}

void ConsoleLogger::log(Level level, const std::string& message) {
    (void)level;
    (void)message;
    // TODO: implement in Phase B
}

void ConsoleLogger::debug(const std::string& message) {
    log(Level::DEBUG, message);
}

void ConsoleLogger::info(const std::string& message) {
    log(Level::INFO, message);
}

void ConsoleLogger::warning(const std::string& message) {
    log(Level::WARNING, message);
}

void ConsoleLogger::error(const std::string& message) {
    log(Level::ERROR, message);
}

void ConsoleLogger::setLevel(Level level) {
    min_level_ = level;
}

std::string ConsoleLogger::levelToString(Level level) const {
    (void)level;
    return "TODO";
}

std::string ConsoleLogger::getCurrentTime() const {
    return "TODO";
}

void initializeGlobalLogger(std::shared_ptr<Logger> logger) {
    g_logger = logger;
}

} // namespace tello