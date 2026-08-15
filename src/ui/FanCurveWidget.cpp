// LiquidCam - FanCurveWidget.cpp
#include "FanCurveWidget.h"
#include "Theme.h"

#include <QLineF>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPolygonF>

#include <algorithm>
#include <cmath>

namespace lc {
namespace {

constexpr qreal kMarginLeft   = 38.0;
constexpr qreal kMarginRight  = 12.0;
constexpr qreal kMarginTop    = 12.0;
constexpr qreal kMarginBottom = 26.0;
constexpr qreal kGrabRadius   = 11.0;

constexpr int kAxisMin = 20;    // 20 .. 100 on both axes keeps the plot readable
constexpr int kAxisMax = 100;

} // namespace

FanCurveWidget::FanCurveWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumHeight(200);
    setMouseTracking(true);
    setCursor(Qt::CrossCursor);
}

void FanCurveWidget::setCurve(const FanCurve& curve)
{
    curve_ = curve;
    curve_.sort();
    update();
}

void FanCurveWidget::setEditable(bool editable)
{
    editable_ = editable;
    setCursor(editable ? Qt::CrossCursor : Qt::ArrowCursor);
    update();
}

void FanCurveWidget::setMarker(float x, int duty, bool valid)
{
    if (markerValid_ == valid && std::fabs(markerX_ - x) < 0.4f && markerDuty_ == duty)
        return;                       // nothing visible would change; skip the repaint
    markerX_     = x;
    markerDuty_  = duty;
    markerValid_ = valid;
    update();
}

QRectF FanCurveWidget::plotRect() const
{
    return QRectF(kMarginLeft, kMarginTop,
                  std::max<qreal>(10.0, width() - kMarginLeft - kMarginRight),
                  std::max<qreal>(10.0, height() - kMarginTop - kMarginBottom));
}

QPointF FanCurveWidget::toPixels(qreal x, qreal y) const
{
    const QRectF r = plotRect();
    const qreal fx = (x - kAxisMin) / qreal(kAxisMax - kAxisMin);
    const qreal fy = (y - kAxisMin) / qreal(kAxisMax - kAxisMin);
    return QPointF(r.left() + fx * r.width(), r.bottom() - fy * r.height());
}

QPointF FanCurveWidget::toValues(const QPointF& pixels) const
{
    const QRectF r = plotRect();
    const qreal fx = (pixels.x() - r.left()) / r.width();
    const qreal fy = (r.bottom() - pixels.y()) / r.height();
    return QPointF(kAxisMin + fx * (kAxisMax - kAxisMin),
                   kAxisMin + fy * (kAxisMax - kAxisMin));
}

int FanCurveWidget::hitTest(const QPointF& pixels) const
{
    for (uint8_t i = 0; i < curve_.count; ++i) {
        const QPointF p = toPixels(curve_.points[i].x, curve_.points[i].y);
        if (QLineF(p, pixels).length() <= kGrabRadius)
            return i;
    }
    return -1;
}

