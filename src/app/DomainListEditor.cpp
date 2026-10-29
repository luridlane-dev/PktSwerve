#include "DomainListEditor.h"

#include <QClipboard>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QTimer>
#include <QVBoxLayout>

namespace {

QString normalize(QString text)
{
    text = text.trimmed().toLower();
    const int scheme = text.indexOf("://");
    if (scheme >= 0)
        text.remove(0, scheme + 3);
    const int slash = text.indexOf('/');
    if (slash >= 0)
        text.truncate(slash);
    while (text.endsWith('.'))
        text.chop(1);
    return text;
}

} // namespace

DomainListEditor::DomainListEditor(QWidget* parent)
    : QWidget(parent)
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(10);

    m_input = new QLineEdit;
    m_input->setPlaceholderText("example.com");
    auto* addBtn = new QPushButton("Add");
    auto* inputRow = new QHBoxLayout;
    inputRow->setSpacing(8);
    inputRow->addWidget(m_input, 1);
    inputRow->addWidget(addBtn);
    root->addLayout(inputRow);

    auto* list = new QWidget;
    m_rows = new QVBoxLayout(list);
    m_rows->setContentsMargins(0, 0, 0, 0);
    m_rows->setSpacing(6);
    m_rows->addStretch();
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setWidget(list);
    root->addWidget(scroll, 1);

    m_count = new QLabel;
    m_count->setObjectName("note");
    m_copyBtn = new QPushButton("Copy list");
    m_clearBtn = new QPushButton("Clear");
    m_clearBtn->setObjectName("destructiveBtn");
    auto* footer = new QHBoxLayout;
    footer->setSpacing(8);
    footer->addWidget(m_count, 1);
    footer->addWidget(m_copyBtn);
    footer->addWidget(m_clearBtn);
    root->addLayout(footer);

    connect(m_input, &QLineEdit::returnPressed, this, &DomainListEditor::addFromInput);
    connect(addBtn, &QPushButton::clicked, this, &DomainListEditor::addFromInput);
    connect(m_copyBtn, &QPushButton::clicked, this, &DomainListEditor::copyAll);
    connect(m_clearBtn, &QPushButton::clicked, this, &DomainListEditor::clearAll);
    updateState();
}

void DomainListEditor::setDomains(const QStringList& domains)
{
    clearAll();
    for (const QString& domain : domains)
        addDomain(domain);
}

void DomainListEditor::addFromInput()
{
    static const QRegularExpression separators("[\\s,;]+");
    for (const QString& part : m_input->text().split(separators, Qt::SkipEmptyParts))
        addDomain(part);
    m_input->clear();
}

void DomainListEditor::addDomain(const QString& text)
{
    const QString domain = normalize(text);
    if (domain.isEmpty() || m_domains.contains(domain))
        return;
    m_domains << domain;

    auto* label = new QLabel(domain);
    label->setTextFormat(Qt::PlainText);
    auto* remove = new QPushButton(QString::fromUtf8("\xC3\x97"));
    remove->setObjectName("chipRemove");
    remove->setCursor(Qt::PointingHandCursor);
    remove->setToolTip("Remove");

    auto* chip = new QFrame;
    chip->setObjectName("chip");
    auto* chipLay = new QHBoxLayout(chip);
    chipLay->setContentsMargins(12, 4, 4, 4);
    chipLay->setSpacing(8);
    chipLay->addWidget(label);
    chipLay->addWidget(remove);

    auto* row = new QWidget;
    auto* rowLay = new QHBoxLayout(row);
    rowLay->setContentsMargins(0, 0, 0, 0);
    rowLay->addWidget(chip);
    rowLay->addStretch();
    m_rows->insertWidget(m_rows->count() - 1, row);

    connect(remove, &QPushButton::clicked, this, [this, domain, row] { removeDomain(domain, row); });
    updateState();
}

void DomainListEditor::removeDomain(const QString& domain, QWidget* row)
{
    m_domains.removeOne(domain);
    m_rows->removeWidget(row);
    row->hide();
    row->deleteLater();
    updateState();
}

void DomainListEditor::clearAll()
{
    while (m_rows->count() > 1) {
        QLayoutItem* item = m_rows->takeAt(0);
        item->widget()->hide();
        item->widget()->deleteLater();
        delete item;
    }
    m_domains.clear();
    updateState();
}

void DomainListEditor::copyAll()
{
    QGuiApplication::clipboard()->setText(m_domains.join('\n'));
    m_copyBtn->setText("Copied");
    QTimer::singleShot(1200, this, [this] { m_copyBtn->setText("Copy list"); });
}

void DomainListEditor::updateState()
{
    m_count->setText(QString("%1 domain(s)").arg(m_domains.size()));
    m_copyBtn->setEnabled(!m_domains.isEmpty());
    m_clearBtn->setEnabled(!m_domains.isEmpty());
}
