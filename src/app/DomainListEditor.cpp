#include "DomainListEditor.h"

#include "core/AppMatcher.h"
#include "core/RunningApps.h"

#include <QClipboard>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

QString DomainListEditor::normalizeDomain(const QString& text)
{
    static const QRegularExpression pathStart("[/?#]");
    static const QRegularExpression validHost(
        "^[a-z0-9_]([a-z0-9_-]*[a-z0-9_])?(\\.[a-z0-9_]([a-z0-9_-]*[a-z0-9_])?)+$");

    QString host = text.trimmed().toLower();

    const int scheme = host.indexOf("://");
    if (scheme >= 0)
        host.remove(0, scheme + 3);
    const int pathAt = host.indexOf(pathStart);
    if (pathAt >= 0)
        host.truncate(pathAt);
    const int userAt = host.lastIndexOf('@');
    if (userAt >= 0)
        host.remove(0, userAt + 1);
    const int portAt = host.lastIndexOf(':');
    if (portAt >= 0)
        host.truncate(portAt);

    // A domain already covers its subdomains, so "*." and "www." add nothing.
    while (host.startsWith("*."))
        host.remove(0, 2);
    if (host.startsWith("www.") && host.indexOf('.', 4) > 0)
        host.remove(0, 4);
    while (host.endsWith('.'))
        host.chop(1);

    // Internationalised names are matched in their punycode form, as seen on the wire.
    host = QString::fromLatin1(QUrl::toAce(host)).toLower();
    return validHost.match(host).hasMatch() ? host : QString();
}

DomainListEditor::DomainListEditor(Kind kind, QWidget* parent)
    : QWidget(parent), m_kind(kind)
{
    const bool apps = kind == Kind::Apps;
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(10);

    m_input = new QLineEdit;
    if (apps) {
#ifdef Q_OS_MACOS
        m_input->setPlaceholderText("Discord.app");
#else
        m_input->setPlaceholderText("discord.exe");
#endif
    } else {
        m_input->setPlaceholderText("example.com");
    }
    m_input->setClearButtonEnabled(true);
    auto* addBtn = new QPushButton(tr("Add"));
    auto* inputRow = new QHBoxLayout;
    inputRow->setSpacing(8);
    inputRow->addWidget(m_input, 1);
    inputRow->addWidget(addBtn);
    if (apps) {
        auto* browseBtn = new QPushButton(tr("Browse..."));
        auto* runningBtn = new QPushButton(tr("Running..."));
        browseBtn->setToolTip(tr("Choose a program from disk"));
        runningBtn->setToolTip(tr("Choose from the programs that are running now"));
        inputRow->addWidget(browseBtn);
        inputRow->addWidget(runningBtn);
        connect(browseBtn, &QPushButton::clicked, this, &DomainListEditor::browseForApps);
        connect(runningBtn, &QPushButton::clicked, this, &DomainListEditor::pickRunningApps);
    }
    root->addLayout(inputRow);

    auto* list = new QWidget;
    m_rows = new QVBoxLayout(list);
    m_rows->setContentsMargins(0, 0, 0, 0);
    m_rows->setSpacing(6);
    m_rows->addStretch();
    m_scroll = new QScrollArea;
    m_scroll->setWidgetResizable(true);
    m_scroll->setWidget(list);
    root->addWidget(m_scroll, 1);

    m_empty = new QLabel(apps ? tr("No apps yet.\nType a program name above, or use Browse and Running.")
                              : tr("No sites yet.\nAdd a domain above. Separate several with spaces or commas."));
    m_empty->setObjectName("note");
    m_empty->setAlignment(Qt::AlignCenter);
    m_empty->setWordWrap(true);
    root->addWidget(m_empty, 1);

    m_count = new QLabel;
    m_count->setObjectName("note");
    m_copyBtn = new QPushButton(tr("Copy list"));
    m_clearBtn = new QPushButton(tr("Clear"));
    m_clearBtn->setObjectName("destructiveBtn");
    auto* footer = new QHBoxLayout;
    footer->setSpacing(8);
    footer->addWidget(m_count, 1);
    footer->addWidget(m_copyBtn);
    footer->addWidget(m_clearBtn);
    root->addLayout(footer);

    m_flashTimer = new QTimer(this);
    m_flashTimer->setSingleShot(true);
    m_flashTimer->setInterval(3000);
    connect(m_flashTimer, &QTimer::timeout, this, &DomainListEditor::updateState);

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
        addDomain(domain, true);
}