void FanCurveWidget::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRectF r = plotRect();

    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0x17, 0x17, 0x1D));
    painter.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 5, 5);

    // Grid, every 10 units.
    QFont small = font();
    small.setPointSize(8);
    painter.setFont(small);

    for (int v = kAxisMin; v <= kAxisMax; v += 10) {
        const QPointF x0 = toPixels(v, kAxisMin);
        const QPointF y0 = toPixels(kAxisMin, v);
        const bool major = (v % 20 == 0);
        painter.setPen(QPen(major ? theme::kLine : QColor(0x24, 0x24, 0x2E), 1.0));
        painter.drawLine(QPointF(x0.x(), r.top()), QPointF(x0.x(), r.bottom()));
        painter.drawLine(QPointF(r.left(), y0.y()), QPointF(r.right(), y0.y()));

        if (major) {
            painter.setPen(theme::kTextMuted);
            painter.drawText(QRectF(x0.x() - 16, r.bottom() + 4, 32, 14),
                             Qt::AlignHCenter | Qt::AlignTop, QString::number(v));
            painter.drawText(QRectF(2, y0.y() - 8, kMarginLeft - 8, 16),
                             Qt::AlignRight | Qt::AlignVCenter, QString::number(v) + '%');
        }
    }

    painter.setPen(theme::kTextMuted);
    painter.drawText(QRectF(r.left(), r.bottom() + 4, r.width(), 16),
                     Qt::AlignRight | Qt::AlignTop, axisTitle_);

    if (curve_.count == 0)
        return;

    // Curve polyline, extended flat to both edges.
    QPolygonF line;
    line << QPointF(r.left(), toPixels(kAxisMin, curve_.points[0].y).y());
    for (uint8_t i = 0; i < curve_.count; ++i)
        line << toPixels(curve_.points[i].x, curve_.points[i].y);
    line << QPointF(r.right(), toPixels(kAxisMax, curve_.points[curve_.count - 1].y).y());

    QPolygonF filled = line;
    filled << QPointF(r.right(), r.bottom()) << QPointF(r.left(), r.bottom());

    QLinearGradient gradient(0, r.top(), 0, r.bottom());
    gradient.setColorAt(0.0, QColor(0x8B, 0x5C, 0xF6, 90));
    gradient.setColorAt(1.0, QColor(0x8B, 0x5C, 0xF6, 8));
    painter.setPen(Qt::NoPen);
    painter.setBrush(gradient);
    painter.drawPolygon(filled);

    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(theme::kAccent, 2.0));
    painter.drawPolyline(line);

    // Live sensor marker.
    if (markerValid_) {
        const qreal clamped = std::min<qreal>(kAxisMax, std::max<qreal>(kAxisMin, markerX_));
        const QPointF p = toPixels(clamped, std::max(kAxisMin, markerDuty_));
        painter.setPen(QPen(theme::kGood, 1.0, Qt::DashLine));
        painter.drawLine(QPointF(p.x(), r.top()), QPointF(p.x(), r.bottom()));
        painter.setPen(Qt::NoPen);
        painter.setBrush(theme::kGood);
        painter.drawEllipse(p, 4.0, 4.0);
    }

    // Handles.
    for (uint8_t i = 0; i < curve_.count; ++i) {
        const QPointF p = toPixels(curve_.points[i].x, curve_.points[i].y);
        const bool active = (int(i) == dragIndex_ || int(i) == hoverIndex_);
        painter.setPen(QPen(QColor(0x14, 0x14, 0x19), 2.0));
        painter.setBrush(active ? QColor(0xFF, 0xFF, 0xFF) : theme::kAccent);
        painter.drawEllipse(p, active ? 6.5 : 5.0, active ? 6.5 : 5.0);
    }

    if (hoverIndex_ >= 0 && hoverIndex_ < int(curve_.count)) {
        const auto& pt = curve_.points[hoverIndex_];
        const QPointF p = toPixels(pt.x, pt.y);
        painter.setFont(theme::readoutFont(8, true));
        painter.setPen(theme::kText);
        painter.drawText(QRectF(p.x() - 30, p.y() - 24, 60, 14), Qt::AlignCenter,
                         QStringLiteral("%1\u00B0 %2%%").arg(pt.x).arg(pt.y));
    }
}

void FanCurveWidget::mousePressEvent(QMouseEvent* event)
{
    if (!editable_)
        return;

    const int index = hitTest(event->pos());
    if (event->button() == Qt::RightButton) {
        if (index >= 0 && curve_.count > 2) {
            for (int i = index; i + 1 < int(curve_.count); ++i)
                curve_.points[i] = curve_.points[i + 1];
            --curve_.count;
            hoverIndex_ = -1;
            update();
            emit curveEdited();
        }
        return;
    }

    if (event->button() == Qt::LeftButton && index >= 0) {
        dragIndex_ = index;
        update();
    }
}

void FanCurveWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (dragIndex_ >= 0 && dragIndex_ < int(curve_.count)) {
        const QPointF v = toValues(event->pos());
        auto& pt = curve_.points[dragIndex_];

        // Keep the point between its neighbours so the curve stays monotonic in x.
        int minX = kAxisMin, maxX = kAxisMax;
        if (dragIndex_ > 0)                    minX = curve_.points[dragIndex_ - 1].x + 1;
        if (dragIndex_ + 1 < int(curve_.count)) maxX = curve_.points[dragIndex_ + 1].x - 1;

        pt.x = static_cast<int16_t>(std::clamp<int>(int(std::lround(v.x())), minX, maxX));
        pt.y = static_cast<int16_t>(std::clamp<int>(int(std::lround(v.y())), 0, 100));
        update();
        emit curveEdited();
        return;
    }

    const int index = hitTest(event->pos());
    if (index != hoverIndex_) {
        hoverIndex_ = index;
        setCursor(index >= 0 ? Qt::SizeAllCursor : (editable_ ? Qt::CrossCursor : Qt::ArrowCursor));
        update();
    }
}

void FanCurveWidget::mouseReleaseEvent(QMouseEvent*)
{
    if (dragIndex_ >= 0) {
        dragIndex_ = -1;
        curve_.sort();
        update();
        emit curveEdited();
    }
}

void FanCurveWidget::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (!editable_ || curve_.count >= FanCurve::kMaxPoints)
        return;
    if (hitTest(event->pos()) >= 0)
        return;

    const QPointF v = toValues(event->pos());
    curve_.add(int(std::lround(v.x())), int(std::lround(v.y())));
    curve_.sort();
    update();
    emit curveEdited();
}

void FanCurveWidget::leaveEvent(QEvent*)
{
    if (hoverIndex_ != -1) {
        hoverIndex_ = -1;
        update();
    }
}

} // namespace lc

#include "moc_FanCurveWidget.cpp"
