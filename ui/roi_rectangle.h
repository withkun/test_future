#pragma once
#ifndef INC_ROI_RECTANGLE_H_
#define INC_ROI_RECTANGLE_H_

#include "../roi.h"
#include "promise-cpp/add_ons/qt/promise_qt.hpp"
#include <QCursor>
#include <QPointF>

// 矩形 ROI 区域定义
struct EXPORTED RoiRectangle : public Roi {
    // roiType: straight_rectangle 正交矩形
    //          rectangle          可旋转的矩形
    RoiRectangle(const std::string &roiType);
    RoiRectangle(const RoiRectangle &right);

    cv::RotatedRect minAreaRect() const override; // 获取最小外接矩形  

    // 获取roi区域的图像（超出图像范围的部分，使用patchBgra填充）  
    // tryInPlace: true 输出图像尽量在原图上构建; false 不影响原图  
    // isMarginTransparent: roi区域外的设置为透明区域  
    // 返回值: 裁剪后的图像和实际旋转区域  
    std::pair<cv::Mat, cv::RotatedRect> roiImage(const cv::Mat &inputImage, const cv::Scalar &patchBgra, bool tryInPlace, bool isMarginTransparent) const override;

    // 获取姿态  
    Attitude getAttitude() const override;
    // 获取中心
    cv::Point2f getCenter() const override;

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

    // 具体Roi对象比较
    bool equals(const Roi& other) const override;

    DataProperty<double>      radian_;      // 旋转角度（弧度）
    DataProperty<cv::Point2f> center_;      // 中心位置（相对于父窗口的位置。组件旋转中心，不计算拖拉组件）
    DataProperty<cv::Size2f>  size_;        // 大小
    DataProperty<bool>        useScanLine_; // 是否使用扫描线
};

struct UiRoiRectangle : public UiRoi {
    Q_OBJECT
public:
    UiRoiRectangle(const std::string &roiType, const std::shared_ptr<Roi> &roi);
    virtual ~UiRoiRectangle();
    virtual bool isOnObject(const QPointF &pos, Qt::CursorShape &mouseDirection) override;
    virtual void paint(QPainter &qp, bool isDrawHotSpot) override;

    // 运行状态机
    promise::Promise runInitialize() override;         // 初始状态，自由绘制
    promise::Promise runEdit() override;               // 绘制完可编辑状态
    promise::Promise runRotate();                      // 旋转状态
    promise::Promise runMove();                        // 移动状态
    promise::Promise runResize(Qt::WindowFrameSection edge, const Qt::CursorShape &mouseDirection);   // 尺寸拉伸状态	
    promise::Promise runContextMenu(const QPointF &mousePos, const std::function<void(QMenu *, const FnAddMenu &)> &onMenuCreated) override; // 右键菜单

private:
    friend struct EXPORTED RoiRectangle;
    class Impl;
    std::unique_ptr<Impl> impl_;
};


#endif

