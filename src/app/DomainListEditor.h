#pragma once
#include <QString>
#include <QStringList>
#include <QWidget>

class QLabel;
class QLineEdit;
class QPushButton;
class QScrollArea;
class QTimer;
class QVBoxLayout;

// A list of entries shown as removable chips, one per row. It holds either
// domain names or application names (the app list has Browse and Running buttons).
class DomainListEditor : public QWidget {
    Q_OBJECT
public:
    enum class Kind { Domains, Apps };

    explicit DomainListEditor(Kind kind = Kind::Domains, QWidget* parent = nullptr);

    void setDomains(const QStringList& domains);
    QStringList domains() const { return m_domains; }

    // Turns what the user typed or pasted (a URL, "host:443", "*.example.com")
    // into a bare lower-case host name. Empty when it is not a usable domain.
    static QString normalizeDomain(const QString& text);

private:
    enum class AddResult { Added, Duplicate, Invalid };

    void addFromInput();
    // `fromConfig` is for entries loaded from the saved settings: those were accepted by
    // older versions, so they are kept (only tidied) rather than checked strictly.
    AddResult addDomain(const QString& text, bool fromConfig = false);
    void removeDomain(const QString& domain, QWidget* row);
    void clearAll();
    void copyAll();
    void browseForApps();
    void pickRunningApps();
    void updateState();
    void flash(const QString& message);
    QString normalize(const QString& text, bool fromConfig) const;
    static QString tidyDomain(QString text);

    Kind         m_kind;
    QStringList  m_domains;
    QLineEdit*   m_input = nullptr;
    QScrollArea* m_scroll = nullptr;
    QLabel*      m_empty = nullptr;
    QVBoxLayout* m_rows = nullptr;
    QLabel*      m_count = nullptr;
    QTimer*      m_flashTimer = nullptr;
    QPushButton* m_copyBtn = nullptr;
    QPushButton* m_clearBtn = nullptr;
};
