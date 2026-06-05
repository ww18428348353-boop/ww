#include "CurveEditor.h"
#include "core/CurveSolver.h"

#include <QPainter>
#include <QMouseEvent>
#include <QPainterPath>
#include <QFontMetrics>
#include <algorithm>

namespace HighPro {

namespace {
constexpr int kPad     = 8;
constexpr int kHandleR = 5;        // 控制点半径 (像素) — 比 4 稍大, 拖拽手感更稳
constexpr int kPickR   = 10;       // 命中半径

QColor channelColor(int ch)
{
    // 通道颜色 (按用户要求):
    //   RGB(master) = #b9b9b9 浅灰
    //   R           = #a41010 深红
    //   G           = #0fa80f 翠绿
    //   B           = #14148c 深蓝
    switch (ch) {
    case 1: return QColor(0xa4, 0x10, 0x10);
    case 2: return QColor(0x0f, 0xa8, 0x0f);
    case 3: return QColor(0x14, 0x14, 0x8c);
    default: return QColor(0xb9, 0xb9, 0xb9);
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
    emit editingFinished();
}

void CurveEditor::resetAll()
{
    m_curves.reset();
    update();
    emit curvesChanged();
    emit editingFinished();
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
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);

    // 背景
    p.fillRect(rect(), QColor(40, 40, 40));

    const int W = width()  - 2 * kPad;
    const int H = height() - 2 * kPad;
    if (W <= 0 || H <= 0) return;

    // 边框 (内嵌一像素, AA 时不糊)
    p.setPen(QPen(QColor(60, 60, 60), 1));
    p.setBrush(Qt::NoBrush);
    p.drawRect(QRectF(kPad + 0.5, kPad + 0.5, W - 1, H - 1));

    // 网格 (8 段) — 中线略深, 其他更细更暗
    for (int i = 0; i <= 8; ++i) {
        int x = kPad + W * i / 8;
        int y = kPad + H * i / 8;
        if (i == 4)        p.setPen(QPen(QColor(95, 95, 95), 1));
        else if (i == 0 || i == 8) p.setPen(QPen(QColor(60, 60, 60), 1));
        else               p.setPen(QPen(QColor(64, 64, 64), 1));
        p.drawLine(x, kPad, x, kPad + H);
        p.drawLine(kPad, y, kPad + W, y);
    }

    // 对角参考线
    p.setPen(QPen(QColor(70, 70, 70, 180), 1, Qt::DashLine));
    p.drawLine(dataToScreen(0, 0), dataToScreen(255, 255));

    // 画一条曲线 (lut 256 点)
    auto drawOne = [&](const CurveParams::Pts& pts, QColor color, qreal width) {
        std::array<uint8_t, 256> lut{};
        CurveSolver::buildSingleLut(pts, lut);
        QPen pen(color, width);
        pen.setCapStyle(Qt::RoundCap);
        pen.setJoinStyle(Qt::RoundJoin);
        p.setPen(pen);
        QPainterPath path;
        for (int x = 0; x < 256; ++x) {
            QPoint sp = dataToScreen(x, lut[x]);
            if (x == 0) path.moveTo(sp);
            else        path.lineTo(sp);
        }
        p.drawPath(path);
    };

    // 1) 先画"非当前"通道, 半透明 + 细线; 暗色通道(B)略提 alpha 保留可见
    auto otherColor = [](int ch) {
        QColor c = channelColor(ch);
        // B 通道 (#14148c) 太暗, 在深底+低 alpha 下看不见, 单独提亮
        c.setAlpha(ch == 3 ? 200 : 150);
        return c;
    };
    if (m_channel != 0) drawOne(m_curves.master, otherColor(0), 1.0);
    if (m_channel != 1) drawOne(m_curves.r,      otherColor(1), 1.0);
    if (m_channel != 2) drawOne(m_curves.g,      otherColor(2), 1.0);
    if (m_channel != 3) drawOne(m_curves.b,      otherColor(3), 1.0);

    // 2) 当前通道在最上面, 高亮粗线
    drawOne(curPts(), channelColor(m_channel), 2.0);

    // 3) 控制点 (只画当前通道) — 大圆 + 通道色填充 + 白边, hover/drag 加发光环
    const QColor chColor = channelColor(m_channel);
    const QColor chColorOnDark = (m_channel == 3)
        ? QColor(0x66, 0x66, 0xc8)             // 蓝色描点本体太暗, 用浅蓝替代填充
        : chColor;
    const auto& pts = curPts();
    for (int i = 0; i < pts.size(); ++i) {
        const QPoint sp = dataToScreen(pts[i].first, pts[i].second);
        const bool active = (i == m_dragIdx) || (m_dragIdx < 0 && i == m_hoverIdx);
        if (active) {
            // 半透明发光环
            QColor halo = chColorOnDark; halo.setAlpha(70);
            p.setPen(Qt::NoPen);
            p.setBrush(halo);
            p.drawEllipse(sp, kHandleR + 4, kHandleR + 4);
        }
        // 外圈白边
        p.setPen(QPen(QColor(245, 245, 245), 1.2));
        p.setBrush(chColorOnDark);
        p.drawEllipse(sp, kHandleR, kHandleR);
        // 中心暗芯
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(20, 20, 20));
        p.drawEllipse(sp, 1, 1);
    }

