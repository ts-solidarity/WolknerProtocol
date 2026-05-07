#include "MainWindow.h"

#include <QAction>
#include <QApplication>
#include <QButtonGroup>
#include <QDesktopServices>
#include <QEvent>
#include <QEasingCurve>
#include <QFocusEvent>
#include <QFrame>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPixmap>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QShortcut>
#include <QStackedLayout>
#include <QStatusBar>
#include <QString>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QThread>
#include <QTimer>
#include <QToolBar>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>
#include <QWindow>
#include <QtConcurrent/QtConcurrentRun>
#include <cmath>
#include <cstdio>

// =================== HeroCell ===================

HeroCell::HeroCell(const QString& label, bool magenta, QWidget* parent)
    : QWidget(parent), magenta_(magenta) {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(20, 12, 20, 12);
    outer->setSpacing(2);

    labelText_ = new QLabel(label, this);
    labelText_->setObjectName("heroLabel");
    labelText_->setTextInteractionFlags(Qt::NoTextInteraction);
    labelText_->setFocusPolicy(Qt::NoFocus);
    outer->addWidget(labelText_);

    auto* stackHost = new QWidget(this);
    stack_ = new QStackedLayout(stackHost);
    stack_->setContentsMargins(0, 0, 0, 0);

    valueLabel_ = new QLabel("—", this);
    valueLabel_->setObjectName(magenta ? "heroValue" : "heroValueB");
    valueLabel_->setTextInteractionFlags(Qt::NoTextInteraction);
    valueLabel_->setFocusPolicy(Qt::NoFocus);
    valueLabel_->setCursor(Qt::IBeamCursor);

    valueEdit_ = new QLineEdit(this);
    valueEdit_->setObjectName(magenta ? "heroValueEdit" : "heroValueEditB");
    valueEdit_->setAlignment(Qt::AlignLeft);
    valueEdit_->installEventFilter(this);

    stack_->addWidget(valueLabel_);
    stack_->addWidget(valueEdit_);
    stack_->setCurrentWidget(valueLabel_);
    outer->addWidget(stackHost);

    setMinimumWidth(170);
}

void HeroCell::bind(ProcessMem* mem, uint64_t valueSlotAddr) {
    mem_ = mem;
    valueSlotAddr_ = valueSlotAddr;
    refresh();
}

void HeroCell::setLabel(const QString& text) {
    labelText_->setText(text);
}

// Two-hop read: value slot → current LuaNumber → double.
static bool readVariableValue(const ProcessMem* mem, uint64_t valueSlotAddr, double& out) {
    if (!mem || valueSlotAddr == 0) return false;
    uint64_t luaPtr = 0;
    if (!mem->readInto(valueSlotAddr, &luaPtr, 8)) return false;
    if (luaPtr < 0x10000) return false;
    return mem->readInto(luaPtr + 16, &out, 8);
}

static bool writeVariableValue(ProcessMem* mem, uint64_t valueSlotAddr, double v) {
    if (!mem || valueSlotAddr == 0) return false;
    uint64_t luaPtr = 0;
    if (!mem->readInto(valueSlotAddr, &luaPtr, 8)) return false;
    if (luaPtr < 0x10000) return false;
    return mem->write(luaPtr + 16, &v, 8);
}

void HeroCell::refresh() {
    double d = 0.0;
    if (!readVariableValue(mem_, valueSlotAddr_, d)) {
        valueLabel_->setText("—");
        return;
    }
    valueLabel_->setText(fmtDouble(d));
}

QString HeroCell::fmtDouble(double d) const {
    if (d != d) return "—";
    if (d == std::trunc(d)) return QString::number((long long)d);
    return QString::number(d);
}

void HeroCell::mouseDoubleClickEvent(QMouseEvent* /*e*/) {
    if (!mem_ || valueSlotAddr_ == 0) return;
    enterEdit();
}

void HeroCell::enterEdit() {
    valueEdit_->setText(valueLabel_->text() == "—" ? "" : valueLabel_->text());
    stack_->setCurrentWidget(valueEdit_);
    valueEdit_->setFocus();
    valueEdit_->selectAll();
}

void HeroCell::commitEdit() {
    bool ok = false;
    double v = valueEdit_->text().toDouble(&ok);
    if (!ok || !mem_ || valueSlotAddr_ == 0) {
        cancelEdit();
        return;
    }
    writeVariableValue(mem_, valueSlotAddr_, v);
    refresh();
    stack_->setCurrentWidget(valueLabel_);
    emit valueWritten();
}

