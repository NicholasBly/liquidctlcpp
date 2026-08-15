// LiquidCam - FanCurveWidget.h
// Direct-manipulation curve editor. Drag a point to move it, double-click the
// canvas to add one, right-click a point to delete it.
#pragma once

#include <QString>
#include <QWidget>

#include "core/FanCurve.h"

namespace lc {

class FanCurveWidget : public QWidget {
    Q_OBJECT

public:
    explicit FanCurveWidget(QWidget* parent = nullptr);

    void     setCurve(const FanCurve& curve);
    FanCurve curve() const { return curve_; }

    void setEditable(bool editable);
    void setAxisTitle(const QString& title) { axisTitle_ = title; update(); }

    // Live indicator: where the current sensor reading lands on the curve.
    void setMarker(float x, int duty, bool valid);

signals:
    void curveEdited();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    QRectF  plotRect() const;
    QPointF toPixels(qreal x, qreal y) const;
    QPointF toValues(const QPointF& pixels) const;
    int     hitTest(const QPointF& pixels) const;

    FanCurve curve_ = FanCurve::defaultCustom();
    QString  axisTitle_ = QStringLiteral("CPU temperature (\u00B0C)");
    int      dragIndex_  = -1;
    int      hoverIndex_ = -1;
    bool     editable_   = true;
    float    markerX_    = 0.f;
    int      markerDuty_ = 0;
    bool     markerValid_ = false;
};

} // namespace lc
