// Copyright (c) 2020 Hans Schmidt
// Copyright (c) 2017-2018 The Particl Core developers
// Copyright (c) 2026 The Paladeum Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef PLB_QT_MNEMONICDIALOG_H
#define PLB_QT_MNEMONICDIALOG_H

#include <QDialog>

#include <support/allocators/secure.h>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QStackedWidget;
class QTimer;

/**
 * The recovery words of a wallet that is being created (the first start, and Create Wallet):
 * new words, shown so that they can be written down, or the words of a wallet to restore.
 * The node waits for the answer (ShowMnemonic) and makes the keys of the wallet from the
 * words and the optional passphrase (my_words, my_passphrase, see CWallet::GenerateNewSeed).
 */
class MnemonicDialog : public QDialog
{
    Q_OBJECT

public:
    explicit MnemonicDialog(QWidget *parent = nullptr);
    ~MnemonicDialog();

    /** Show the recovery words of a wallet (Settings, Show recovery words) the way they were shown when it was made */
    static void showWords(QWidget *parent, const SecureString &words, const SecureString &passphrase);

public Q_SLOTS:
    /** There is no way past this step but an answer: the wallet is being made from it */
    void reject() override;

private Q_SLOTS:
    void showNewWords();
    void newWordsChecked();
    void validateWords();
    void createNew();
    void restore();

private:
    enum Page { Choose = 0, New = 1, Restore = 2 };
    QWidget *choosePage();
    QWidget *newPage();
    QWidget *restorePage();
    /** Hand the words and the passphrase to the wallet and close */
    void finish(const std::string &words, const std::string &passphrase);
    void wipe();

    QStackedWidget *stack;
    // new words
    std::string generated;
    QList<QLabel*> wordLabels;
    QCheckBox *writtenDown;
    QLineEdit *newPassphrase;
    QPushButton *createButton;
    // restore
    QPlainTextEdit *restoreWords;
    QLabel *restoreStatus;
    QLineEdit *restorePassphrase;
    QPushButton *restoreButton;
    QTimer *validateTimer;
    bool restoreValid;
};

#endif // PLB_QT_MNEMONICDIALOG_H
