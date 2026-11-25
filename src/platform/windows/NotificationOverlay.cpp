#include "platform/NotificationOverlay.h"

#include <QConicalGradient>
#include <QDateTime>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>
#include <QScreen>
#include <QSystemTrayIcon>
#include <QTextLayout>
#include <QTimer>
#include <QWidget>

#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <cmath>
#include <set>

// The card from macOS (NotificationOverlay.mm), drawn with Qt: Mirdain's colors and Aileron,
// one row per notification. It sits beside the tray icon, above the taskbar on whichever edge
// that is, without a notch: Windows flyouts have none.

namespace {

constexpr int CardWidth = 300, Padding = 12, IconSize = 16, TextInset = Padding + IconSize + 10, CloseSize = 22;
// Room around the card for the shadow it draws, and its distance from the taskbar and screen edges.
constexpr int ShadowMargin = 16, ScreenGap = 12, Radius = 8;

QColor color(QRgb rgb, double alpha = 1)
{
    QColor result(rgb);
    result.setAlphaF(alpha);
    return result;
}
const QColor Background = color(0x0a0e11), Line = color(0x29383f), Border = color(0x3b4e57), Text = color(0xe7eef4),
    Muted = color(0x9dacbc), Accent = color(0xe967c8), Purple = color(0xa46ff2), Warning = color(0xf2a07a),
    Hover = color(0x172429), Pressed = color(0xe967c8, 0.14);

QFont aileron(bool semiBold, int pixels)
{
    static const auto families = [] {
        QStringList result;
        for (const auto *file : {":/assets/fonts/Aileron-Regular.otf", ":/assets/fonts/Aileron-SemiBold.otf"})
            result.append(QFontDatabase::applicationFontFamilies(QFontDatabase::addApplicationFont(file)).value(0));
        return result;
    }();
    QFont font(families[semiBold]);
    font.setPixelSize(pixels);
    font.setWeight(semiBold ? QFont::DemiBold : QFont::Normal);
    return font;
}

// text wrapped to width, at most lines lines (0 for all), its last line cut short with an
// ellipsis when there is more.
QStringList wrap(const QString &text, const QFont &font, int width, int lines)
{
    QTextLayout layout(text, font);
    QStringList result;
    layout.beginLayout();
    for (auto line = layout.createLine(); line.isValid(); line = layout.createLine()) {
        line.setLineWidth(width);
        if (lines && result.size() == lines - 1) {
            result.append(QFontMetrics(font).elidedText(text.mid(line.textStart()), Qt::ElideRight, width));
            break;
        }
        result.append(text.mid(line.textStart(), line.textLength()).trimmed());
    }
    layout.endLayout();
    return result;
}

using Indicator = NotificationOverlay::Indicator;
using Item = NotificationOverlay::Item;

// One notification's place in the card and the text it shows.
struct Row {
    Item item;
    QRect bounds;
    QRect close;
    QString title;
    QStringList body;
    int inset;
    // A failed notification with nothing to open shows two lines of its error until clicked, then
    // all of it. One that can be opened (a Gwaith run with its session) opens instead.
    bool expandable;
    bool opens() const { return expandable || item.clickable; }
};

void drawIcon(QPainter &painter, Indicator indicator, QPointF origin)
{
    painter.save();
    painter.translate(origin);
    painter.setRenderHint(QPainter::Antialiasing);
    if (indicator == Indicator::Spinner) {
        // A ring whose comet tail runs from transparent through purple to Mirdain pink, turning
        // clockwise. Its angle comes from the clock, so it keeps turning across repaints.
        const auto phase = std::fmod(QDateTime::currentMSecsSinceEpoch() / 900.0, 1.0);
        QConicalGradient gradient(QPointF(IconSize / 2.0, IconSize / 2.0), -360 * phase);
        gradient.setColorAt(0, Accent);
        gradient.setColorAt(0.5, Purple);
        gradient.setColorAt(1, color(0xa46ff2, 0));
        painter.setPen(QPen(QBrush(gradient), 2, Qt::SolidLine, Qt::RoundCap));
        painter.drawEllipse(QRectF(1.5, 1.5, IconSize - 3, IconSize - 3));
    } else {
        // A filled shape with a mark knocked out of it in the card's color.
        QPainterPath mark;
        if (indicator == Indicator::Done) {
            mark.moveTo(4.6, 8.3);
            mark.lineTo(7, 10.7);
            mark.lineTo(11.4, 5.7);
        } else {
            mark.moveTo(8, indicator == Indicator::Failed ? 6 : 4.4);
            mark.lineTo(8, 9.3);
        }
        painter.setPen(Qt::NoPen);
        painter.setBrush(indicator == Indicator::Failed ? Warning : Accent);
        if (indicator == Indicator::Failed) painter.drawPolygon(QPolygonF{{8, 1.5}, {15.2, 14.3}, {0.8, 14.3}});
        else painter.drawEllipse(QRectF(0.5, 0.5, IconSize - 1, IconSize - 1));
        painter.strokePath(mark, QPen(Background, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        // The exclamation mark's dot.
        if (indicator != Indicator::Done) {
            painter.setBrush(Background);
            painter.drawEllipse(QPointF(8, 11.9), 1.05, 1.05);
        }
    }
    painter.restore();
}

class Card : public QWidget {
public:
    std::function<void(const QString &)> activate;
    std::function<void(const QString &)> dismiss;
    // Failed notifications showing their whole error, by ID.
    std::set<QString> expanded;
    QList<Row> rows;

    Card() : QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::WindowDoesNotAcceptFocus
                              | Qt::NoDropShadowWindowHint)
    {
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_ShowWithoutActivating);
        setMouseTracking(true);
        // Spinners turn while any shows.
        spin.setInterval(16);
        connect(&spin, &QTimer::timeout, this, qOverload<>(&QWidget::update));
    }

    // Lays the rows out; returns the card's size.
    QSize layout(const QList<Item> &items)
    {
        const auto titleFont = aileron(true, 13), bodyFont = aileron(false, 12);
        const QFontMetrics titleMetrics(titleFont), bodyMetrics(bodyFont);
        rows.clear();
        // Rows sit one pixel inside the card's top and bottom border, with a line between each.
        int y = 1;
        for (const auto &item : items) {
            if (y > 1) y += 1;
            Row row{item, {}, {}, {}, {}, 0, item.indicator == Indicator::Failed && !item.clickable};
            // Rows with no icon (none, or a progress bar beneath) start their text at the edge.
            row.inset = item.indicator == Indicator::None || item.indicator == Indicator::Progress ? Padding : TextInset;
            const int textWidth = CardWidth - row.inset - Padding;
            row.title = titleMetrics.elidedText(item.title, Qt::ElideRight, textWidth - CloseSize);
            if (!item.body.isEmpty()) row.body = wrap(item.body, bodyFont, textWidth, row.expandable && expanded.count(item.id) ? 0 : 2);
            int height = Padding + titleMetrics.height();
            if (!row.body.isEmpty()) height += 2 + int(row.body.size()) * bodyMetrics.lineSpacing();
            if (item.indicator == Indicator::Progress) height += 8 + 3;
            height += Padding;
            row.bounds = QRect(0, y, CardWidth, height);
            row.close = QRect(CardWidth - Padding - CloseSize + 4, y + Padding - 4, CloseSize, CloseSize);
            rows.append(row);
            y += height;
        }
        const bool spinning = std::any_of(items.begin(), items.end(), [](const Item &item) { return item.indicator == Indicator::Spinner; });
        if (spinning != spin.isActive()) spinning ? spin.start() : spin.stop();
        update();
        return QSize(CardWidth, y + 1);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QRectF card = rect().adjusted(ShadowMargin, ShadowMargin, -ShadowMargin, -ShadowMargin);

        // A soft shadow, layered outward and a little downward.
        painter.setPen(Qt::NoPen);
        for (int spread = ShadowMargin; spread > 0; --spread) {
            const double fade = 1 - double(spread) / ShadowMargin;
            painter.setBrush(color(0x000000, 0.06 * fade * fade));
            painter.drawRoundedRect(card.adjusted(-spread, -spread + 4, spread, spread + 4).intersected(QRectF(rect())),
                Radius + spread, Radius + spread);
        }
        painter.setBrush(Background);
        painter.drawRoundedRect(card, Radius, Radius);

        painter.save();
        painter.translate(card.topLeft());
        QPainterPath clip;
        clip.addRoundedRect(QRectF(0, 0, card.width(), card.height()), Radius, Radius);
        painter.setClipPath(clip);
        const auto titleFont = aileron(true, 13), bodyFont = aileron(false, 12);
        const QFontMetrics titleMetrics(titleFont), bodyMetrics(bodyFont);
        for (qsizetype index = 0; index < rows.size(); ++index) {
            const auto &row = rows[index];
            if (index) painter.fillRect(QRect(1, row.bounds.top() - 1, CardWidth - 2, 1), Line);
            // Raised while hovered, tinted pink while pressed; inset from the card's border on each side.
            if (row.opens() && index == hovered && !overClose)
                painter.fillRect(row.bounds.adjusted(1, 0, -1, 0), index == pressed ? Pressed : Hover);

            const auto &item = row.item;
            if (item.indicator != Indicator::None && item.indicator != Indicator::Progress)
                drawIcon(painter, item.indicator, QPointF(Padding, row.bounds.top() + Padding + 1));
            int y = row.bounds.top() + Padding;
            painter.setFont(titleFont);
            painter.setPen(Text);
            painter.drawText(row.inset, y + titleMetrics.ascent(), row.title);
            y += titleMetrics.height() + 2;
            painter.setFont(bodyFont);
            painter.setPen(item.indicator == Indicator::Failed ? Warning : Muted);
            for (const auto &line : row.body) {
                painter.drawText(row.inset, y + bodyMetrics.ascent(), line);
                y += bodyMetrics.lineSpacing();
            }
            if (item.indicator == Indicator::Progress) {
                // A thin bar filled to progress, in the spinner's purple-to-pink.
                const QRectF track(row.inset, y + 6, CardWidth - row.inset - Padding, 3);
                painter.setPen(Qt::NoPen);
                painter.setBrush(Line);
                painter.drawRoundedRect(track, 1.5, 1.5);
                QLinearGradient fill(track.topLeft(), track.topRight());
                fill.setColorAt(0, Purple);
                fill.setColorAt(1, Accent);
                painter.setBrush(fill);
                painter.drawRoundedRect(QRectF(track.topLeft(), QSizeF(track.width() * std::clamp(item.progress, 0.0, 1.0), 3)), 1.5, 1.5);
            }

            if (index == hovered && overClose) {
                painter.setPen(Qt::NoPen);
                painter.setBrush(Hover);
                painter.drawRoundedRect(row.close, 4, 4);
            }
            const QPointF center = QRectF(row.close).center();
            painter.setPen(QPen(index == hovered && overClose ? Text : Muted, 1.3, Qt::SolidLine, Qt::RoundCap));
            painter.drawLine(center + QPointF(-4, -4), center + QPointF(4, 4));
            painter.drawLine(center + QPointF(4, -4), center + QPointF(-4, 4));
        }
        painter.restore();
        painter.setPen(QPen(Border, 1));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(card.adjusted(0.5, 0.5, -0.5, -0.5), Radius, Radius);
    }

    void mouseMoveEvent(QMouseEvent *event) override { hover(event->position().toPoint()); }

    void leaveEvent(QEvent *) override
    {
        hovered = pressed = -1;
        overClose = false;
        unsetCursor();
        update();
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() != Qt::LeftButton || hovered < 0) return;
        pressed = hovered;
        pressedClose = overClose;
        update();
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() != Qt::LeftButton) return;
        const int row = pressed;
        const bool close = pressedClose;
        pressed = -1;
        update();
        if (row < 0 || row != hovered || close != overClose) return;
        const auto &target = rows[row];
        const auto id = target.item.id;
        // Handlers rebuild the card, so they run once the click is done.
        if (close) QTimer::singleShot(0, this, [this, id] { dismiss(id); });
        else if (target.expandable) QTimer::singleShot(0, this, [this, id] {
            if (!expanded.erase(id)) expanded.insert(id);
            onExpand();
        });
        else if (target.item.clickable) QTimer::singleShot(0, this, [this, id] { activate(id); });
    }