void DomainListEditor::addFromInput()
{
    // Program names can contain spaces, domains cannot.
    static const QRegularExpression domainSeparators("[\\s,;]+");
    static const QRegularExpression appSeparators("[,;\\n]+");
    QStringList invalid, duplicates;
    int added = 0;
    for (const QString& part : m_input->text().split(m_kind == Kind::Apps ? appSeparators : domainSeparators, Qt::SkipEmptyParts)) {
        switch (addDomain(part)) {
        case AddResult::Added:     ++added; break;
        case AddResult::Duplicate: duplicates << part; break;
        case AddResult::Invalid:   invalid << part; break;
        }
    }

    // Keep what could not be added in the box so it can be corrected.
    if (invalid.isEmpty())
        m_input->clear();
    else
        m_input->setText(invalid.join(' '));

    if (!invalid.isEmpty())
        flash((m_kind == Kind::Apps ? tr("Not a valid app name: %1") : tr("Not a valid domain: %1"))
                  .arg(invalid.join(", ")));
    else if (!duplicates.isEmpty() && added == 0)
        flash(tr("Already in the list: %1").arg(duplicates.join(", ")));
    m_input->setFocus();
}

DomainListEditor::AddResult DomainListEditor::addDomain(const QString& text, bool fromConfig)
{
    const QString domain = normalize(text, fromConfig);
    if (domain.isEmpty())
        return AddResult::Invalid;
    if (m_domains.contains(domain))
        return AddResult::Duplicate;
    m_domains << domain;

    auto* label = new QLabel(domain);
    label->setTextFormat(Qt::PlainText);
    auto* remove = new QPushButton(QString::fromUtf8("\xC3\x97"));
    remove->setObjectName("chipRemove");
    remove->setCursor(Qt::PointingHandCursor);
    remove->setToolTip(tr("Remove"));

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
    return AddResult::Added;
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
    m_copyBtn->setText(tr("Copied"));
    QTimer::singleShot(1200, this, [this] { m_copyBtn->setText(tr("Copy list")); });
}

// What older versions did to a saved entry: lower-case it and cut off a scheme and path.
QString DomainListEditor::tidyDomain(QString text)
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

QString DomainListEditor::normalize(const QString& text, bool fromConfig) const
{
    if (m_kind == Kind::Apps)
        return AppMatcher::normalizeEntry(text);
    if (!fromConfig)
        return normalizeDomain(text);
    const QString strict = normalizeDomain(text);
    return strict.isEmpty() ? tidyDomain(text) : strict;
}

void DomainListEditor::browseForApps()
{
#ifdef Q_OS_MACOS
    const QStringList files = QFileDialog::getOpenFileNames(this, tr("Choose apps"), "/Applications",
                                                            tr("Applications (*.app)"));
#else
    const QStringList files = QFileDialog::getOpenFileNames(this, tr("Choose programs"), QString(),
                                                            tr("Programs (*.exe)"));
#endif
    QStringList invalid;
    for (const QString& file : files)
        if (addDomain(file) == AddResult::Invalid)
            invalid << file;
    if (!invalid.isEmpty())
        flash(tr("Not a valid app name: %1").arg(invalid.join(", ")));
}

void DomainListEditor::pickRunningApps()
{
    const QList<RunningApps::Entry> running = RunningApps::list();

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Running programs"));
    dialog.resize(380, 460);
    auto* lay = new QVBoxLayout(&dialog);
    auto* hint = new QLabel(running.isEmpty()
        ? tr("No programs found. Type the name in the box instead.")
        : tr("Pick the programs to add. Start the program first if it is not listed."));
    hint->setObjectName("note");
    hint->setWordWrap(true);
    lay->addWidget(hint);

    auto* filter = new QLineEdit;
    filter->setPlaceholderText(tr("Search"));
    filter->setClearButtonEnabled(true);
    lay->addWidget(filter);

    auto* list = new QListWidget;
    list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    for (const RunningApps::Entry& app : running) {
        auto* item = new QListWidgetItem(app.label, list);
        item->setData(Qt::UserRole, app.entry);
        if (m_domains.contains(app.entry))
            item->setFlags(item->flags() & ~Qt::ItemIsEnabled);   // already in the list
    }
    lay->addWidget(list, 1);

    connect(filter, &QLineEdit::textChanged, &dialog, [list](const QString& text) {
        for (int i = 0; i < list->count(); ++i)
            list->item(i)->setHidden(!list->item(i)->text().contains(text, Qt::CaseInsensitive));
    });
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Add"));
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(list, &QListWidget::itemDoubleClicked, &dialog, &QDialog::accept);
    lay->addWidget(buttons);

    if (dialog.exec() != QDialog::Accepted)
        return;
    for (const QListWidgetItem* item : list->selectedItems())
        addDomain(item->data(Qt::UserRole).toString());
}

void DomainListEditor::flash(const QString& message)
{
    m_count->setText(message);
    m_flashTimer->start();
}

void DomainListEditor::updateState()
{
    const int n = m_domains.size();
    const bool apps = m_kind == Kind::Apps;
    m_count->setText(n == 0 ? (apps ? tr("No apps") : tr("No sites"))
                   : n == 1 ? (apps ? tr("1 app") : tr("1 site"))
                            : (apps ? tr("%1 apps") : tr("%1 sites")).arg(n));
    m_scroll->setVisible(n > 0);
    m_empty->setVisible(n == 0);
    m_copyBtn->setEnabled(n > 0);
    m_clearBtn->setEnabled(n > 0);
}
