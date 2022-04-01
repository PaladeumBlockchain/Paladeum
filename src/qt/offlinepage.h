#ifndef YONA_QT_OFFLINEPAGE_H
#define YONA_QT_OFFLINEPAGE_H

#include <QWidget>
#include <memory>

class ClientModel;
class TransactionFilterProxy;
class PlatformStyle;
class WalletModel;

namespace Ui {
    class OfflinePage;
}

QT_BEGIN_NAMESPACE
class QModelIndex;
QT_END_NAMESPACE

/** Mining page widget */
class OfflinePage : public QWidget
{
    Q_OBJECT

public:
    explicit OfflinePage(const PlatformStyle *platformStyle, QWidget *parent = nullptr);
    ~OfflinePage();

    void setClientModel(ClientModel *clientModel);
    void setWalletModel(WalletModel *walletModel);

private:
    Ui::OfflinePage *ui;
    ClientModel *clientModel;
    WalletModel *walletModel;

private Q_SLOTS:
    void on_createButton_clicked();

private Q_SLOTS:
};

#endif // YONA_QT_OFFLINEPAGE_H
