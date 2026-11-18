// Copyright (c) 2011-2016 The Bitcoin Core developers
// Copyright (c) 2021-2022 The Paladeum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef PLB_QT_GUIUTIL_H
#define PLB_QT_GUIUTIL_H

#include "amount.h"
#include "fs.h"

#include <QEvent>
#include <QHeaderView>
#include <QMessageBox>
#include <QObject>
#include <QProgressBar>
#include <QColor>
#include <QFont>
#include <QList>
#include <QStyledItemDelegate>
#include <QString>
#include <QTableView>
#include <QLabel>

class QValidatedLineEdit;
class SendCoinsRecipient;

QT_BEGIN_NAMESPACE
class QApplication;
class QAbstractItemView;
class QDateTime;
class QFont;
class QPainter;
class QPixmap;
class QLineEdit;
class QUrl;
class QComboBox;
class QWidget;
class QGraphicsDropShadowEffect;
QT_END_NAMESPACE

/** Utility functions used by the Paladeum Qt UI.
 */
namespace GUIUtil
{
    /**
     * Colors of the application theme ("Cell" of the Paladeum design guide), light or dark.
     * Panels have no borders: the ground and the cell colors separate them.
     */
    struct Theme {
        QColor bg;            //!< ground: the window background
        QColor surface;       //!< cell: panels, cards, the rail; fields and quiet buttons on the ground
        QColor surfaceAlt;    //!< raised: fields and quiet buttons on a cell, the active tab
        QColor raisedHover;   //!< a raised fill under the pointer
        QColor border;        //!< the few remaining outlines (menus, check boxes)
        QColor borderStrong;  //!< unchecked check boxes, scroll bar handles
        QColor line;          //!< hairlines between rows inside a panel
        QColor text;
        QColor textSecondary;
        QColor label;         //!< field labels, quiet button text
        QColor muted;         //!< meta and captions only
        QColor textDisabled;
        QColor fill;          //!< mark blue: selection, checked boxes, the secondary filled action
        QColor fillHover;
        QColor fillPressed;
        QColor onFill;        //!< text on mark blue
        QColor action;        //!< action yellow: the one primary action of a page
        QColor actionHover;
        QColor actionPressed;
        QColor onAction;      //!< text on action yellow
        QColor accent;        //!< links, focus rings and chart series (mark blue, lighter in the dark theme)
        QColor accentHover;
        QColor accentPressed;
        QColor accentSoft;    //!< selection tint
        QColor onAccent;      //!< text on the accent color
        QColor positive;      //!< received amounts
        QColor positiveSoft;
        QColor negative;      //!< sent amounts
        QColor negativeSoft;
        QColor ok;            //!< confirmed, valid
        QColor okSoft;
        QColor error;         //!< invalid input, failed transactions
        QColor errorSoft;
        QColor warnBg;
        QColor warnText;
        QColor warnBorder;
        QColor chip;
        QColor chipText;
        // the rail on the left
        QColor sideBg;
        QColor sideText;
        QColor sideStrong;
        QColor sideOnBg;      //!< active navigation item
        QColor sideCard;      //!< hairlines of the rail
        QColor sideTrack;     //!< track of the progress bar
        QColor sideBar;       //!< progress, a switch that is on
        QColor sideOff;       //!< switch that is off
    };
    /** The theme in use (light unless the dark theme was chosen in the settings). */
    const Theme& theme();
    /** Is the dark theme in use? */
    bool darkTheme();
    /** Choose the theme; only before applyTheme() and before any widget is created. */
    void setDarkTheme(bool dark);
    /** Name of the setting (in the settings of the main network) that chooses the dark theme. */
    extern const char* const DARK_THEME_SETTING;
    /**
     * A number in few characters for a narrow place: 1,234 / 123 k / 4.57 M / 1.23 B, and small
     * numbers with three significant digits (0.00123, 4.66e-10). The exact value belongs in a tool tip.
     */
    QString compactNumber(double value);
    /** Register the bundled fonts (Inter, Space Grotesk, JetBrains Mono). Call once, before any widget is created. */
    void loadAppFonts();
    /**
     * The interface font (Inter) at a pixel size and weight. The theme has two weights, 400 and 500,
     * and 600 for rare emphasis: DemiBold is drawn as 500 and Bold as 600.
     */
    QFont appFont(int pixelSize = 14, int weight = QFont::Normal);
    /** The fixed-width font (JetBrains Mono) for addresses, transaction ids, hashes, keys and amounts in lists. */
    QFont monoFont(int pixelSize = 13, int weight = QFont::Normal);
    /** The font of titles and figures (Space Grotesk), 400 or 500; large sizes are set a little tighter. */
    QFont displayFont(int pixelSize, int weight = QFont::Normal);
    /** Apply style, palette, font and style sheet of the theme to the application. */
    void applyTheme(QApplication& app);
    /** Style sheet snippets that follow the theme. */
    QString labelColorStyle();
    /** A warning in the text of a form, in the error color */
    QString warningLabelColorStyle();
    QString invalidFieldStyle();
    QString validFieldStyle();
    /** Style sheet of a card frame: no background and no border of its own (see makeCard()). */
    QString cardFrameStyle(const QString& selector = ".QFrame");
    /** Make a frame a card: a panel in the cell color with cut corners, painted by the theme. */
    void makeCard(QWidget* frame);

