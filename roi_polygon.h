#pragma once
#ifndef INC_ROI_POLYGON_H_
#define INC_ROI_POLYGON_H_

#include "ui/roi.h"
#include "promise-cpp/add_ons/qt/promise_qt.hpp"
#include <QPointF>

struct RoiPolygon : Roi {
    // roiType: AiPolygon  封闭连接线
    RoiPolygon(const std::string &roiType);
    RoiPolygon(const RoiPolygon &right);

    cv::RotatedRect minAreaRect() const override; // 获取最小外接矩形

    // 获取roi区域的图像（超出图像范围的部分，使用patchBgra填充）
    // tryInPlace: true 输出图像尽量在原图上构建; false 不影响原图
    // isMarginTransparent: roi区域外的设置为透明区域
    // 返回值: 裁剪后的图像和实际旋转区域
    std::pair<cv::Mat, cv::RotatedRect> roiImage(const cv::Mat &inputImage, const cv::Scalar &patchBgra, bool tryInPlace, bool isMarginTransparent) const override;

    // 获取姿态
    Attitude getAttitude() const override;

    bool contains(const cv::Point2f &point) const override; // 判断点是否在Roi区域之内

    // 从绑定的姿态位置的区域（从this Roi对象），转成相对于图像的区域
    std::shared_ptr<Roi> attitudeToImage(const Attitude *attitude) const override;
    // 从相对于图像的区域，转到绑定的姿态位置的区域
    std::shared_ptr<Roi> imageToAttitude(const Attitude *attitude) const override;
    // 解绑姿态变量（从相对于姿态的坐标，转成图像坐标）
    void detachAttitude(const Attitude *attitude) override;
    // 绑定姿态变量（从图像坐标，转成相对于姿态的坐标）
    void attachAttitude(const Attitude *attitude) override;

    void paint(UiRoi &uiRoi, QPainter &qp, bool isFirstInGroup, bool isDrawHotSpot) override;
    void paint(UiRoi &uiRoi, QPainter &qp, bool isFirstInGroup, bool isDrawHotSpot, const std::shared_ptr<QPointF> &movingPoint);
    
    // 具体Roi对象比较
    bool equals(const Roi& other) const override;

    VList<cv::Point2f>        points_; // 所有顶点
};

struct UiRoiPolygon : UiRoi {
    Q_OBJECT
public:
    UiRoiPolygon(const std::string &roiType, const std::shared_ptr<Roi> &roi);
    ~UiRoiPolygon() override;
    bool isOnObject(const QPointF &pos, Qt::CursorShape &mouseDirection) override;
    void paint(QPainter &qp, bool isDrawHotSpot) override;

    // 运行状态机
    promise::Promise runInitialize() override;                               // 初始状态，自由绘制
    promise::Promise runEdit() override;                                     // 绘制完可编辑状态
    promise::Promise runMove(const QPointF &mousePos);                       // 移动状态
    promise::Promise runResize(const QPointF &mousePos, size_t cornerIndex); // 尺寸拉伸状态
    promise::Promise runCornerContextMenu(const QPointF &mousePos,           // 点击连接点时的弹出菜单处理
                                          size_t cornerIndex);
    promise::Promise runBorderContextMenu(const QPointF &mousePos,           // 点击边框的弹出菜单处理
                                          size_t startIndex,
                                          size_t endIndex);

    // 数据get/set
    void setPoint(size_t offset, const QPointF &point);
    void insertPoint(size_t offset, const QPointF &point);
    void erasePoint(size_t offset);
    void clearPoints();

private:
    friend struct RoiPolygon;
    class Impl;
    std::unique_ptr<Impl> impl_;
};
#endif // INC_ROI_POLYGON_H_