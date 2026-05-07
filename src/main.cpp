#include "MainWindow.h"

#include <QApplication>
#include <QFile>
#include <QFontDatabase>
#include <QGuiApplication>

// === SLATE / AMBER PALETTE ==========================================
// Sophisticated warm dark — restrained, product-like, not "hacker neon."
//
// bg:               #14161c   (warm slate background)
// surface:          #1d2028   (cards / panels)
// surface-elev:     #232733   (elevated surfaces, hover)
// border:           #2d3140   (subtle border)
// border-strong:    #3d4252
// text:             #e6e8ec   (primary text)
// text-muted:       #9ca0aa
// text-soft:        #6b7080
// accent:           #d4a960   (warm amber/gold — the only accent color)
// accent-hover:     #e0b878
// accent-soft:      #463826   (translucent amber background)
// danger-soft:      #4d2d2d
// =====================================================================
static const char* kStyleSheet = R"QSS(
* {
    color: #e6e8ec;
    font-family: 'Inter', -apple-system, 'Segoe UI', 'Helvetica Neue', sans-serif;
    font-size: 10pt;
}

QMainWindow {
    background: transparent;
}

/* The translucent main window holds a "rootCentral" widget that paints the
   real rounded background. Children draw on top. */
QWidget#rootCentral {
    background-color: transparent;
}

QWidget#bodyWrapper {
    background-color: #14161c;
}

/* === FRAMELESS HEADER === */
QFrame#headerFrame {
    background-color: #181a22;
    border-bottom: 1px solid #2d3140;
    border-top-left-radius: 12px;
    border-top-right-radius: 12px;
}

QLabel#brandPortrait {
    border: 1px solid #2d3140;
    border-radius: 4px;
    background-color: #14161c;
    padding: 0px;
}

QLabel#brandWordmark {
    font-size: 19pt;
    font-weight: 600;
    letter-spacing: -0.5px;
    padding: 0;
}

QLabel#brandSubtitle {
    color: #6b7080;
    font-size: 9pt;
    font-weight: 400;
    letter-spacing: 2px;
    padding-top: 2px;
}

QLabel#brandAuthor {
    font-size: 9pt;
    font-weight: 500;
    letter-spacing: 0;
    padding: 6px 0;
}

/* === MODE TOGGLE (segmented control) === */
QFrame#modeToggleHost {
    background-color: #14161c;
    border: 1px solid #2d3140;
    border-radius: 6px;
}

QFrame#modeIndicator {
    background-color: #d4a960;
    border-radius: 4px;
}

QPushButton#modeBtnLeft, QPushButton#modeBtnRight {
    background-color: transparent;
    color: #9ca0aa;
    border: none;
    padding: 7px 18px;
    font-weight: 500;
    letter-spacing: 1px;
    font-size: 9pt;
}
QPushButton#modeBtnLeft:checked, QPushButton#modeBtnRight:checked {
    color: #14161c;
    font-weight: 600;
}
QPushButton#modeBtnLeft:hover:!checked, QPushButton#modeBtnRight:hover:!checked {
    color: #e6e8ec;
}

/* === WINDOW CONTROLS === */
QPushButton#winMinBtn, QPushButton#winCloseBtn {
    background-color: transparent;
    color: #9ca0aa;
    border: none;
    border-radius: 4px;
    font-size: 11pt;
    font-weight: 400;
    padding: 0;
}
QPushButton#winMinBtn:hover {
    background-color: #232733;
    color: #e6e8ec;
}
QPushButton#winCloseBtn:hover {
    background-color: #4d2d2d;
    color: #e6e8ec;
}

/* === HERO STRIP === */
QFrame#heroCard {
    background-color: #1d2028;
    border: 1px solid #2d3140;
    border-radius: 8px;
}

/* HeroCell paints transparent so heroCard's lighter surface shows through. */
HeroCell { background-color: transparent; }

QLabel#heroLabel {
    color: #9ca0aa;
    font-size: 8pt;
    font-weight: 600;
    letter-spacing: 2px;
}

QLabel#heroValue, QLabel#heroValueB {
    color: #d4a960;
    font-family: 'JetBrains Mono', 'Fira Code', monospace;
    font-size: 22pt;
    font-weight: 700;
    letter-spacing: -1px;
}

QLineEdit#heroValueEdit, QLineEdit#heroValueEditB {
    background-color: #14161c;
    color: #e6e8ec;
    border: 1px solid #d4a960;
    border-radius: 4px;
    padding: 0 6px;
    font-family: 'JetBrains Mono', 'Fira Code', monospace;
    font-size: 22pt;
    font-weight: 700;
    selection-background-color: #463826;
    selection-color: #e6e8ec;
}

QLabel#heroDivider {
    background-color: #2d3140;
    max-width: 1px;
    min-width: 1px;
}

/* === BODY === */
QFrame#bodyCard {
    background-color: #1d2028;
    border: 1px solid #2d3140;
    border-radius: 8px;
}

/* === BUTTONS === */
QPushButton {
    background-color: #232733;
    color: #e6e8ec;
    border: 1px solid #2d3140;
    border-radius: 6px;
    padding: 8px 16px;
    font-weight: 500;
    letter-spacing: 0;
    font-size: 9pt;
}
QPushButton:hover {
    background-color: #2a2f3d;
    border: 1px solid #3d4252;
}
QPushButton:pressed {
    background-color: #1d2028;
}
QPushButton:disabled {
    color: #6b7080;
    background-color: #1a1d24;
    border: 1px solid #232733;
}