public:
    std::function<void()> onExpand;

    // Rows are rebuilt on every update: one appearing under the mouse starts hovered.
    void refreshHover()
    {
        if (isVisible()) hover(mapFromGlobal(QCursor::pos()));
    }

private:
    QTimer spin;
    int hovered = -1;
    int pressed = -1;
    bool overClose = false;
    bool pressedClose = false;

    void hover(QPoint point)
    {
        const auto inCard = point - QPoint(ShadowMargin, ShadowMargin);
        int row = -1;
        for (qsizetype index = 0; index < rows.size(); ++index)
            if (rows[index].bounds.contains(inCard)) row = int(index);
        const bool close = row >= 0 && rows[row].close.contains(inCard);
        const bool hand = row >= 0 && (close || rows[row].opens());
        if (hand) setCursor(Qt::PointingHandCursor);
        else unsetCursor();
        if (row == hovered && close == overClose) return;
        hovered = row;
        overClose = close;
        update();
    }
};

// A full-screen app, game, or presentation in front: the card would cover it.
bool fullScreenInFront()
{
    QUERY_USER_NOTIFICATION_STATE state;
    if (FAILED(SHQueryUserNotificationState(&state))) return false;
    return state == QUNS_BUSY || state == QUNS_RUNNING_D3D_FULL_SCREEN || state == QUNS_PRESENTATION_MODE;
}

}