void HeroCell::cancelEdit() {
    refresh();
    stack_->setCurrentWidget(valueLabel_);
}

bool HeroCell::eventFilter(QObject* watched, QEvent* event) {
    if (watched == valueEdit_) {
        if (event->type() == QEvent::KeyPress) {
            auto* ke = static_cast<QKeyEvent*>(event);
            if (ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) {
                commitEdit();
                return true;
            }
            if (ke->key() == Qt::Key_Escape) {
                cancelEdit();
                return true;
            }
        }
        if (event->type() == QEvent::FocusOut) {
            commitEdit();
        }
    }
    return QWidget::eventFilter(watched, event);
}

// =================== MainWindow ===================

// QTableWidgetItem subclass that sorts by numeric value rather than string.
class NumericValueItem : public QTableWidgetItem {
public:
    explicit NumericValueItem(const QString& text) : QTableWidgetItem(text) {}
    bool operator<(const QTableWidgetItem& other) const override {
        bool ok1 = false, ok2 = false;
        double a = text().toDouble(&ok1);
        double b = other.text().toDouble(&ok2);
        if (ok1 && ok2) return a < b;
        return QTableWidgetItem::operator<(other);
    }
};

// Clickable QLabel — hand cursor on hover, opens URL on click. Q_OBJECT not
// needed (no signals/slots), so AUTOMOC doesn't have to process this file.
class ClickableLabel : public QLabel {
public:
    ClickableLabel(const QString& html, const QString& url, QWidget* parent = nullptr)
        : QLabel(html, parent), url_(url) {
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover, true);
        setMouseTracking(true);
        setTextFormat(Qt::RichText);
        setTextInteractionFlags(Qt::NoTextInteraction);
        setFocusPolicy(Qt::NoFocus);
    }
protected:
    void enterEvent(QEnterEvent* e) override {
        setCursor(Qt::PointingHandCursor);
        QLabel::enterEvent(e);
    }
    void mousePressEvent(QMouseEvent* e) override {
        if (e->button() == Qt::LeftButton) {
            QDesktopServices::openUrl(QUrl(url_));
        }
        QLabel::mousePressEvent(e);
    }
private:
    QString url_;
};