    /** First and last four characters of an address ("Kj6i…qVgR"). Text that is not a valid address is returned as it is. */
    QString shortAddress(const QString& address);
    /** Shortened transaction id or block hash. */
    QString shortHash(const QString& hash, int head = 12, int tail = 8);
    /** Put thousands separators (commas) into the integer part of a formatted number. For display only. */
    QString withThousands(const QString& number);
    /** A line icon drawn in the given color. Names: overview, send, receive, history, tokens, explorer,
        create, manage, addressbook, paste, download, search, chevron, plus, remove, copy, lock. */
    /** Fill a combo box with the languages of the bundled translations, each in its own
        language, after "System default". The item data is the locale name ("" for the default). */
    void fillLanguageCombo(QComboBox* combo);
    /** Ask for the program to be started again after it has shut down (see main()). */
    /** Make a window high enough for its texts that wrap (layouts ask for too little by themselves) */
    void fitDialogHeight(QWidget* dialog);

    /** Start the program again when it has ended; extraArguments are added to those of this start */
    void requestRestart(const QStringList& extraArguments = QStringList());
    QStringList restartArguments();
    bool restartRequested();
    QPixmap lineIcon(const QString& name, int size, const QColor& color, qreal strokeWidth = 2.0);
    /** The avatar of a token, by its root name (before '/', '#' and '!'): a hexagon in the raised color
        with the initial; for PLB the Paladeum mark. The hexagon is as high as the square of the pixmap. */
    QPixmap tokenLogo(const QString& tokenName, int size);
    /** Paint the bullet of a list row, a small hexagon (12 x 14) centered in the rectangle:
        mark blue for PLB (the lighter link blue in the dark theme), the secondary grey for a token. */
    void paintHexBullet(QPainter* painter, const QRect& rect, bool token);
    /** HTML that shows already escaped text in the fixed-width font. */
    QString monoHtml(const QString& escapedText);
    /** Tool tip text with the value in the fixed-width font, optionally with a caption line above. */
    QString monoToolTip(const QString& value, const QString& caption = QString());
    /** Date and time in the form used in tables: 2024-04-13 16:06 */
    QString isoDateTime(const QDateTime& dateTime, bool seconds = false);
    /** Is there a bundled logo for the token (looked up by its root name)? */
    bool hasTokenLogo(const QString& tokenName);
    /**
     * Paint an amount: the fraction (the point and the digits after it) at 60 % of the size of
     * the integer part, on the same base line. Every amount on the pages is painted this way.
     * Returns the width used.
     */
    int paintAmount(QPainter* painter, const QRect& rect, const QString& amount, const QFont& font, const QColor& color,
                    Qt::Alignment align = Qt::AlignRight | Qt::AlignVCenter);
    /** Width of an amount painted by paintAmount(). */
    int amountWidth(const QString& amount, const QFont& font);
    /** Paint a chip (small label with cut corners) at the position; returns its width. */
    int paintChip(QPainter* painter, int x, int centerY, const QString& text);
    /** Paint a badge (cut corners) with its own colors, right- or left-aligned in the rectangle. */
    void paintBadge(QPainter* painter, const QRect& rect, const QString& text, const QColor& fg, const QColor& bg, Qt::Alignment align = Qt::AlignLeft);

