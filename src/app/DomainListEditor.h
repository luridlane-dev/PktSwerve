#pragma once
#include <QStringList>
#include <QWidget>

class QLabel;
class QLineEdit;
class QPushButton;
class QVBoxLayout;

// Domain list shown as removable chips, one per row.
class DomainListEditor : public QWidget {
    Q_OBJECT
public:
    explicit DomainListEditor(QWidget* parent = nullptr);

    void setDomains(const QStringList& domains);
    QStringList domains() const { return m_domains; }

private:
    void addFromInput();
    void addDomain(const QString& text);
    void removeDomain(const QString& domain, QWidget* row);
    void clearAll();
    void copyAll();
    void updateState();

    QStringList  m_domains;
    QLineEdit*   m_input = nullptr;
    QVBoxLayout* m_rows = nullptr;
    QLabel*      m_count = nullptr;
    QPushButton* m_copyBtn = nullptr;
    QPushButton* m_clearBtn = nullptr;
};
