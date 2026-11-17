#pragma once
#ifndef INC_OSD_CIRCLE_H_
#define INC_OSD_CIRCLE_H_
#include "ui/osd.h"
#include "utils/qt_utils.h"
#include "utils/log.h"
#include <QPainter>

// 画圆
struct EXPORTED OsdCircle : public Osd {
    OsdCircle();
    OsdCircle(const OsdCircle &right);

    DataProperty<cv::Point2f> center_; // 中心
    DataProperty<float>       radian_; // 角度
    DataProperty<double>      radius_; // 半径
    DataProperty<float>       width_;  // 线宽

    Cs cs_;              // TODO：换成统一的坐标系统
};
using OsdAreaCircle = OsdCircle; // 兼容2022/06/24前写法

struct EXPORTED UiOsdCircle : public UiOsd {
    Q_OBJECT
public:
    UiOsdCircle(const std::shared_ptr<Osd> &osd);
    ~UiOsdCircle();
    bool isOnObject(const QPointF &pos, Qt::CursorShape &mouseDirection) override;
    void paint(QPainter &qp, bool isDrawHotSpot) override;

private:
    friend struct EXPORTED OsdCircle;
    class Impl;
    std::unique_ptr<Impl> impl_;
};

#endif