struct NotificationOverlay::Impl {
    explicit Impl(QSystemTrayIcon &tray) : tray(tray) {}
    QSystemTrayIcon &tray;
    Card card;
    QPropertyAnimation fade{&card, "windowOpacity"};
    // While a full-screen app covers the card, checks for it leaving.
    QTimer coveredCheck;
    // The latest notifications, kept while the card is hidden for a full-screen app.
    QList<Item> items;
    bool shown = false;

    void update();
    void hide();
    QPoint position(QSize size) const;
};

// Where the card's top left goes: beside the tray icon, inside the screen's work area, so
// above a bottom taskbar, below a top one, and beside a side one. Without the icon's place (it
// can sit in the overflow), the work area's bottom-right corner.
QPoint NotificationOverlay::Impl::position(QSize size) const
{
    const auto icon = tray.geometry();
    auto *screen = icon.isValid() ? QGuiApplication::screenAt(icon.center()) : nullptr;
    if (!screen) screen = QGuiApplication::primaryScreen();
    const auto area = screen->availableGeometry();
    const int left = area.x() + ScreenGap, right = area.x() + area.width() - size.width() - ScreenGap;
    const int top = area.y() + ScreenGap, bottom = area.y() + area.height() - size.height() - ScreenGap;
    if (!icon.isValid()) return {right, bottom};
    return {std::clamp(icon.center().x() - size.width() / 2, left, right), std::clamp(icon.center().y() - size.height() / 2, top, bottom)};
}

