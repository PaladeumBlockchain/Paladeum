// Copyright (c) 2020 Hans Schmidt
// Copyright (c) 2017-2018 The Particl Core developers
// Copyright (c) 2026 The Paladeum Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/mnemonicdialog.h>

#include <qt/cellstyle.h>
#include <qt/firstrun.h>
#include <qt/guiutil.h>

#include <bip39.h>
#include <support/cleanse.h>
#include <wallet/bip39.h>
#include <wallet/wallet.h>

#include <QAbstractButton>
#include <QCheckBox>
#include <QEvent>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegExp>
#include <QStackedWidget>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

namespace {

/** One of the two ways on the first page: a card with a title and a line, that leads on. */
class OptionCard : public QAbstractButton
{
public:
    OptionCard(const QString &title, const QString &detail, QWidget *parent) :
        QAbstractButton(parent), detail(detail), hovered(false)
    {
        setText(title);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::StrongFocus);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setFixedHeight(76);
        setAccessibleName(title);
        setAccessibleDescription(detail);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        const GUIUtil::Theme &t = GUIUtil::theme();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const bool lit = hovered || hasFocus() || isDown();
        // a panel with cut corners; under the pointer or focused, the ring of the selection
        CellStyle::paintCut(&p, QRectF(rect()), 12, isDown() ? t.accentSoft : t.surface, lit ? t.accent : QColor(), 2);
        p.setPen(t.text);
        p.setFont(GUIUtil::displayFont(17, QFont::Medium));
        p.drawText(QRect(20, 16, width() - 72, 22), Qt::AlignLeft | Qt::AlignVCenter, text());
        p.setPen(t.textSecondary);
        p.setFont(GUIUtil::appFont(13));
        p.drawText(QRect(20, 40, width() - 72, 20), Qt::AlignLeft | Qt::AlignVCenter,
                   QFontMetrics(GUIUtil::appFont(13)).elidedText(detail, Qt::ElideRight, width() - 72));
        p.drawPixmap(width() - 36, (height() - 16) / 2, GUIUtil::lineIcon("chevron-right", 16, lit ? t.accent : t.textSecondary, 2.0));
    }
    void enterEvent(QEvent *event) override { hovered = true; update(); QAbstractButton::enterEvent(event); }
    void leaveEvent(QEvent *event) override { hovered = false; update(); QAbstractButton::leaveEvent(event); }

private:
    QString detail;
    bool hovered;
};

QLabel *wrapped(const QString &text, const char *role, QWidget *parent)
{
    QLabel *label = new QLabel(text, parent);
    label->setWordWrap(true);
    if (role)
        label->setProperty("role", role);
    return label;
}

QPushButton *button(const QString &text, bool primary, QWidget *parent)
{
    QPushButton *b = new QPushButton(text, parent);
    b->setFixedHeight(40);
    b->setMinimumWidth(110);
    b->setAutoDefault(false);
    if (primary)
        b->setProperty("primary", true);
    return b;
}

void setRole(QLabel *label, const char *role)
{
    label->setProperty("role", role);
    label->style()->unpolish(label);
    label->style()->polish(label);
}

/** The words, numbered, in columns of four (1-4, 5-8, 9-12, ...), on a tile */
QFrame *wordCard(int count, QList<QLabel*> *labels, QWidget *parent)
{
    QFrame *card = new QFrame(parent);
    card->setProperty("role", "tile");
    QGridLayout *grid = new QGridLayout(card);
    grid->setContentsMargins(20, 16, 20, 16);
    grid->setHorizontalSpacing(24);
    grid->setVerticalSpacing(12);
    for (int i = 0; i < count; ++i) {
        QWidget *cell = new QWidget(card);
        QHBoxLayout *cellLayout = new QHBoxLayout(cell);
        cellLayout->setContentsMargins(0, 0, 0, 0);
        cellLayout->setSpacing(8);
        QLabel *number = new QLabel(QString::number(i + 1), cell);
        number->setFont(GUIUtil::monoFont(12));
        number->setProperty("role", "caption");
        number->setFixedWidth(20);
        number->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        QLabel *word = new QLabel(cell);
        word->setFont(GUIUtil::monoFont(16, QFont::DemiBold));
        word->setTextInteractionFlags(Qt::NoTextInteraction);
        cellLayout->addWidget(number);
        cellLayout->addWidget(word, 1);
        grid->addWidget(cell, i % 4, i / 4);
        labels->append(word);
    }
    return card;
}

} // namespace