    // 4) 坐标读数: 拖动中 / hover 命中点时, 在右上角显示 (x → y)
    int readIdx = (m_dragIdx >= 0) ? m_dragIdx : m_hoverIdx;
    if (readIdx >= 0 && readIdx < pts.size()) {
        const QString txt = QStringLiteral("%1 → %2")
                                .arg(pts[readIdx].first).arg(pts[readIdx].second);
        QFont f = p.font();
        f.setPointSizeF(f.pointSizeF() > 0 ? f.pointSizeF() : 9.0);
        p.setFont(f);
        const QFontMetrics fm(f);
        const int tw = fm.horizontalAdvance(txt) + 10;
        const int th = fm.height() + 4;
        const QRect tagR(kPad + W - tw - 4, kPad + 4, tw, th);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 170));
        p.drawRoundedRect(tagR, 3, 3);
        p.setPen(chColorOnDark);
        p.drawText(tagR, Qt::AlignCenter, txt);
    }

    // 5) 角标 0/255 — 帮助识别坐标范围
    {
        QFont f = p.font();
        f.setPointSizeF(f.pointSizeF() > 0 ? f.pointSizeF() : 8.0);
        p.setFont(f);
        p.setPen(QColor(110, 110, 110));
        p.drawText(QRect(kPad + 2, kPad + H - 14, 30, 12), Qt::AlignLeft | Qt::AlignVCenter,
                   QStringLiteral("0"));
        p.drawText(QRect(kPad + W - 32, kPad + H - 14, 30, 12), Qt::AlignRight | Qt::AlignVCenter,
                   QStringLiteral("255"));
        p.drawText(QRect(kPad + 2, kPad + 2, 30, 12), Qt::AlignLeft | Qt::AlignTop,
                   QStringLiteral("255"));
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
            emit editingFinished();
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
            emit editingFinished();
        }
        e->accept();
        return;
    }
    QWidget::mousePressEvent(e);
}

void CurveEditor::mouseMoveEvent(QMouseEvent* e)
{
    m_hoverPos = e->pos();
    if (m_dragIdx < 0) {
        // 非拖拽: 只更新 hover 命中点 + 触发重绘 (用于发光环 / 坐标提示)
        const int hi = findNearby(e->pos());
        if (hi != m_hoverIdx) {
            m_hoverIdx = hi;
            update();
        }
        return;
    }

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
    m_hoverIdx = m_dragIdx;
    update();
    emit curvesChanged();
    e->accept();
}

void CurveEditor::mouseReleaseEvent(QMouseEvent*)
{
    const bool wasDragging = (m_dragIdx >= 0);
    m_dragIdx = -1;
    if (wasDragging) {
        update();           // 清拖拽态发光环
        emit editingFinished();
    }
}

void CurveEditor::leaveEvent(QEvent*)
{
    if (m_dragIdx < 0 && (m_hoverIdx != -1 || m_hoverPos != QPoint(-1, -1))) {
        m_hoverIdx = -1;
        m_hoverPos = QPoint(-1, -1);
        update();
    }
}

} // namespace HighPro