    // Get the font for the sub labels
    QFont getSubLabelFont();
    QFont getSubLabelFontBolded();

    // Get the font for the main labels
    QFont getTopLabelFont();
    QFont getTopLabelFontBolded();
    QFont getTopLabelFont(int weight, int pxsize);

    QGraphicsDropShadowEffect* getShadowEffect();

    // Create human-readable string from date
    QString dateTimeStr(const QDateTime &datetime);
    QString dateTimeStr(qint64 nTime);

    // Return a monospace font
    QFont fixedPitchFont();

    // Set up widgets for address and amounts
    void setupAddressWidget(QValidatedLineEdit *widget, QWidget *parent);
    void setupAmountWidget(QLineEdit *widget, QWidget *parent);

    // Parse "paladeum:" URI into recipient object, return true on successful parsing
    bool parsePaladeumURI(const QUrl &uri, SendCoinsRecipient *out);
    bool parsePaladeumURI(QString uri, SendCoinsRecipient *out);
    QString formatPaladeumURI(const SendCoinsRecipient &info);

    // Returns true if given address+amount meets "dust" definition
    bool isDust(const QString& address, const CAmount& amount);

    // HTML escaping for rich text controls
    QString HtmlEscape(const QString& str, bool fMultiLine=false);
    QString HtmlEscape(const std::string& str, bool fMultiLine=false);

    /** Copy a field of the currently selected entry of a view to the clipboard. Does nothing if nothing
        is selected.
       @param[in] column  Data column to extract from the model
       @param[in] role    Data role to extract from the model
       @see  TransactionView::copyLabel, TransactionView::copyAmount, TransactionView::copyAddress
     */
    void copyEntryData(QAbstractItemView *view, int column, int role=Qt::EditRole);

    /** Return a field of the currently selected entry as a QString. Does nothing if nothing
        is selected.
       @param[in] column  Data column to extract from the model
       @see  TransactionView::copyLabel, TransactionView::copyAmount, TransactionView::copyAddress
     */
    QList<QModelIndex> getEntryData(QAbstractItemView *view, int column);

    void setClipboard(const QString& str);

    /** Get save filename, mimics QFileDialog::getSaveFileName, except that it appends a default suffix
        when no suffix is provided by the user.

      @param[in] parent  Parent window (or 0)
      @param[in] caption Window caption (or empty, for default)
      @param[in] dir     Starting directory (or empty, to default to documents directory)
      @param[in] filter  Filter specification such as "Comma Separated Files (*.csv)"
      @param[out] selectedSuffixOut  Pointer to return the suffix (file type) that was selected (or 0).
                  Can be useful when choosing the save file format based on suffix.
     */
    QString getSaveFileName(QWidget *parent, const QString &caption, const QString &dir,
        const QString &filter,
        QString *selectedSuffixOut);

    /** Get open filename, convenience wrapper for QFileDialog::getOpenFileName.

      @param[in] parent  Parent window (or 0)
      @param[in] caption Window caption (or empty, for default)
      @param[in] dir     Starting directory (or empty, to default to documents directory)
      @param[in] filter  Filter specification such as "Comma Separated Files (*.csv)"
      @param[out] selectedSuffixOut  Pointer to return the suffix (file type) that was selected (or 0).
                  Can be useful when choosing the save file format based on suffix.
     */
    QString getOpenFileName(QWidget *parent, const QString &caption, const QString &dir,
        const QString &filter,
        QString *selectedSuffixOut);