void MnemonicDialog::showWords(QWidget *parent, const SecureString &words, const SecureString &passphrase)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(tr("Recovery words"));
    dialog.setMinimumWidth(580);
    QVBoxLayout *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(28, 26, 28, 22);
    layout->setSpacing(12);
    QLabel *title = new QLabel(tr("The recovery words of this wallet"), &dialog);
    title->setProperty("role", "title");
    layout->addWidget(title);
    layout->addWidget(wrapped(tr("Whoever has these words can take the coins of this wallet. Show them only where nobody can see the screen, and keep them on paper, not in a photo or a file."), "secondary", &dialog));
    const QStringList list = QString::fromStdString(std::string(words.begin(), words.end())).split(' ', QString::SkipEmptyParts);
    QList<QLabel*> labels;
    layout->addWidget(wordCard(list.size(), &labels, &dialog));
    for (int i = 0; i < labels.size(); ++i)
        labels[i]->setText(list[i]);
    if (!passphrase.empty()) {
        // the passphrase of the words is part of them: hidden until asked for
        QLabel *passLabel = new QLabel(tr("Passphrase of the words"), &dialog);
        passLabel->setProperty("role", "fieldLabel");
        layout->addWidget(passLabel);
        QHBoxLayout *passRow = new QHBoxLayout();
        QLabel *pass = new QLabel(QString(QChar(0x2022)).repeated(10), &dialog);
        pass->setFont(GUIUtil::monoFont(15, QFont::DemiBold));
        QPushButton *show = new QPushButton(tr("Show"), &dialog);
        show->setProperty("look", "link");
        show->setCursor(Qt::PointingHandCursor);
        const QString passText = QString::fromStdString(std::string(passphrase.begin(), passphrase.end()));
        QObject::connect(show, &QPushButton::clicked, pass, [pass, show, passText] { pass->setText(passText); show->hide(); });
        passRow->addWidget(pass, 1);
        passRow->addWidget(show);
        layout->addLayout(passRow);
        layout->addWidget(wrapped(tr("The words restore this wallet only together with this passphrase."), "caution", &dialog));
    }
    QHBoxLayout *buttons = new QHBoxLayout();
    buttons->addStretch(1);
    QPushButton *close = button(tr("Close"), true, &dialog);
    close->setDefault(true);
    QObject::connect(close, &QPushButton::clicked, &dialog, &QDialog::accept);
    buttons->addWidget(close);
    layout->addLayout(buttons);
    dialog.exec();
    for (QLabel *label : labels)
        label->clear();
}

MnemonicDialog::MnemonicDialog(QWidget *parent) :
    QDialog(parent),
    restoreValid(false)
{
    setWindowTitle(tr("Recovery words"));
    // the node waits for an answer: no way out of this window but one of its buttons
    setWindowFlags((windowFlags() | Qt::CustomizeWindowHint | Qt::WindowTitleHint) & ~Qt::WindowCloseButtonHint & ~Qt::WindowContextHelpButtonHint);
    setMinimumWidth(620);
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(28, 26, 28, 22);
    stack = new QStackedWidget(this);
    stack->addWidget(choosePage());
    stack->addWidget(newPage());
    stack->addWidget(restorePage());
    layout->addWidget(stack);
    connect(stack, &QStackedWidget::currentChanged, this, [this] { GUIUtil::fitDialogHeight(this); });
}

MnemonicDialog::~MnemonicDialog()
{
    wipe();
}

void MnemonicDialog::reject()
{
    // Escape and the close button of the window: the wallet cannot be made without words
}

