#pragma once
#include <QObject>
#include <QMutex>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QWaitCondition>
#include <QThread>
#include <QQueue>
#include <atomic>

namespace LogFmt {

inline QString format(const QString& msg) {
    return msg;
}

template <typename... Args>
QString format(const QString& msg, Args&&... args) {
    auto toStr = [](const auto& val) -> QString {
        using T = std::decay_t<decltype(val)>;
        if constexpr (std::is_same_v<T, bool>) {
            return val ? "true" : "false";
        } else if constexpr (std::is_arithmetic_v<T>) {
            return QString::number(val);
        } else {
            return QString("%1").arg(val);
        }
    };
    return msg.arg(toStr(std::forward<Args>(args))...);
}

} // namespace LogFmt


#define LOG_DEBUG(msg, ...) Logger::instance().log(Logger::Debug, LogFmt::format(msg, ##__VA_ARGS__), __FILE__, __LINE__)
#define LOG_INFO(msg, ...)  Logger::instance().log(Logger::Info,  LogFmt::format(msg, ##__VA_ARGS__), __FILE__, __LINE__)
#define LOG_WARN(msg, ...)  Logger::instance().log(Logger::Warn,  LogFmt::format(msg, ##__VA_ARGS__), __FILE__, __LINE__)
#define LOG_ERROR(msg, ...) Logger::instance().log(Logger::Error, LogFmt::format(msg, ##__VA_ARGS__), __FILE__, __LINE__)

#define LOG_INFO0(msg)  Logger::instance().log(Logger::Info,  msg, __FILE__, __LINE__)
#define LOG_WARN0(msg)  Logger::instance().log(Logger::Warn,  msg, __FILE__, __LINE__)
#define LOG_ERR0(msg)   Logger::instance().log(Logger::Error, msg, __FILE__, __LINE__)
#define LOG_DEBUG0(msg) Logger::instance().log(Logger::Debug, msg, __FILE__, __LINE__)


// ---------- Logger ----------
class Logger : public QObject
{
    Q_OBJECT
public:
    enum Level { Debug = 0, Info, Warn, Error };

    static Logger& instance();

    void init(const QString& filePath,
              Level minLevel = Info,
              int   maxLines = 500);

    void log(Level level, const QString& message,
             const char* file = nullptr, int line = 0);

    QStringList recentLines(int count = 200) const;

    void  setMinLevel(Level level);
    Level minLevel() const { return m_minLevel.load(std::memory_order_relaxed); }

signals:
    void lineAppended(const QString& formattedLine);

private:
    Logger();
    ~Logger();
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    void writerLoop();

    static QString levelTag(Level l);
    static QString shortFile(const char* path);

    mutable QMutex   m_queueMutex;
    QWaitCondition   m_queueCond;
    QQueue<QString>  m_queue;
    bool             m_quit = false;

    QFile            m_file;
    QTextStream      m_stream;

    mutable QMutex m_bufMutex;
    QStringList    m_buffer;
    int            m_maxLines = 500;

    std::atomic<Level> m_minLevel { Info };
    bool               m_initialized = false;

    QThread* m_writerThread = nullptr;
};