// ----- Sliding mode-toggle indicator -----
// Encapsulated in a function-style helper that wires up the animation.
static void wireModeToggleIndicator(QFrame* host, QPushButton* left, QPushButton* right,
                                    QFrame* indicator) {
    auto place = [host, left, right, indicator](QPushButton* target, bool animate) {
        const int margin = 3;
        QRect r = target->geometry();
        QRect target_rect(r.x() + margin, margin,
                          r.width() - 2 * margin, host->height() - 2 * margin);
        if (animate) {
            auto* anim = new QPropertyAnimation(indicator, "geometry");
            anim->setDuration(200);
            anim->setEasingCurve(QEasingCurve::OutCubic);
            anim->setStartValue(indicator->geometry());
            anim->setEndValue(target_rect);
            anim->start(QAbstractAnimation::DeleteWhenStopped);
        } else {
            indicator->setGeometry(target_rect);
        }
    };
    // Listen to `toggled`, not `clicked`, so the slider follows state changes
    // even when they happen programmatically (e.g. mode auto-detect after a
    // scan or `setChecked()` from code paths).
    QObject::connect(left,  &QPushButton::toggled, [=](bool checked){ if (checked) place(left, true); });
    QObject::connect(right, &QPushButton::toggled, [=](bool checked){ if (checked) place(right, true); });
    QTimer::singleShot(0, [=]{ place(left->isChecked() ? left : right, false); });
}

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("WolknerProtocol");
    setWindowIcon(QIcon(":/icons/serge.png"));
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground, true);
    resize(1100, 820);

    auto* central = new QWidget(this);
    central->setObjectName("rootCentral");
    central->setAttribute(Qt::WA_StyledBackground, true);
    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // ============== HEADER ==============
    headerFrame_ = new QFrame(this);
    headerFrame_->setObjectName("headerFrame");
    headerFrame_->installEventFilter(this);
    headerFrame_->setFixedHeight(78);
    auto* headerLayout = new QHBoxLayout(headerFrame_);
    headerLayout->setContentsMargins(20, 12, 12, 12);
    headerLayout->setSpacing(14);

    // Portrait
    auto* portrait = new QLabel(this);
    portrait->setObjectName("brandPortrait");
    QPixmap pm(":/icons/serge.png");
    if (!pm.isNull()) {
        portrait->setPixmap(pm.scaled(50, 50, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
    portrait->setFixedSize(50, 50);
    portrait->setAlignment(Qt::AlignCenter);
    headerLayout->addWidget(portrait, 0, Qt::AlignVCenter);

    // Wordmark + subtitle (single label, two-color HTML span — joined word)
    auto* leftCol = new QVBoxLayout();
    leftCol->setSpacing(0);
    auto* wordmark = new QLabel(
        "<span style='color:#e6e8ec;'>Wolkner</span>"
        "<span style='color:#d4a960;'>Protocol</span>", this);
    wordmark->setObjectName("brandWordmark");
    wordmark->setTextFormat(Qt::RichText);
    wordmark->setTextInteractionFlags(Qt::NoTextInteraction);
    wordmark->setFocusPolicy(Qt::NoFocus);
    leftCol->addWidget(wordmark);

    auto* subtitle = new QLabel("Suzerain  ·  Live State Editor", this);
    subtitle->setObjectName("brandSubtitle");
    subtitle->setTextInteractionFlags(Qt::NoTextInteraction);
    subtitle->setFocusPolicy(Qt::NoFocus);
    leftCol->addWidget(subtitle);
    headerLayout->addLayout(leftCol);
    headerLayout->addStretch();

    // Sliding mode toggle (Sordland / Rizia)
    auto* modeHost = new QFrame(this);
    modeHost->setObjectName("modeToggleHost");
    modeHost->setFixedSize(220, 36);
    auto* modeIndicator = new QFrame(modeHost);
    modeIndicator->setObjectName("modeIndicator");
    modeIndicator->setAttribute(Qt::WA_TransparentForMouseEvents);
    sordlandBtn_ = new QPushButton("Sordland", modeHost);
    sordlandBtn_->setObjectName("modeBtnLeft");
    sordlandBtn_->setCheckable(true);
    sordlandBtn_->setChecked(true);
    sordlandBtn_->setCursor(Qt::PointingHandCursor);
    sordlandBtn_->setFocusPolicy(Qt::NoFocus);
    sordlandBtn_->setGeometry(0, 0, 110, 36);
    riziaBtn_ = new QPushButton("Rizia", modeHost);
    riziaBtn_->setObjectName("modeBtnRight");
    riziaBtn_->setCheckable(true);
    riziaBtn_->setCursor(Qt::PointingHandCursor);
    riziaBtn_->setFocusPolicy(Qt::NoFocus);
    riziaBtn_->setGeometry(110, 0, 110, 36);
    // Exclusive group → exactly one is :checked at any moment, no paint-cycle
    // window where both look active and the indicator falls between them.
    auto* modeGroup = new QButtonGroup(this);
    modeGroup->setExclusive(true);
    modeGroup->addButton(sordlandBtn_);
    modeGroup->addButton(riziaBtn_);
    sordlandBtn_->raise();
    riziaBtn_->raise();
    modeIndicator->lower();
    wireModeToggleIndicator(modeHost, sordlandBtn_, riziaBtn_, modeIndicator);
    headerLayout->addWidget(modeHost, 0, Qt::AlignVCenter);
    connect(sordlandBtn_, &QPushButton::clicked, this, &MainWindow::onSetSordland);
    connect(riziaBtn_,    &QPushButton::clicked, this, &MainWindow::onSetRizia);

    headerLayout->addSpacing(14);

    // Author — entire `~ by wleeaf ~` is one clickable rectangle, hand cursor on hover.
    auto* author = new ClickableLabel(
        "<span style='color:#6b7080;'>~&nbsp;&nbsp;by&nbsp;&nbsp;</span>"
        "<span style='color:#d4a960; font-weight:600;'>wleeaf</span>"
        "<span style='color:#6b7080;'>&nbsp;&nbsp;~</span>",
        "https://wleeaf.dev", this);
    author->setObjectName("brandAuthor");
    headerLayout->addWidget(author, 0, Qt::AlignVCenter);

    headerLayout->addSpacing(14);

    // Window controls
    minBtn_ = new QPushButton("─", this);
    minBtn_->setObjectName("winMinBtn");
    minBtn_->setFixedSize(34, 28);
    minBtn_->setCursor(Qt::PointingHandCursor);
    closeBtn_ = new QPushButton("✕", this);
    closeBtn_->setObjectName("winCloseBtn");
    closeBtn_->setFixedSize(34, 28);
    closeBtn_->setCursor(Qt::PointingHandCursor);
    headerLayout->addWidget(minBtn_, 0, Qt::AlignVCenter);
    headerLayout->addWidget(closeBtn_, 0, Qt::AlignVCenter);
    connect(minBtn_, &QPushButton::clicked, this, &MainWindow::showMinimized);
    connect(closeBtn_, &QPushButton::clicked, this, &MainWindow::close);

    layout->addWidget(headerFrame_);

    // ============== BODY (single padded area) ==============
    auto* body = new QWidget(this);
    body->setObjectName("bodyWrapper");
    body->setAttribute(Qt::WA_StyledBackground, true);
    auto* bodyLayout = new QVBoxLayout(body);
    bodyLayout->setContentsMargins(24, 20, 24, 16);
    bodyLayout->setSpacing(16);

    // Hero card
    auto* heroCard = new QFrame(this);
    heroCard->setObjectName("heroCard");
    auto* heroLayout = new QHBoxLayout(heroCard);
    heroLayout->setContentsMargins(8, 12, 8, 12);
    heroLayout->setSpacing(0);

    heroSlot0_ = new HeroCell("BUDGET",    true,  this);
    heroSlot1_ = new HeroCell("WEALTH",    false, this);
    heroSlot2_ = new HeroCell("AUTHORITY", true,  this);
    heroSlot3_ = new HeroCell("ENERGY",    false, this);
    auto makeDivider = [&]() -> QLabel* {
        auto* d = new QLabel(this);
        d->setObjectName("heroDivider");
        d->setFixedWidth(1);
        return d;
    };
    heroLayout->addWidget(heroSlot0_);
    divBefore1_ = makeDivider();
    heroLayout->addWidget(divBefore1_);
    heroLayout->addWidget(heroSlot1_);
    divBefore2_ = makeDivider();
    heroLayout->addWidget(divBefore2_);
    heroLayout->addWidget(heroSlot2_);
    divBefore3_ = makeDivider();
    heroLayout->addWidget(divBefore3_);
    heroLayout->addWidget(heroSlot3_);
    heroLayout->addStretch();
    bodyLayout->addWidget(heroCard);

    auto refreshOnEdit = [this]{
        updateHeroStrip();
        for (int r = 0; r < table_->rowCount(); ++r) {
            auto* nameItem = table_->item(r, 0);
            auto* valItem = table_->item(r, 1);
            if (!nameItem || !valItem) continue;
            uint64_t slot = nameItem->data(Qt::UserRole).toULongLong();
            double cur = 0.0;
            if (readVariableValue(&mem_, slot, cur)) {
                suppressCellEvent_ = true;
                valItem->setText((cur == std::trunc(cur)) ? QString::number((long long)cur) : QString::number(cur));
                suppressCellEvent_ = false;
            }
        }
    };
    connect(heroSlot0_, &HeroCell::valueWritten, this, refreshOnEdit);
    connect(heroSlot1_, &HeroCell::valueWritten, this, refreshOnEdit);
    connect(heroSlot2_, &HeroCell::valueWritten, this, refreshOnEdit);
    connect(heroSlot3_, &HeroCell::valueWritten, this, refreshOnEdit);

    // Toolbar
    auto* topBar = new QHBoxLayout();
    topBar->setSpacing(10);
    attachBtn_ = new QPushButton("Attach", this);
    attachBtn_->setObjectName("primaryButton");
    attachBtn_->setCursor(Qt::PointingHandCursor);
    refreshBtn_ = new QPushButton("Refresh", this);
    refreshBtn_->setObjectName("secondaryButton");
    refreshBtn_->setEnabled(false);
    refreshBtn_->setCursor(Qt::PointingHandCursor);
    filter_ = new QLineEdit(this);
    filter_->setPlaceholderText("Search variables or values   (e.g. budget, opinion, =5, >10)");
    filter_->setEnabled(false);

    topBar->addWidget(attachBtn_);
    topBar->addWidget(refreshBtn_);
    topBar->addSpacing(8);
    topBar->addWidget(filter_, 1);
    bodyLayout->addLayout(topBar);

    // Body card containing the table
    auto* bodyCard = new QFrame(this);
    bodyCard->setObjectName("bodyCard");
    auto* bodyCardLayout = new QVBoxLayout(bodyCard);
    bodyCardLayout->setContentsMargins(0, 0, 0, 0);
    bodyCardLayout->setSpacing(0);
    table_ = new QTableWidget(this);
    table_->setColumnCount(2);
    table_->setHorizontalHeaderLabels({"Variable", "Value"});
    table_->horizontalHeader()->setStretchLastSection(false);
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
    table_->setColumnWidth(1, 180);
    table_->verticalHeader()->setVisible(false);
    table_->setAlternatingRowColors(true);
    table_->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSortingEnabled(false);  // we sort manually on header click
    table_->setShowGrid(false);
    table_->setFocusPolicy(Qt::ClickFocus);
    table_->verticalHeader()->setDefaultSectionSize(32);
    table_->horizontalHeader()->setFixedHeight(40);
    table_->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    table_->horizontalHeader()->setSortIndicatorShown(true);
    table_->horizontalHeader()->setSectionsClickable(true);

    // Manual-sort behaviour: sortingEnabled stays OFF so live polling never
    // rearranges rows. Clicking a header column sorts once, toggling
    // ascending/descending. Rows then hold position until the next click.
    connect(table_->horizontalHeader(), &QHeaderView::sectionClicked,
            this, [this](int col){
        auto* hdr = table_->horizontalHeader();
        Qt::SortOrder order = Qt::AscendingOrder;
        if (hdr->sortIndicatorSection() == col &&
            hdr->sortIndicatorOrder() == Qt::AscendingOrder) {
            order = Qt::DescendingOrder;
        }
        suppressCellEvent_ = true;
        table_->sortItems(col, order);
        suppressCellEvent_ = false;
        hdr->setSortIndicator(col, order);
    });

    bodyCardLayout->addWidget(table_);
    bodyLayout->addWidget(bodyCard, 1);

    layout->addWidget(body, 1);

    setCentralWidget(central);

    // Status bar
    statusLabel_ = new QLabel("◇  Not attached  ·  click Attach with Suzerain running", this);
    statusLabel_->setTextInteractionFlags(Qt::NoTextInteraction);
    statusLabel_->setFocusPolicy(Qt::NoFocus);
    statusLabel_->setObjectName("statusLabel");
    statusBar()->addWidget(statusLabel_, 1);
    statusBar()->setSizeGripEnabled(false);

    // === Live polling timer (1 Hz) — refreshes hero strip + entire table ===
    auto* livePoll = new QTimer(this);
    livePoll->setInterval(1000);
    connect(livePoll, &QTimer::timeout, this, [this]{
        if (!mem_.isAttached()) return;

        // Hero strip cells (skips ones whose inline editor is open).
        for (auto* c : { heroSlot0_, heroSlot1_, heroSlot2_, heroSlot3_ }) {
            if (c->isVisible()) c->refresh();
        }

        // Table — read each row's value from memory and update the cell text.
        // Skip the cell that's currently being edited so we don't clobber the
        // user's typing. Detect editing by whether the focused widget is a
        // QLineEdit that's a descendant of the table's viewport.
        QTableWidgetItem* editing = nullptr;
        if (auto* fw = QApplication::focusWidget()) {
            if (qobject_cast<QLineEdit*>(fw)) {
                QWidget* p = fw->parentWidget();
                while (p) {
                    if (p == table_->viewport() || p == table_) {
                        editing = table_->currentItem();
                        break;
                    }
                    p = p->parentWidget();
                }
            }
        }
        // Sorting is OFF by default — rows hold their position. Just rewrite
        // the cell text in place; positions don't change.
        suppressCellEvent_ = true;
        for (int r = 0; r < table_->rowCount(); ++r) {
            auto* nameItem = table_->item(r, 0);
            auto* valItem  = table_->item(r, 1);
            if (!nameItem || !valItem || valItem == editing) continue;
            uint64_t slot = nameItem->data(Qt::UserRole).toULongLong();
            double cur = 0.0;
            if (!readVariableValue(&mem_, slot, cur)) continue;
            QString shown = (cur == std::trunc(cur))
                ? QString::number((long long)cur)
                : QString::number(cur);
            if (valItem->text() != shown) valItem->setText(shown);
        }
        suppressCellEvent_ = false;
    });
    livePoll->start();

    // === Window fade-in animation on launch ===
    auto* fadeFx = new QGraphicsOpacityEffect(central);
    fadeFx->setOpacity(0.0);
    central->setGraphicsEffect(fadeFx);
    auto* fadeAnim = new QPropertyAnimation(fadeFx, "opacity", this);
    fadeAnim->setDuration(280);
    fadeAnim->setStartValue(0.0);
    fadeAnim->setEndValue(1.0);
    fadeAnim->setEasingCurve(QEasingCurve::OutCubic);
    QTimer::singleShot(0, [fadeAnim, central]{
        fadeAnim->start(QAbstractAnimation::DeleteWhenStopped);
        // Detach the effect once the fade completes so it doesn't keep
        // rendering the whole tree through an offscreen buffer.
        QObject::connect(fadeAnim, &QPropertyAnimation::finished, [central]{
            central->setGraphicsEffect(nullptr);
        });
    });

    // Signals
    connect(attachBtn_, &QPushButton::clicked, this, &MainWindow::onAttachClicked);
    connect(refreshBtn_, &QPushButton::clicked, this, &MainWindow::onRefreshClicked);
    connect(filter_, &QLineEdit::textChanged, this, &MainWindow::onFilterChanged);
    connect(table_, &QTableWidget::cellChanged, this, &MainWindow::onCellChanged);
    connect(&scanWatcher_, &QFutureWatcher<void>::finished, this, &MainWindow::onScanFinished);
}

MainWindow::~MainWindow() {
    delete scanner_;
}

void MainWindow::setStatus(const QString& msg) {
    statusLabel_->setText(msg);
}

void MainWindow::onAttachClicked() {
    auto pid = ProcessMem::findPidByName("Suzerain.exe");
    if (!pid) {
        QMessageBox::warning(this, "Suzerain not found",
                             "Couldn't find a running Suzerain.exe process.\n"
                             "Launch the game through Steam first, then click Attach.");
        return;
    }
    if (!mem_.attach(*pid)) {
        QMessageBox::critical(this, "Attach failed",
                              QString("Could not open /proc/%1/mem — %2\n\n"
                                      "You likely need to run this program with sudo.")
                                  .arg(*pid).arg(QString::fromStdString(mem_.lastError())));
        return;
    }
    setStatus(QString("◇  Connected to PID %1  ·  scanning memory…").arg(*pid));
    attachBtn_->setEnabled(false);
    scanner_ = new Scanner(mem_, [this](const std::string& msg){
        QMetaObject::invokeMethod(this, [this, msg](){
            setStatus(QString("◇  %1").arg(QString::fromStdString(msg)));
        }, Qt::QueuedConnection);
    });
    // Pulse the status label gently while scanning runs.
    auto* pulseFx = new QGraphicsOpacityEffect(statusLabel_);
    pulseFx->setOpacity(1.0);
    statusLabel_->setGraphicsEffect(pulseFx);
    auto* pulse = new QPropertyAnimation(pulseFx, "opacity", this);
    pulse->setObjectName("statusPulse");
    pulse->setDuration(1400);
    pulse->setKeyValueAt(0.0, 1.0);
    pulse->setKeyValueAt(0.5, 0.45);
    pulse->setKeyValueAt(1.0, 1.0);
    pulse->setLoopCount(-1);
    pulse->start();
    runEnumerateAsync();
}

void MainWindow::runEnumerateAsync() {
    auto future = QtConcurrent::run([this]() {
        // Detect the LuaNumber class pointer ONCE (during the very first scan).
        // It's the IL2CPP class pointer for Language.Lua.LuaNumber and never
        // changes for the lifetime of the running process. Re-detecting on
        // Refresh is heuristic and can pick a different cluster, which would
        // make enumerate() match non-LuaNumber objects → garbage values.
        if (luaNumberTypePtr_ == 0) {
            auto tp = scanner_->detectLuaNumberTypePtr();
            if (!tp) {
                QMetaObject::invokeMethod(this, [this](){
                    QMessageBox::critical(this, "Scan failed",
                                          "Could not detect the LuaNumber class pointer.\n"
                                          "Make sure you're past the main menu.");
                }, Qt::QueuedConnection);
                return;
            }
            luaNumberTypePtr_ = *tp;
        }
        variables_ = scanner_->enumerate(luaNumberTypePtr_);
    });
    scanWatcher_.setFuture(future);
}

void MainWindow::onScanFinished() {
    // Stop the scanning pulse and restore opacity.
    if (auto* fx = qobject_cast<QGraphicsOpacityEffect*>(statusLabel_->graphicsEffect())) {
        fx->setOpacity(1.0);
    }
    statusLabel_->setGraphicsEffect(nullptr);

    populateTable();
    detectCampaignMode();
    if (!variables_.empty()) {
        filter_->setEnabled(true);
        refreshBtn_->setEnabled(true);
        setStatus(QString("◆  Connected  ·  PID %1  ·  %2 variables  ·  double-click any value to edit")
                      .arg(mem_.pid()).arg(variables_.size()));
    } else {
        attachBtn_->setEnabled(true);
        setStatus("◇  No variables found  ·  is the game past the main menu?");
    }
}

void MainWindow::updateHeroStrip() {
    auto bindBy = [&](HeroCell* cell, const QString& label, const char* key) {
        cell->setLabel(label);
        uint64_t addr = 0;
        if (key) {
            auto it = variables_.find(key);
            if (it != variables_.end()) addr = it->second.valueSlotAddr;
        }
        cell->bind(mem_.isAttached() ? &mem_ : nullptr, addr);
    };

    if (mode_ == CampaignMode::Sordland) {
        // Layout:  [BUDGET]  |  [WEALTH]
        bindBy(heroSlot0_, "BUDGET", "BaseGame.GovernmentBudget");
        bindBy(heroSlot1_, "WEALTH", "BaseGame.PersonalWealth");
        bindBy(heroSlot2_, "",       nullptr);
        bindBy(heroSlot3_, "",       nullptr);
        heroSlot0_->setVisible(true);
        heroSlot1_->setVisible(true);
        heroSlot2_->setVisible(false);
        heroSlot3_->setVisible(false);
        divBefore1_->setVisible(true);
        divBefore2_->setVisible(false);
        divBefore3_->setVisible(false);
    } else {  // Rizia
        // Layout:  [AUTHORITY]  |  [BUDGET]  |  [ENERGY]
        // Authority and Budget swap positions (slot0 hosts Authority).
        bindBy(heroSlot0_, "AUTHORITY", "RiziaDLC.Resources_Authority");
        bindBy(heroSlot1_, "",          nullptr);
        bindBy(heroSlot2_, "BUDGET",    "RiziaDLC.Resources_Budget");
        bindBy(heroSlot3_, "ENERGY",    "RiziaDLC.Resources_Energy");
        heroSlot0_->setVisible(true);
        heroSlot1_->setVisible(false);
        heroSlot2_->setVisible(true);
        heroSlot3_->setVisible(true);
        divBefore1_->setVisible(false);
        divBefore2_->setVisible(true);
        divBefore3_->setVisible(true);
    }
}

void MainWindow::onSetSordland() {
    mode_ = CampaignMode::Sordland;
    sordlandBtn_->setChecked(true);
    riziaBtn_->setChecked(false);
    updateHeroStrip();
}

void MainWindow::onSetRizia() {
    mode_ = CampaignMode::Rizia;
    sordlandBtn_->setChecked(false);
    riziaBtn_->setChecked(true);
    updateHeroStrip();
}

void MainWindow::detectCampaignMode() {
    // Only auto-detect on the FIRST scan after attaching. Subsequent scans
    // (Refresh) preserve whatever the user has selected.
    if (firstScanDone_) {
        updateHeroStrip();
        return;
    }
    firstScanDone_ = true;
    bool hasRizia    = variables_.count("RiziaDLC.Resources_Budget") > 0;
    bool hasSordland = variables_.count("BaseGame.GovernmentBudget") > 0;
    if (hasRizia && !hasSordland) {
        onSetRizia();
    } else {
        onSetSordland();
    }
}

void MainWindow::mousePressEvent(QMouseEvent* event) {
    QMainWindow::mousePressEvent(event);
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
    // Header drag-to-move for the frameless window.
    if (watched == headerFrame_ && event->type() == QEvent::MouseButtonPress) {
        auto* me = static_cast<QMouseEvent*>(event);
        if (me->button() == Qt::LeftButton) {
            // Don't drag if the click landed on a child widget (button, label link, etc.)
            QWidget* child = headerFrame_->childAt(me->pos());
            if (!child || child == headerFrame_) {
                if (auto* h = windowHandle()) {
                    h->startSystemMove();
                    return true;
                }
            }
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::populateTable() {
    suppressCellEvent_ = true;
    table_->setSortingEnabled(false);
    table_->setRowCount((int)variables_.size());

    std::vector<std::pair<std::string, VariableEntry>> sorted(variables_.begin(), variables_.end());
    std::sort(sorted.begin(), sorted.end(), [](auto& a, auto& b){ return a.first < b.first; });

    int row = 0;
    for (auto& [name, entry] : sorted) {
        double v = scanner_->readValue(entry);
        auto* nameItem = new QTableWidgetItem(QString::fromStdString(name));
        nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
        // Store the value-slot address in UserRole. Reads/writes indirect
        // through it: slot → current LuaNumber → value.
        nameItem->setData(Qt::UserRole, QVariant::fromValue(entry.valueSlotAddr));

        QString shown = (v == std::trunc(v)) ? QString::number((long long)v) : QString::number(v);
        auto* valItem = new NumericValueItem(shown);

        table_->setItem(row, 0, nameItem);
        table_->setItem(row, 1, valItem);
        row++;
    }
    // Initial sort once (by name asc) — rows then hold position. The user
    // re-sorts manually by clicking the column headers.
    table_->sortItems(0, Qt::AscendingOrder);
    table_->horizontalHeader()->setSortIndicator(0, Qt::AscendingOrder);
    suppressCellEvent_ = false;
    applyFilter(filter_->text());
}

void MainWindow::onFilterChanged(const QString& text) {
    applyFilter(text);
}

// Filter syntax:
//   <text>           substring on name OR value (case-insensitive)
//   =<num>           value exactly equals num
//   !=<num>          value not equal
//   >, >=, <, <=     value comparison
void MainWindow::applyFilter(const QString& text) {
    QString raw = text.trimmed();
    QString lowered = raw.toLower();

    // Detect numeric comparator
    QString op;
    double opVal = 0.0;
    bool hasOp = false;
    auto tryOp = [&](const QString& sym) {
        if (raw.startsWith(sym)) {
            QString rest = raw.mid(sym.size()).trimmed();
            bool ok = false;
            double v = rest.toDouble(&ok);
            if (ok) { op = sym; opVal = v; hasOp = true; }
            return ok;
        }
        return false;
    };
    // Order matters: longer ops first
    tryOp(">=") || tryOp("<=") || tryOp("!=") || tryOp("=") || tryOp(">") || tryOp("<");

    int visible = 0;
    for (int r = 0; r < table_->rowCount(); ++r) {
        auto* nameItem = table_->item(r, 0);
        auto* valItem = table_->item(r, 1);
        if (!nameItem || !valItem) continue;

        bool show;
        if (raw.isEmpty()) {
            show = true;
        } else if (hasOp) {
            bool ok = false;
            double v = valItem->text().toDouble(&ok);
            if (!ok) {
                show = false;
            } else if (op == "=")       show = (v == opVal);
            else if (op == "!=")        show = (v != opVal);
            else if (op == ">")         show = (v > opVal);
            else if (op == ">=")        show = (v >= opVal);
            else if (op == "<")         show = (v < opVal);
            else if (op == "<=")        show = (v <= opVal);
            else                         show = false;
        } else {
            show = nameItem->text().toLower().contains(lowered)
                || valItem->text().toLower().contains(lowered);
        }
        table_->setRowHidden(r, !show);
        if (show) visible++;
    }
    if (mem_.isAttached()) {
        setStatus(QString("◆  Connected  ·  PID %1  ·  %2 of %3 visible")
                      .arg(mem_.pid()).arg(visible).arg(variables_.size()));
    }
}

void MainWindow::onCellChanged(int row, int column) {
    if (suppressCellEvent_) return;
    if (column != 1) return;
    auto* nameItem = table_->item(row, 0);
    auto* valItem = table_->item(row, 1);
    if (!nameItem || !valItem) return;
    bool ok = false;
    double newVal = valItem->text().toDouble(&ok);
    if (!ok) {
        QMessageBox::warning(this, "Invalid value", "Please enter a number.");
        // Restore previous shown value via the value-slot indirection.
        uint64_t slot = nameItem->data(Qt::UserRole).toULongLong();
        double cur = 0.0;
        readVariableValue(&mem_, slot, cur);
        suppressCellEvent_ = true;
        valItem->setText((cur == std::trunc(cur)) ? QString::number((long long)cur) : QString::number(cur));
        suppressCellEvent_ = false;
        return;
    }
    uint64_t slot = nameItem->data(Qt::UserRole).toULongLong();
    if (!writeVariableValue(&mem_, slot, newVal)) {
        QMessageBox::critical(this, "Write failed",
                              QString("Could not write to slot 0x%1.").arg(slot, 0, 16));
    }
    // Re-format the displayed value
    suppressCellEvent_ = true;
    valItem->setText((newVal == std::trunc(newVal)) ? QString::number((long long)newVal) : QString::number(newVal));
    suppressCellEvent_ = false;
    // If the edited variable is one of the hero-strip stats, reflect the new value.
    updateHeroStrip();
}

void MainWindow::onRefreshClicked() {
    refreshBtn_->setEnabled(false);
    filter_->setEnabled(false);
    setStatus("◇  Re-scanning memory…");
    table_->clearContents();
    table_->setRowCount(0);
    variables_.clear();
    runEnumerateAsync();
}