QWidget *MnemonicDialog::choosePage()
{
    QWidget *page = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);
    layout->addWidget(new WizardHeader(WizardHeader::Words, tr("A new wallet"),
        tr("A Paladeum wallet is made from 12 recovery words. Make new words for it, or restore a wallet from the words you have."), page));
    layout->addSpacing(4);
    OptionCard *create = new OptionCard(tr("Create a new wallet"), tr("New recovery words are made and shown to you"), page);
    OptionCard *restore = new OptionCard(tr("Restore a wallet"), tr("From the words of a Paladeum wallet or another BIP39 wallet"), page);
    layout->addWidget(create);
    layout->addWidget(restore);
    layout->addStretch(1);
    connect(create, &QAbstractButton::clicked, this, &MnemonicDialog::showNewWords);
    connect(restore, &QAbstractButton::clicked, this, [this] {
        stack->setCurrentIndex(Restore);
        restoreWords->setFocus();
    });
    return page;
}

QWidget *MnemonicDialog::newPage()
{
    QWidget *page = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);
    layout->addWidget(new WizardHeader(WizardHeader::Words, tr("Write down the recovery words"),
        tr("These 12 words are the wallet. Whoever has them can take its coins, and they bring the wallet back if this computer or the wallet file is lost."), page));

    // three columns of four, read down each column: 1-4, 5-8, 9-12
    layout->addWidget(wordCard(12, &wordLabels, page));
    layout->addWidget(wrapped(tr("Write them on paper in this order and keep the paper somewhere safe. Do not keep them in a photo, a file or an e-mail, and never type them into a website."), "secondary", page));

    // a passphrase for the words: for few people, and a second thing that must not be lost
    QPushButton *addPassphrase = new QPushButton(tr("Add a passphrase to the words"), page);
    addPassphrase->setProperty("look", "link");
    addPassphrase->setCursor(Qt::PointingHandCursor);
    QWidget *passphraseBox = new QWidget(page);
    QVBoxLayout *passphraseLayout = new QVBoxLayout(passphraseBox);
    passphraseLayout->setContentsMargins(0, 0, 0, 0);
    passphraseLayout->setSpacing(6);
    newPassphrase = new QLineEdit(passphraseBox);
    newPassphrase->setEchoMode(QLineEdit::Password);
    newPassphrase->setMinimumHeight(40);
    newPassphrase->setPlaceholderText(tr("Passphrase of the words (optional)"));
    passphraseLayout->addWidget(newPassphrase);
    passphraseLayout->addWidget(wrapped(tr("With a passphrase the words alone do not restore the wallet: the words and the passphrase are both needed. If it is forgotten, the coins are lost."), "caution", passphraseBox));
    passphraseBox->hide();
    connect(addPassphrase, &QPushButton::clicked, this, [this, addPassphrase, passphraseBox] {
        addPassphrase->hide();
        passphraseBox->show();
        newPassphrase->setFocus();
        GUIUtil::fitDialogHeight(this);
    });
    QHBoxLayout *linkRow = new QHBoxLayout();
    linkRow->addWidget(addPassphrase);
    linkRow->addStretch(1);
    layout->addLayout(linkRow);
    layout->addWidget(passphraseBox);

    writtenDown = new QCheckBox(tr("I have written down the 12 words in this order"), page);
    layout->addSpacing(4);
    layout->addWidget(writtenDown);
    layout->addStretch(1);

    QHBoxLayout *buttons = new QHBoxLayout();
    buttons->setSpacing(8);
    QPushButton *back = button(tr("Back"), false, page);
    QPushButton *other = button(tr("Other words"), false, page);
    other->setToolTip(tr("Make other words, for example if someone may have seen these"));
    createButton = button(tr("Continue"), true, page);
    createButton->setEnabled(false);
    buttons->addWidget(back);
    buttons->addStretch(1);
    buttons->addWidget(other);
    buttons->addWidget(createButton);
    layout->addLayout(buttons);
    connect(back, &QPushButton::clicked, this, [this] { stack->setCurrentIndex(Choose); });
    connect(other, &QPushButton::clicked, this, &MnemonicDialog::showNewWords);
    connect(writtenDown, &QCheckBox::toggled, this, &MnemonicDialog::newWordsChecked);
    connect(createButton, &QPushButton::clicked, this, &MnemonicDialog::createNew);
    return page;
}

