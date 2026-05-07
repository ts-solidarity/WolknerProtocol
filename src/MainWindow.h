#pragma once

#include <QFutureWatcher>
#include <QMainWindow>
#include <QString>
#include <QPointer>
#include <unordered_map>

#include "ProcessMem.h"
#include "Scanner.h"

class QLineEdit;
class QTableWidget;
class QTableWidgetItem;
class QLabel;
class QPushButton;
class QStatusBar;
class QMouseEvent;
class QFrame;
class QStackedLayout;
class HeroCell;

enum class CampaignMode { Sordland, Rizia };

// Small editable label widget for hero stats. Shows a big number; double-click
// to swap it for a QLineEdit; on Enter/blur, write the new value back to memory.
class HeroCell : public QWidget {
    Q_OBJECT
public:
    HeroCell(const QString& label, bool magenta, QWidget* parent = nullptr);

    // Bind this cell to a variable by its dict-entry value-slot address.
    // Pass 0 to "unbind".
    void bind(ProcessMem* mem, uint64_t valueSlotAddr);
    void refresh();
    void setLabel(const QString& text);

signals:
    void valueWritten();

protected:
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void enterEdit();
    void commitEdit();
    void cancelEdit();
    QString fmtDouble(double d) const;

    QStackedLayout* stack_;
    QLabel* labelText_;
    QLabel* valueLabel_;
    QLineEdit* valueEdit_;
    ProcessMem* mem_ = nullptr;
    uint64_t valueSlotAddr_ = 0;  // 0 = unbound
    bool magenta_;
};

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    // Frameless-window event handlers (drag the header to move).
    void mousePressEvent(QMouseEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    void onAttachClicked();
    void onRefreshClicked();
    void onFilterChanged(const QString& text);
    void onCellChanged(int row, int column);
    void onScanFinished();
    void onSetSordland();
    void onSetRizia();

private:
    void applyFilter(const QString& text);
    void populateTable();
    void startEnumeration();
    void setStatus(const QString& msg);
    void runEnumerateAsync();
    void updateHeroStrip();
    void applyCampaignMode();
    void detectCampaignMode();

    ProcessMem mem_;
    Scanner* scanner_ = nullptr;
    uint64_t luaNumberTypePtr_ = 0;
    std::unordered_map<std::string, VariableEntry> variables_;
    CampaignMode mode_ = CampaignMode::Sordland;

    QTableWidget* table_ = nullptr;
    QLineEdit* filter_ = nullptr;
    QPushButton* attachBtn_ = nullptr;
    QPushButton* refreshBtn_ = nullptr;
    QLabel* statusLabel_ = nullptr;

    QFrame* headerFrame_ = nullptr;
    QPushButton* sordlandBtn_ = nullptr;
    QPushButton* riziaBtn_ = nullptr;
    QPushButton* minBtn_ = nullptr;
    QPushButton* closeBtn_ = nullptr;

    // Hero strip is four fixed slots. Their labels and bindings change with
    // the campaign mode — slot0 might host BUDGET (Sordland) or AUTHORITY
    // (Rizia), etc. The "Budget"/"Wealth"/etc. names are just initial labels.
    HeroCell* heroSlot0_ = nullptr;
    HeroCell* heroSlot1_ = nullptr;
    HeroCell* heroSlot2_ = nullptr;
    HeroCell* heroSlot3_ = nullptr;

    // Dividers between slots — owned by the slot that comes after them.
    // divBefore1_ lives between slot0 and slot1; hides when slot1 hides.
    QLabel* divBefore1_ = nullptr;
    QLabel* divBefore2_ = nullptr;
    QLabel* divBefore3_ = nullptr;

    QFutureWatcher<void> scanWatcher_;
    bool suppressCellEvent_ = false;
    bool firstScanDone_ = false;
};