QPushButton#primaryButton {
    background-color: #d4a960;
    color: #14161c;
    border: 1px solid #d4a960;
    font-weight: 600;
}
QPushButton#primaryButton:hover {
    background-color: #e0b878;
    border: 1px solid #e0b878;
}
QPushButton#primaryButton:pressed {
    background-color: #b8923f;
}
QPushButton#primaryButton:disabled {
    background-color: #1a1d24;
    color: #6b7080;
    border: 1px solid #232733;
}

QPushButton#secondaryButton {
    background-color: transparent;
    color: #d4a960;
    border: 1px solid #463826;
}
QPushButton#secondaryButton:hover {
    background-color: #463826;
    color: #e0b878;
    border: 1px solid #d4a960;
}
QPushButton#secondaryButton:disabled {
    color: #6b7080;
    border: 1px solid #232733;
}

/* === FILTER INPUT === */
QLineEdit {
    background-color: #14161c;
    border: 1px solid #2d3140;
    border-radius: 6px;
    padding: 8px 12px;
    color: #e6e8ec;
    font-size: 10pt;
    selection-background-color: #463826;
    selection-color: #e6e8ec;
}
QLineEdit:focus {
    border: 1px solid #d4a960;
    background-color: #1a1d24;
}
QLineEdit:disabled {
    color: #6b7080;
    background-color: #181a22;
}

/* Cell editor (table double-click) */
QTableView QLineEdit, QAbstractItemView QLineEdit {
    background-color: #14161c;
    color: #e6e8ec;
    border: 1px solid #d4a960;
    border-radius: 0;
    padding: 2px 8px;
    margin: 0;
    font-family: 'JetBrains Mono', 'Fira Code', monospace;
    font-size: 10pt;
    selection-background-color: #463826;
    selection-color: #e6e8ec;
}

/* === TABLE === */
QTableWidget {
    background-color: transparent;
    alternate-background-color: #1a1d24;
    gridline-color: transparent;
    border: none;
    selection-background-color: #232733;
    selection-color: #e6e8ec;
    outline: none;
    font-family: 'JetBrains Mono', 'Fira Code', monospace;
    font-size: 9pt;
}
QTableWidget::item {
    padding: 6px 14px;
    border: none;
}
QTableWidget::item:hover {
    background-color: #232733;
}
QTableWidget::item:selected {
    background-color: #232733;
    color: #d4a960;
}

QHeaderView::section {
    background-color: transparent;
    color: #9ca0aa;
    padding: 12px 14px;
    border: none;
    border-bottom: 1px solid #2d3140;
    font-family: 'Inter', sans-serif;
    font-size: 9pt;
    font-weight: 600;
    letter-spacing: 1px;
}

/* === STATUS BAR === */
QStatusBar {
    background-color: #181a22;
    color: #9ca0aa;
    border-top: 1px solid #2d3140;
    border-bottom-left-radius: 12px;
    border-bottom-right-radius: 12px;
    font-size: 9pt;
    letter-spacing: 0;
    padding-left: 14px;
    padding-right: 14px;
}
QStatusBar::item { border: none; }

QLabel#statusDot {
    color: #d4a960;
    font-size: 12pt;
}

/* === SCROLLBARS === */
QScrollBar:vertical {
    background-color: transparent;
    width: 8px;
    margin: 4px 0;
}
QScrollBar::handle:vertical {
    background-color: #2d3140;
    border-radius: 4px;
    min-height: 30px;
}
QScrollBar::handle:vertical:hover { background-color: #d4a960; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }

/* Table scrollbar starts BELOW the column header (40px) so the track aligns
   with the data rows, not the header. */
QTableWidget QScrollBar:vertical {
    margin: 40px 4px 6px 0;
    width: 8px;
}
QTableWidget QScrollBar::handle:vertical {
    background-color: #3d4252;
    border-radius: 4px;
    min-height: 40px;
}
QTableWidget QScrollBar::handle:vertical:hover {
    background-color: #d4a960;
}

QScrollBar:horizontal {
    background-color: transparent;
    height: 8px;
    margin: 0 4px;
}
QScrollBar::handle:horizontal {
    background-color: #2d3140;
    border-radius: 4px;
    min-width: 30px;
}
QScrollBar::handle:horizontal:hover { background-color: #d4a960; }
QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }
QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background: transparent; }

/* === DIALOGS / TOOLTIPS === */
QMessageBox { background-color: #1d2028; }
QToolTip {
    background-color: #232733;
    color: #e6e8ec;
    border: 1px solid #2d3140;
    border-radius: 4px;
    padding: 4px 8px;
}
)QSS";

#include <QIcon>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("WolknerProtocol");
    QApplication::setOrganizationName("Wleeaf");
    QApplication::setWindowIcon(QIcon(":/icons/serge.png"));
    // Wayland: the compositor maps dock/taskbar icons by app_id ↔ .desktop file.
    // setDesktopFileName makes Qt advertise this app_id so installed
    // .desktop files (with matching StartupWMClass) get picked up.
    QGuiApplication::setDesktopFileName("wolknerprotocol");

    // Embed both font families so the UI renders identically everywhere.
    QFontDatabase::addApplicationFont(":/fonts/JetBrainsMono-Regular.ttf");
    QFontDatabase::addApplicationFont(":/fonts/JetBrainsMono-Bold.ttf");
    QFontDatabase::addApplicationFont(":/fonts/Inter-Regular.ttf");
    QFontDatabase::addApplicationFont(":/fonts/Inter-Medium.ttf");
    QFontDatabase::addApplicationFont(":/fonts/Inter-SemiBold.ttf");

    app.setStyleSheet(kStyleSheet);

    MainWindow w;
    w.show();
    return app.exec();
}