QWidget *MnemonicDialog::restorePage()
{
    QWidget *page = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);
    layout->addWidget(new WizardHeader(WizardHeader::Words, tr("Restore a wallet"),
        tr("Enter the recovery words of the wallet in their order. The words of Paladeum Core, of Paladeum 0.1.x wallets and of other BIP39 wallets can be used."), page));
    layout->addSpacing(4);
    QLabel *wordsLabel = new QLabel(tr("Recovery words"), page);
    wordsLabel->setProperty("role", "fieldLabel");
    layout->addWidget(wordsLabel);
    restoreWords = new QPlainTextEdit(page);
    restoreWords->setFont(GUIUtil::monoFont(14));
    restoreWords->setFixedHeight(92);
    restoreWords->setUndoRedoEnabled(false);
    restoreWords->setTabChangesFocus(true);
    restoreWords->setPlaceholderText(tr("word1 word2 word3 ..."));
    layout->addWidget(restoreWords);
    restoreStatus = wrapped(QString(), "caption", page);
    layout->addWidget(restoreStatus);
    layout->addSpacing(4);
    QLabel *passLabel = new QLabel(tr("Passphrase of the words (optional)"), page);
    passLabel->setProperty("role", "fieldLabel");
    layout->addWidget(passLabel);
    restorePassphrase = new QLineEdit(page);
    restorePassphrase->setEchoMode(QLineEdit::Password);
    restorePassphrase->setMinimumHeight(40);
    restorePassphrase->setPlaceholderText(tr("Only if the wallet was set up with one"));
    layout->addWidget(restorePassphrase);
    layout->addWidget(wrapped(tr("Type the words only on a computer you trust. The wallet finds its coins once the synchronization has reached the blocks that hold them."), "caption", page));
    layout->addStretch(1);

    QHBoxLayout *buttons = new QHBoxLayout();
    buttons->setSpacing(8);
    QPushButton *back = button(tr("Back"), false, page);
    restoreButton = button(tr("Restore"), true, page);
    restoreButton->setEnabled(false);
    buttons->addWidget(back);
    buttons->addStretch(1);
    buttons->addWidget(restoreButton);
    layout->addLayout(buttons);

    validateTimer = new QTimer(this);
    validateTimer->setSingleShot(true);
    validateTimer->setInterval(150);
    connect(restoreWords, &QPlainTextEdit::textChanged, validateTimer, static_cast<void (QTimer::*)()>(&QTimer::start));
    connect(validateTimer, &QTimer::timeout, this, &MnemonicDialog::validateWords);
    connect(back, &QPushButton::clicked, this, [this] { stack->setCurrentIndex(Choose); });
    connect(restoreButton, &QPushButton::clicked, this, &MnemonicDialog::restore);
    validateWords();
    return page;
}

void MnemonicDialog::showNewWords()
{
    if (!generated.empty())
        memory_cleanse(&generated[0], generated.size());
    const SecureString words = CMnemonic::Generate(128);
    generated.assign(words.begin(), words.end());
    const QStringList list = QString::fromStdString(generated).split(' ', QString::SkipEmptyParts);
    for (int i = 0; i < wordLabels.size(); ++i)
        wordLabels[i]->setText(i < list.size() ? list[i] : QString());
    // new words have not been written down yet
    writtenDown->setChecked(false);
    newWordsChecked();
    stack->setCurrentIndex(New);
}

void MnemonicDialog::newWordsChecked()
{
    createButton->setEnabled(writtenDown->isChecked() && !generated.empty());
}

