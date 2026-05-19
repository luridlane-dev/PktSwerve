#include "Logger.h"
#include <QStandardPaths>
#include <QDir>
#include <QMetaObject>
#include <QCoreApplication>
#include <QFileInfo>

Logger& Logger::instance()
{
    static Logger s;
    return s;
}

Logger::Logger()
{
    m_writerThread = new QThread(this);
    connect(m_writerThread, &QThread::started, this, [this]() {
        writerLoop();
    }, Qt::DirectConnection);
    m_writerThread->setObjectName("LogWriter");
    m_writerThread->start();
}

Logger::~Logger()
{
    {
        QMutexLocker lock(&m_queueMutex);
        m_quit = true;
        m_queueCond.wakeAll();
    }
    m_writerThread->wait(3000);

}

void Logger::init(const QString& filePath, Level minLevel, int maxLines)
{
    m_minLevel.store(minLevel, std::memory_order_relaxed);
    m_maxLines = maxLines;

    if (QFile::exists(filePath)) {
        QString backup = filePath + ".1";
        QFile::remove(backup);
        QFile::rename(filePath, backup);
    }

    m_file.setFileName(filePath);
    if (m_file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        m_stream.setDevice(&m_file);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        m_stream.setEncoding(QStringConverter::Utf8);
#else
        m_stream.setCodec("UTF-8");
#endif
        m_initialized = true;
    }

    QString header = QString("===== PktSwerve started at %1 =====")
                         .arg(QDateTime::currentDateTime()
                                  .toString("yyyy-MM-dd HH:mm:ss"));
    {
        QMutexLocker lock(&m_queueMutex);
        m_queue.enqueue(header);
        m_queueCond.wakeOne();
    }
}


void Logger::log(Level level, const QString& message,
                 const char* file, int line)
{
    if (level < m_minLevel.load(std::memory_order_relaxed)) return;

    QString timestamp = QDateTime::currentDateTime().toString("HH:mm:ss.zzz");
    QString location;
    if (file)
        location = QString(" [%1:%2]").arg(shortFile(file)).arg(line);

    QString formatted = QString("[%1] %2  %3%4")
                            .arg(timestamp)
                            .arg(levelTag(level))
                            .arg(message)
                            .arg(location);

    {
        QMutexLocker lock(&m_bufMutex);
        m_buffer.append(formatted);
        while (m_buffer.size() > m_maxLines)
            m_buffer.removeFirst();
    }

    {
        QMutexLocker lock(&m_queueMutex);
        m_queue.enqueue(formatted);
        m_queueCond.wakeOne();
    }

    if (QCoreApplication::instance()) {
        QMetaObject::invokeMethod(QCoreApplication::instance(),
                                  [this, formatted]() {
                                      emit lineAppended(formatted);
                                  }, Qt::QueuedConnection);
    }
}


void Logger::writerLoop()
{
    static constexpr int FLUSH_INTERVAL_LINES = 20;
    int linesSinceFlush = 0;

    while (true) {
        QQueue<QString> batch;

        {
            QMutexLocker lock(&m_queueMutex);
            while (m_queue.isEmpty() && !m_quit)
                m_queueCond.wait(&m_queueMutex);

            if (m_quit && m_queue.isEmpty())
                break;

            batch.swap(m_queue);
        }

        if (m_file.isOpen()) {
            while (!batch.isEmpty()) {
                m_stream << batch.dequeue() << "\n";
                ++linesSinceFlush;
            }
            if (linesSinceFlush >= FLUSH_INTERVAL_LINES) {
                m_stream.flush();
                linesSinceFlush = 0;
            }
        }
    }

    if (m_file.isOpen()) {
        m_stream.flush();
        m_file.close();
    }
}


QStringList Logger::recentLines(int count) const
{
    QMutexLocker lock(&m_bufMutex);
    if (m_buffer.size() <= count)
        return m_buffer;
    return m_buffer.mid(m_buffer.size() - count);
}

void Logger::setMinLevel(Level level)
{
    m_minLevel.store(level, std::memory_order_relaxed);
}


QString Logger::levelTag(Level l)
{
    switch (l) {
    case Debug: return "DBG";
    case Info:  return "INF";
    case Warn:  return "WRN";
    case Error: return "ERR";
    }
    return "???";
}

QString Logger::shortFile(const char* path)
{
    QString s(path);
    int idx = s.lastIndexOf('/');
    if (idx < 0) idx = s.lastIndexOf('\\');
    return idx >= 0 ? s.mid(idx + 1) : s;
}