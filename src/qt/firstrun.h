// Copyright (c) 2026 The Paladeum Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef PLB_QT_FIRSTRUN_H
#define PLB_QT_FIRSTRUN_H

#include <QDialog>
#include <QWidget>


/**
 * The screens of the first start are one sequence: where the data goes and
 * the language (Intro), the recovery words of the new wallet (MnemonicDialog),
 * the passphrase of the wallet (AskPassphraseDialog), the backup
 * (BackupPromptDialog). Each of them begins with this header: the logo, a
 * title and the steps with the current one marked.
 */
class WizardHeader : public QWidget
{
    Q_OBJECT

public:
    enum Step { Start = 0, Words = 1, Passphrase = 2, Backup = 3 };
    WizardHeader(Step step, const QString &title, const QString &subtitle, QWidget *parent);

    /** The logo of the program with rounded corners */
    static QPixmap logo(int size, qreal devicePixelRatio);
};

#endif // PLB_QT_FIRSTRUN_H
