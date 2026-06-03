#include "CurveEditor.h"
#include "core/CurveSolver.h"

#include <QPainter>
#include <QMouseEvent>
#include <QPainterPath>
#include <algorithm>

namespace HighPro {

namespace {
constexpr int kPad = 6;
constexpr int kHandleR = 4;        // 控制点半径 (像素)
constexpr int kPickR = 8;          // 命中半径

QColor channelColor(int ch)
{
    switch (ch) {
    case 1: return QColor(220, 80, 80);
    case 2: return QColor(80, 220, 80);
    case 3: return QColor(80, 140, 240);
    default: return QColor(220, 220, 220);
    }
}
} // namespace

CurveEditor::CurveEditor(QWidget* parent) : QWidget(parent)
{
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(180, 180);
}

void CurveEditor::setCurves(const CurveParams& cp)
{
    m_curves = cp;
    update();
}

void CurveEditor::setChannel(int ch)
{
    if (ch < 0 || ch > 3 || ch == m_channel) return;
    m_channel = ch;
    update();
}

void CurveEditor::resetCurrent()
{
    auto& p = curPts();
    p = { {0,0}, {255,255} };
    update();
    emit curvesChanged();
}

void CurveEditor::resetAll()
{
    m_curves.reset();
    update();
    emit curvesChanged();
}

CurveParams::Pts& CurveEditor::curPts()
{
    switch (m_channel) {
    case 1: return m_curves.r;
    case 2: return m_curves.g;
    case 3: return m_curves.b;
    default: return m_curves.master;
    }
}

const CurveParams::Pts& CurveEditor::curPts() const
{
    return const_cast<CurveEditor*>(this)->curPts();
}

QPoint CurveEditor::dataToScreen(int x, int y) const
{
    const int W = width()  - 2 * kPad;
    const int H = height() - 2 * kPad;
    if (W <= 0 || H <= 0) return {};
    int sx = kPad + (int)std::lround((double)x / 255.0 * W);
    int sy = kPad + H - (int)std::lround((double)y / 255.0 * H);
    return { sx, sy };
}

QPoint CurveEditor::screenToData(const QPoint& s) const
{
    const int W = width()  - 2 * kPad;
    const int H = height() - 2 * kPad;
    if (W <= 0 || H <= 0) return {};
    int dx = (int)std::lround((double)(s.x() - kPad) / W * 255.0);
    int dy = (int)std::lround((double)(kPad + H - s.y()) / H * 255.0);
    return { std::clamp(dx, 0, 255), std::clamp(dy, 0, 255) };
}

int CurveEditor::findNearby(const QPoint& screen) const
{
    const auto& pts = curPts();
    for (int i = 0; i < pts.size(); ++i) {
        const QPoint sc = dataToScreen(pts[i].first, pts[i].second);
        if (std::abs(sc.x() - screen.x()) <= kPickR &&
            std::abs(sc.y() - screen.y()) <= kPickR) {
            return i;
        }
    }
    return -1;
}

void CurveEditor::sortPts()
{
    auto& pts = curPts();
    std::sort(pts.begin(), pts.end(),
              [](const QPair<int,int>& a, const QPair<int,int>& b){
                  return a.first < b.first;
              });
}

void CurveEditor::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // 背景
    p.fillRect(rect(), QColor(40, 40, 40));

    const int W = width()  - 2 * kPad;
    const int H = height() - 2 * kPad;
    if (W <= 0 || H <= 0) return;

    // 网格 (8 段)
    for (int i = 0; i <= 8; ++i) {
        int x = kPad + W * i / 8;
        int y = kPad + H * i / 8;
        if (i == 4) p.setPen(QPen(QColor(100, 100, 100), 1));
        else        p.setPen(QPen(QColor(70, 70, 70), 1));
        p.drawLine(x, kPad, x, kPad + H);
        p.drawLine(kPad, y, kPad + W, y);
    }

    // 对角参考线
    p.setPen(QPen(QColor(60, 60, 60, 180), 1, Qt::DashLine));
    p.drawLine(dataToScreen(0, 0), dataToScreen(255, 255));

    // 画一条曲线 (lut 256 点)
    auto drawOne = [&](const CurveParams::Pts& pts, QColor color, int width) {
        std::array<uint8_t, 256> lut{};
        CurveSolver::buildSingleLut(pts, lut);
        QPen pen(color, width);
        p.setPen(pen);
        QPainterPath path;
        for (int x = 0; x < 256; ++x) {
            QPoint sp = dataToScreen(x, lut[x]);
            if (x == 0) path.moveTo(sp);
            else        path.lineTo(sp);
        }
        p.drawPath(path);
    };

    // 1) 先画"非当前"通道, 半透明 + 细线
    auto otherColor = [](int ch) {
        QColor c = channelColor(ch);
        c.setAlpha(150);   // 60%
        return c;
    };
    if (m_channel != 0) drawOne(m_curves.master, otherColor(0), 1);
    if (m_channel != 1) drawOne(m_curves.r,      otherColor(1), 1);
    if (m_channel != 2) drawOne(m_curves.g,      otherColor(2), 1);
    if (m_channel != 3) drawOne(m_curves.b,      otherColor(3), 1);

    // 2) 当前通道在最上面, 高亮粗线
    drawOne(curPts(), channelColor(m_channel), 2);

    // 控制点 (只画当前通道)
    p.setPen(QPen(Qt::black, 1));
    p.setBrush(channelColor(m_channel));
    for (const auto& pt : curPts()) {
        QPoint sp = dataToScreen(pt.first, pt.second);
        p.drawEllipse(sp, kHandleR, kHandleR);
    }
}

void CurveEditor::mousePressEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton) {
        int idx = findNearby(e->pos());
        if (idx < 0) {
            // 加点
            QPoint d = screenToData(e->pos());
            // 端点 0/255 不要重复加
            auto& pts = curPts();
            if (d.x() <= 0 || d.x() >= 255) {
                m_dragIdx = -1;
                return;
            }
            pts.push_back({ d.x(), d.y() });
            sortPts();
            // 找新加的索引
            for (int i = 0; i < pts.size(); ++i) {
                if (pts[i].first == d.x() && pts[i].second == d.y()) { m_dragIdx = i; break; }
            }
            update();
            emit curvesChanged();
        } else {
            m_dragIdx = idx;
        }
        e->accept();
        return;
    }
    if (e->button() == Qt::RightButton) {
        int idx = findNearby(e->pos());
        if (idx > 0 && idx < curPts().size() - 1) {   // 端点不可删
            curPts().removeAt(idx);
            update();
            emit curvesChanged();
        }
        e->accept();
        return;
    }
    QWidget::mousePressEvent(e);
}

void CurveEditor::mouseMoveEvent(QMouseEvent* e)
{
    if (m_dragIdx < 0) return;
    auto& pts = curPts();
    if (m_dragIdx >= pts.size()) { m_dragIdx = -1; return; }

    QPoint d = screenToData(e->pos());
    // 端点 x 锁
    if (m_dragIdx == 0) d.setX(0);
    else if (m_dragIdx == pts.size() - 1) d.setX(255);
    else {
        // 中间点 x 不能越过相邻
        int prevX = pts[m_dragIdx - 1].first + 1;
        int nextX = pts[m_dragIdx + 1].first - 1;
        d.setX(std::clamp(d.x(), prevX, nextX));
    }
    pts[m_dragIdx] = { d.x(), d.y() };
    update();
    emit curvesChanged();
    e->accept();
}

void CurveEditor::mouseReleaseEvent(QMouseEvent*)
{
    m_dragIdx = -1;
}

} // namespace HighPro
