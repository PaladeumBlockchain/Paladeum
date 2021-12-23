#include "yonaunits.h"

#include <QAbstractTableModel>
#include <QStringList>

class PlatformStyle;
class MyRestrictedTokenRecord;
class MyRestrictedTokensTablePriv;
class WalletModel;

class CWallet;

/** UI model for the transaction table of a wallet.
 */
class MyRestrictedTokensTableModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    explicit MyRestrictedTokensTableModel(const PlatformStyle *platformStyle, CWallet* wallet, WalletModel *parent = 0);
    ~MyRestrictedTokensTableModel();

    enum ColumnIndex {
        Date = 0,
        Type = 1,
        ToAddress = 2,
        TokenName = 3
    };

    /** Roles to get specific information from a transaction row.
        These are independent of column.
    */
    enum RoleIndex {
        /** Type of transaction */
                TypeRole = Qt::UserRole,
        /** Date and time this transaction was created */
                DateRole,
        /** Watch-only boolean */
                WatchonlyRole,
        /** Watch-only icon */
                WatchonlyDecorationRole,
        /** Address of transaction */
                AddressRole,
        /** Label of address related to transaction */
                LabelRole,
        /** Unique identifier */
                TxIDRole,
        /** Transaction hash */
                TxHashRole,
        /** Transaction data, hex-encoded */
                TxHexRole,
        /** Whole transaction as plain text */
                TxPlainTextRole,
        /** Unprocessed icon */
                RawDecorationRole,
        /** YONA or name of an token */
                TokenNameRole,
    };

    int rowCount(const QModelIndex &parent) const;
    int columnCount(const QModelIndex &parent) const;
    QVariant data(const QModelIndex &index, int role) const;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const;
    QModelIndex index(int row, int column, const QModelIndex & parent = QModelIndex()) const;
    bool processingQueuedTransactions() const { return fProcessingQueuedTransactions; }

private:
    CWallet* wallet;
    WalletModel *walletModel;
    QStringList columns;
    MyRestrictedTokensTablePriv *priv;
    bool fProcessingQueuedTransactions;
    const PlatformStyle *platformStyle;

    void subscribeToCoreSignals();
    void unsubscribeFromCoreSignals();

    QString lookupAddress(const std::string &address, bool tooltip) const;
    QVariant addressColor(const MyRestrictedTokenRecord *wtx) const;
    QString formatTxDate(const MyRestrictedTokenRecord *wtx) const;
    QString formatTxType(const MyRestrictedTokenRecord *wtx) const;
    QString formatTxToAddress(const MyRestrictedTokenRecord *wtx, bool tooltip) const;
    QString formatTooltip(const MyRestrictedTokenRecord *rec) const;
    QVariant txStatusDecoration(const MyRestrictedTokenRecord *wtx) const;
    QVariant txWatchonlyDecoration(const MyRestrictedTokenRecord *wtx) const;
    QVariant txAddressDecoration(const MyRestrictedTokenRecord *wtx) const;

public Q_SLOTS:
    void updateMyRestrictedTokens(const QString &address, const QString& token_name, const int type, const qint64 date);
            /* New transaction, or transaction changed status */

    /** Updates the column title to "Amount (DisplayUnit)" and emits headerDataChanged() signal for table headers to react. */
    /* Needed to update fProcessingQueuedTransactions through a QueuedConnection */
    void setProcessingQueuedTransactions(bool value) { fProcessingQueuedTransactions = value; }

    friend class MyRestrictedTokensTablePriv;
};


