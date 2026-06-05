#pragma once

#include "core/ColorEffect.h"

#include <QWidget>
#include <QVector>
#include <QPair>

namespace HighPro {

// 256×256 自绘曲线编辑器.
//   - 4 通道切换 (master / R / G / B), 每通道独立 Pts.
//   - 左键: 空白处加点 / 已有点上拖动 / 同点附近移动
//   - 右键: 删除最近点 (端点不可删)
//   - 拖动会限制在 [0..255] 区间, 端点 x 锁定 (0/255)
//   - 实时调 onChanged 回调反馈, 由 EffectPanel 同步到 EffectStack.
//
// 视觉:
//   - 8×8 网格灰线 (中线深一点)
//   - 当前曲线粗线 (颜色按通道: master=白, R=红, G=绿, B=蓝)
//   - 控制点小圆点
class CurveEditor : public QWidget
{
    Q_OBJECT
public:
    explicit CurveEditor(QWidget* parent = nullptr);

    void setCurves(const CurveParams& cp);
    CurveParams curves() const { return m_curves; }

    int channel() const { return m_channel; }   // 0=master, 1=R, 2=G, 3=B

public slots:
    void setChannel(int ch);
    void resetCurrent();          // 当前通道重置为对角线
    void resetAll();              // 全部 4 通道重置

signals:
    void curvesChanged();
    // 鼠标释放 / 重置按钮 / 通道切换 — 用于 dialog 把"会话起点快照"提交到 undo 栈,
    // 与 SliderRow 的 commit 语义一致, 避免一次拖动产生 N 个 undo 步.
    void editingFinished();

protected:
    void paintEvent(QPaintEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void leaveEvent(QEvent* e) override;
    QSize sizeHint() const override { return QSize(256, 256); }
    QSize minimumSizeHint() const override { return QSize(180, 180); }

private:
    CurveParams::Pts& curPts();
    const CurveParams::Pts& curPts() const;

    QPoint dataToScreen(int x, int y) const;       // (0..255, 0..255) → widget 像素
    QPoint screenToData(const QPoint& s) const;
    int    findNearby(const QPoint& screen) const; // 返回 idx, -1=无
    void   sortPts();

    CurveParams m_curves;
    int         m_channel = 0;     // 0=master,1=R,2=G,3=B

    int         m_dragIdx  = -1;
    int         m_hoverIdx = -1;            // 鼠标悬停命中的控制点 (-1=无)
    QPoint      m_hoverPos{ -1, -1 };       // 鼠标当前位置 (widget 坐标), -1=离开
};

} // namespace HighPro