void MnemonicDialog::validateWords()
{
    QString text = restoreWords->toPlainText();
    QByteArray utf8 = text.toUtf8();
    std::string raw(utf8.constData(), utf8.size());
    text.fill(QChar(' '));
    memory_cleanse(utf8.data(), utf8.size());
    std::string normalized, language, error;
    QString status;
    const char *role = "caption";
    restoreValid = false;
    if (!bip39::Normalize(raw, true, normalized)) {
        status = tr("Only the English words of BIP39 can be used here.");
        role = "error";
    } else {
        const int count = normalized.empty() ? 0 : 1 + std::count(normalized.begin(), normalized.end(), ' ');
        bool checksumOk = false;
        const bool wordsOk = count > 0 && bip39::CheckMnemonicWords(normalized, language, error, checksumOk);
        if (count == 0) {
            status = tr("12, 18 or 24 words.");
        } else if (CMnemonic::Check(SecureString(normalized.begin(), normalized.end()))) {
            restoreValid = true;
            // Paladeum 0.1.x showed "paladeum" in place of the BIP39 word "raven"
            const bool oldWords = (" " + normalized + " ").find(" paladeum ") != std::string::npos;
            status = tr("%n words, correct.", "", count) + " " + (oldWords ? tr("The words of a Paladeum 0.1.x wallet.") : tr("BIP39 words."));
            role = "ok";
        } else if (count != 12 && count != 18 && count != 24) {
            status = tr("%n words so far. 12, 18 or 24 are needed.", "", count);
        } else if (!wordsOk) {
            QRegExp unknown("word (\\d+) ");
            if (unknown.indexIn(QString::fromStdString(error)) >= 0)
                status = tr("Word %1 is not in the word list. Check its spelling.").arg(unknown.cap(1));
            else
                status = tr("Only the English words of BIP39 can be used here.");
            role = "error";
        } else if (!checksumOk) {
            status = tr("The words do not fit together (checksum). A word is mistyped, missing or out of order.") + " " +
                     tr("The earlier Paladeum mobile app made its words without this checksum; such words cannot make a wallet here. Create a new wallet and add the keys of those words to it with Restore from recovery words (start the program with -mnemonicrestore).");
            role = "error";
        } else {
            status = tr("Only the English words of BIP39 can be used here.");
            role = "error";
        }
    }
    if (!raw.empty())
        memory_cleanse(&raw[0], raw.size());
    if (!normalized.empty())
        memory_cleanse(&normalized[0], normalized.size());
    restoreStatus->setText(status);
    setRole(restoreStatus, role);
    restoreButton->setEnabled(restoreValid);
    GUIUtil::fitDialogHeight(this);
}

void MnemonicDialog::createNew()
{
    if (generated.empty() || !writtenDown->isChecked())
        return;
    const QByteArray pass = newPassphrase->text().toUtf8();
    finish(generated, std::string(pass.constData(), pass.size()));
}

void MnemonicDialog::restore()
{
    validateWords();
    if (!restoreValid)
        return;
    const QByteArray utf8 = restoreWords->toPlainText().toUtf8();
    std::string normalized;
    if (!bip39::Normalize(std::string(utf8.constData(), utf8.size()), true, normalized))
        return;
    const QByteArray pass = restorePassphrase->text().toUtf8();
    finish(normalized, std::string(pass.constData(), pass.size()));
    if (!normalized.empty())
        memory_cleanse(&normalized[0], normalized.size());
}

void MnemonicDialog::finish(const std::string &words, const std::string &passphrase)
{
    // the wallet makes its seed from these (CWallet::GenerateNewSeed) and clears them
    my_words = words;
    my_passphrase = passphrase;
    wipe();
    QDialog::accept();
}

void MnemonicDialog::wipe()
{
    if (!generated.empty())
        memory_cleanse(&generated[0], generated.size());
    generated.clear();
    for (QLabel *label : wordLabels)
        label->clear();
    for (QLineEdit *edit : {newPassphrase, restorePassphrase}) {
        edit->setText(QString(" ").repeated(edit->text().size()));
        edit->clear();
    }
    const bool blocked = restoreWords->blockSignals(true);
    restoreWords->setPlainText(QString(" ").repeated(restoreWords->toPlainText().size()));
    restoreWords->clear();
    restoreWords->blockSignals(blocked);
}
