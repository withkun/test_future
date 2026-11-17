#ifndef INC_OSD_RECTANGLE_H_
#define INC_OSD_RECTANGLE_H_
#pragma once

#include "ui/osd.h"
#include "utils/qt_utils.h"
#include "utils/log.h"
#include <QPainter>

// 画矩形
struct EXPORTED OsdRectangle : public Osd {
    OsdRectangle();
    OsdRectangle(const OsdRectangle &right);

    DataProperty<double>      radian_;      // 旋转角度（弧度）
    DataProperty<cv::Point2f> center_;      // 中心位置（相对于父窗口的位置。组件旋转中心，不计算拖拉组件）
    DataProperty<cv::Size2f>  size_;        // 大小
    DataProperty<float>       width_;       // 线宽
    DataProperty<bool>        isShowGrid_;  // 是否显示中间格子线和方向箭头
};

struct EXPORTED UiOsdRectangle : public UiOsd {
    Q_OBJECT
public:
    UiOsdRectangle(const std::shared_ptr<Osd> &osd);
    ~UiOsdRectangle();
    bool isOnObject(const QPointF &pos, Qt::CursorShape &mouseDirection) override;
    void paint(QPainter &qp, bool isDrawHotSpot) override;

private:
    friend struct EXPORTED OsdRectangle;
    class Impl;
    std::unique_ptr<Impl> impl_;
};

#endif