    /** Get connection type to call object slot in GUI thread with invokeMethod. The call will be blocking.

       @returns If called from the GUI thread, return a Qt::DirectConnection.
                If called from another thread, return a Qt::BlockingQueuedConnection.
    */
    Qt::ConnectionType blockingGUIThreadConnection();

    // Determine whether a widget is hidden behind other windows
    bool isObscured(QWidget *w);

    // Open debug.log
    void openDebugLogfile();

    // Open the config file
    bool openPaladeumConf();

    // Replace invalid default fonts with known good ones
    void SubstituteFonts(const QString& language);

    // Concatenate a string given the painter, static text width, left side of rect, and right side of rect
    // and which side the concatenated string is on (default left)
    void concatenate(QPainter* painter, QString& strToCon, int static_width, int left_side, int right_size);


    class SyncWarningMessage : public QDialog
    {
        Q_OBJECT

    public:
        explicit SyncWarningMessage(QWidget *parent = 0);

        bool showTransactionSyncWarningMessage();
    };


    /** Qt event filter that intercepts ToolTipChange events, and replaces the tooltip with a rich text
      representation if needed. This assures that Qt can word-wrap long tooltip messages.
      Tooltips longer than the provided size threshold (in characters) are wrapped.
     */
    class ToolTipToRichTextFilter : public QObject
    {
        Q_OBJECT

    public:
        explicit ToolTipToRichTextFilter(int size_threshold, QObject *parent = 0);

    protected:
        bool eventFilter(QObject *obj, QEvent *evt);

    private:
        int size_threshold;
    };

    /**
     * Makes a QTableView last column feel as if it was being resized from its left border.
     * Also makes sure the column widths are never larger than the table's viewport.
     * In Qt, all columns are resizable from the right, but it's not intuitive resizing the last column from the right.
     * Usually our second to last columns behave as if stretched, and when on strech mode, columns aren't resizable
     * interactively or programmatically.
     *
     * This helper object takes care of this issue.
     *
     */
    class TableViewLastColumnResizingFixer: public QObject
    {
        Q_OBJECT

        public:
            TableViewLastColumnResizingFixer(QTableView* table, int lastColMinimumWidth, int allColsMinimumWidth, QObject *parent);
            void stretchColumnWidth(int column);

        private:
            QTableView* tableView;
            int lastColumnMinimumWidth;
            int allColumnsMinimumWidth;
            int lastColumnIndex;
            int columnCount;
            int secondToLastColumnIndex;

            void adjustTableColumnsWidth();
            int getAvailableWidthForColumn(int column);
            int getColumnsWidth();
            void connectViewHeadersSignals();
            void disconnectViewHeadersSignals();
            void setViewHeaderResizeMode(int logicalIndex, QHeaderView::ResizeMode resizeMode);
            void resizeColumn(int nColumnIndex, int width);

        private Q_SLOTS:
            void on_sectionResized(int logicalIndex, int oldSize, int newSize);
            void on_geometriesChanged();
    };

    bool GetStartOnSystemStartup();
    bool SetStartOnSystemStartup(bool fAutoStart);

    /* Convert QString to OS specific boost path through UTF-8 */
    fs::path qstringToBoostPath(const QString &path);

    /* Convert OS specific boost path to QString through UTF-8 */
    QString boostPathToQString(const fs::path &path);

    /* Convert seconds into a QString with days, hours, mins, secs */
    QString formatDurationStr(int secs);

    /* Format CNodeStats.nServices bitmask into a user-readable string */
    QString formatServicesStr(quint64 mask);

    /* Format a CNodeCombinedStats.dPingTime into a user-readable string or display N/A, if 0*/
    QString formatPingTime(double dPingTime);

