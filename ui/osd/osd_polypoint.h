#ifndef INC_OSD_POLYPOINT_H_
#define INC_OSD_POLYPOINT_H_
#pragma once

#include "ui/osd.h"
#include "types/attitude.h"
#include <opencv2/core.hpp>
#include <QPainter>
#include <QColor>

// 封闭多边形
struct EXPORTED OsdPolyPoint : public Osd {
    OsdPolyPoint();
    OsdPolyPoint(const std::string &osdType);
    OsdPolyPoint(const OsdPolyPoint &right);

    VList<cv::Point2f>       points_;   // 顶点
    DataProperty<float>      radian_;   // 相对于第一个点的旋转角度
    DataProperty<float>      width_;    // 线宽
    DataProperty<bool>       showAngle_;// 是否加粗显示角
};

struct EXPORTED OsdPolyGon : public OsdPolyPoint {
    OsdPolyGon();
    OsdPolyGon(const OsdPolyGon &right);
};

// 不封闭多边形
struct EXPORTED OsdPolyLine : public OsdPolyPoint {
    OsdPolyLine();
    OsdPolyLine(const OsdPolyLine &right);
};


struct EXPORTED UiOsdPolyPoint : public UiOsd {
    Q_OBJECT
public:
    UiOsdPolyPoint(const std::shared_ptr<Osd> &osd);
    ~UiOsdPolyPoint();
    bool isOnObject(const QPointF &pos, Qt::CursorShape &mouseDirection) override;
    void paint(QPainter &qp, bool isDrawHotSpot) override;

private:
    friend struct EXPORTED OsdPolyPoint;
    class Impl;
    std::unique_ptr<Impl> impl_;
};

#endif