NotificationOverlay::NotificationOverlay(QSystemTrayIcon &tray, std::function<void(const QString &)> activate,
    std::function<void(const QString &)> dismiss)
    : impl(new Impl(tray))
{
    // Clicks act without activating Forward or taking focus from the app in front.
    const auto window = reinterpret_cast<HWND>(impl->card.winId());
    SetWindowLongPtrW(window, GWL_EXSTYLE, GetWindowLongPtrW(window, GWL_EXSTYLE) | WS_EX_NOACTIVATE);
    impl->card.activate = std::move(activate);
    impl->card.dismiss = std::move(dismiss);
    impl->card.onExpand = [state = impl.get()] { state->update(); };
    QObject::connect(&impl->fade, &QPropertyAnimation::finished, &impl->card, [state = impl.get()] {
        if (!state->shown) state->card.hide();
    });
    impl->coveredCheck.setInterval(2000);
    QObject::connect(&impl->coveredCheck, &QTimer::timeout, &impl->card, [state = impl.get()] { state->update(); });
}

bool NotificationOverlay::available() { return true; }

NotificationOverlay::~NotificationOverlay() = default;

void NotificationOverlay::show(const QList<Item> &items)
{
    impl->items = items;
    auto &expanded = impl->card.expanded;
    for (auto id = expanded.begin(); id != expanded.end();)
        if (std::none_of(items.begin(), items.end(), [&](const Item &item) { return item.id == *id; })) id = expanded.erase(id);
        else ++id;
    impl->update();
}

void NotificationOverlay::Impl::hide()
{
    if (!shown) return;
    shown = false;
    fade.stop();
    fade.setDuration(140);
    fade.setEasingCurve(QEasingCurve::InCubic);
    fade.setStartValue(card.windowOpacity());
    fade.setEndValue(0.0);
    fade.start();
}

void NotificationOverlay::Impl::update()
{
    const bool covered = !items.isEmpty() && fullScreenInFront();
    if (covered != coveredCheck.isActive()) covered ? coveredCheck.start() : coveredCheck.stop();
    if (items.isEmpty() || covered) {
        hide();
        return;
    }
    const auto size = card.layout(items) + QSize(2 * ShadowMargin, 2 * ShadowMargin);
    card.setGeometry(QRect(position(size - QSize(2 * ShadowMargin, 2 * ShadowMargin)) - QPoint(ShadowMargin, ShadowMargin), size));
    card.refreshHover();
    if (shown) return;
    shown = true;
    fade.stop();
    if (!card.isVisible()) card.setWindowOpacity(0);
    card.show();
    card.raise();
    fade.setDuration(180);
    fade.setEasingCurve(QEasingCurve::OutCubic);
    fade.setStartValue(card.windowOpacity());
    fade.setEndValue(1.0);
    fade.start();
}