    /* Format a CNodeCombinedStats.nTimeOffset into a user-readable string. */
    QString formatTimeOffset(int64_t nTimeOffset);

    QString formatNiceTimeOffset(qint64 secs);

    QString formatBytes(uint64_t bytes);

    /** Item delegate that shows some columns (amounts, addresses, dates) in the fixed-width font. */
    class MonoColumnDelegate : public QStyledItemDelegate
    {
    public:
        MonoColumnDelegate(const QList<int>& columns, QObject *parent, const QList<int>& amounts = QList<int>()) :
            QStyledItemDelegate(parent), monoColumns(columns), amountColumns(amounts) {}
        /** Amount columns are painted with the smaller fraction, see paintAmount() */
        void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;

    protected:
        void initStyleOption(QStyleOptionViewItem *option, const QModelIndex &index) const override;

    private:
        QList<int> monoColumns;
        QList<int> amountColumns;
    };

    /** Logs how long a piece of GUI work took, in the "qt" debug category (-debug=qt). */
    class LogTimer
    {
    public:
        explicit LogTimer(const QString& what);
        ~LogTimer();
        void setDetail(const QString& detail) { m_detail = detail; }

    private:
        QString m_what;
        QString m_detail;
        qint64 m_start;
    };

    class ClickableLabel : public QLabel
    {
        Q_OBJECT

    Q_SIGNALS:
        /** Emitted when the label is clicked. The relative mouse coordinates of the click are
         * passed to the signal.
         */
        void clicked(const QPoint& point);
    protected:
        void mouseReleaseEvent(QMouseEvent *event);
    };
    
    class ClickableProgressBar : public QProgressBar
    {
        Q_OBJECT
        
    Q_SIGNALS:
        /** Emitted when the progressbar is clicked. The relative mouse coordinates of the click are
         * passed to the signal.
         */
        void clicked(const QPoint& point);
    protected:
        void mouseReleaseEvent(QMouseEvent *event);
    };

#if defined(Q_OS_MAC) && QT_VERSION >= 0x050000
    // workaround for Qt OSX Bug:
    // https://bugreports.qt-project.org/browse/QTBUG-15631
    // QProgressBar uses around 10% CPU even when app is in background
    class ProgressBar : public ClickableProgressBar
    {
        bool event(QEvent *e) {
            return (e->type() != QEvent::StyleAnimationUpdate) ? QProgressBar::event(e) : false;
        }
    };
#else
    typedef ClickableProgressBar ProgressBar;
#endif

    /**
     * Returns the start-moment of the day in local time.
     *
     * QDateTime::QDateTime(const QDate& date) is deprecated since Qt 5.15.
     * QDate::startOfDay() was introduced in Qt 5.14.
     */
    QDateTime StartOfDay(const QDate& date);


     /** 
     * Splits the string into substrings wherever separator occurs, and returns
     * the list of those strings. Empty strings do not appear in the result.
     *
     * QString::split() signature differs in different Qt versions:
     *  - QString::SplitBehavior is deprecated since Qt 5.15
     *  - Qt::SplitBehavior was introduced in Qt 5.14
     * If {QString|Qt}::SkipEmptyParts behavior is required, use this
     * function instead of QString::split().
     */
    template <typename SeparatorType>
    QStringList SplitSkipEmptyParts(const QString& string, const SeparatorType& separator)
    {
    #if (QT_VERSION >= QT_VERSION_CHECK(5, 14, 0))
        return string.split(separator, Qt::SkipEmptyParts);
    #else
        return string.split(separator, QString::SkipEmptyParts);
    #endif
    }

     /**
     * Returns true if pixmap has been set.
     *
     * QPixmap* QLabel::pixmap() is deprecated since Qt 5.15.
     */
    bool HasPixmap(const QLabel* label);
    QImage GetImage(const QLabel* label);

} // namespace GUIUtil

#endif // PLB_QT_GUIUTIL_H
