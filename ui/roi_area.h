#pragma once
#ifndef INC_ROI_AREA_H_
#define INC_ROI_AREA_H_
#include <memory>
#include <functional>
#include <atomic>
#include <QPainter>
#include <vector>
#include <string>
#include "layer_tool.h"
#include "types/vset.h"
#include "types/attitude.h"
#include "boost/variant.hpp"

class EXPORTED UiRoi;
class EXPORTED Widget;

// ROI 区域定义
struct EXPORTED Roi :
    public LayerTool {

    // 常用的 ROI 类型定义，更多的用户类型，使用字符串扩展不同的类型
    enum RoiType {
        kCustom,                    // 用户定义的类型
        kFreeDraw,                  // 自由绘制
        kRotatedRect,               // 旋转矩形
        kRect,                      // 正交矩形
        kCircle,                    // 圆型
        kEllipse,                   // 椭圆
        kLabel,                     // 标签区域
        kLine,                      // 直线型
    };

    enum RoiCategory {
        kInternal,  // 内部管理用的roi
        kImageArea, // 图像区域
        kLabelArea  // 标签区域
    };


    Roi(const Roi &);
    Roi(const std::string &roiType, RoiCategory roiCategory);
    virtual ~Roi();

    // 层工具的类名字
    std::string toolClassName() const override;

    // 创建
    static std::shared_ptr<Roi> newInstance(const std::string &roiType);
    std::shared_ptr<Roi> clone();

    // 枚举类型转为字符串类型
    static std::string roiTypeName(const RoiType &roiType);

    // 从绑定的姿态位置的区域（从this Roi对象），转成相对于图像的区域
    virtual std::shared_ptr<Roi> attitudeToImage(const Attitude *attitude) const;
    // 从相对于图像的区域，转到绑定的姿态位置的区域
    virtual std::shared_ptr<Roi> imageToAttitude(const Attitude *attitude) const;
    // 解绑姿态变量（从相对于姿态的坐标，转成图像坐标）
    virtual void detachAttitude(const Attitude *attitude);
    // 绑定姿态变量（从图像坐标，转成相对于姿态的坐标）
    virtual void attachAttitude(const Attitude *attitude);


    // 获取所有经姿态扩展的Roi列表
    std::vector<std::shared_ptr<Roi>> getExtRoiByAttitude() const;
    // 是否有参考姿态变量，并且姿态数组为空
    bool hasEmptyAttitudes() const;

    virtual cv::RotatedRect minAreaRect() const; // 获取最小外接矩形

    // 获取roi区域的图像（超出图像范围的部分，使用patchBgra填充）  
    // tryInPlace: true 输出图像尽量在原图上构建; false 不影响原图  
    // isMarginTransparent: roi区域外的设置为透明区域  
    // 返回值: 裁剪后的图像和实际旋转区域  
    virtual std::pair<cv::Mat, cv::RotatedRect> roiImage(const cv::Mat &inputImage, const cv::Scalar &patchBgra, bool tryInPlace, bool isMarginTransparent) const;

    // 获取姿态  
    virtual Attitude getAttitude() const;
    // 获取中心
    virtual cv::Point2f getCenter() const;

    virtual bool contains(const cv::Point2f &point) const; // 判断点是否在Roi区域之内
    virtual void copyFrom(const Roi &roi);

    virtual void paint(UiRoi &uiRoi, QPainter &qp, bool isFirstInGroup/*是否为同组中首个*/, bool isDrawHotSpot/*是否画热点*/);
    
    // roi基类对象比较
    virtual bool equalsBase(const Roi& other) const;
    // 具体Roi对象比较
    virtual bool equals(const Roi& other) const = 0;

    // 比较两个Roi对象是否相等
    bool operator==(const Roi& other) const;

    std::string                 roiType_;        // roi类型
    RoiCategory                 roiCategory_;    // roi分类
    DataProperty<std::string>   text_;           // 标签文字

    std::weak_ptr<Widget>       widget_;         // roi所属widget
    DataProperty<bool>          isExtByAttitude_;// 是否经由姿态扩展（必须结合widget_不为空时才能有效）
    std::weak_ptr<Roi>          roi0_;           // 最初由哪个roi扩展而来（可能为自身）
    std::string                 uuid_;           // 唯一ID（仅roi0有必要）

    std::shared_ptr<VariantMap> cfgTable_;       // 参数表
    //bool                        isInitialized_;  // 区域是否已经初始赋值
};

// Roi类型定义
struct RoiSpec {
    boost::variant<std::string, Roi::RoiType> roiType_;  // Roi类型
    size_t minNum_ = 0;                                  // 允许的最小个数
    size_t maxNum_ = std::numeric_limits<size_t>::max(); // 允许的最大个数
};

// 分组的 ROI 区域
struct EXPORTED RoiGroup {
    RoiGroup();
    RoiGroup(const RoiGroup &);
    RoiGroup(RoiGroup &&);
    ~RoiGroup();
    RoiGroup &operator=(const RoiGroup &);
    std::vector<std::shared_ptr<Roi>> rois_;  // Roi列表
    std::shared_ptr<Attitude> attitude_;      // 对应的姿态变量（可能未nullptr）

    ///< 获取分组的姿态id（格式为: "nid,A,B,C"，nid为数字的标签序号，A,B,C为所有命中的标签）
    std::string getAttitudeId(size_t groupIndex /* 分组下标 */) const;
    ///< 获取roi id（格式为: "rid,text"，rid为roi数组下标，text为roi文本）
    std::string getRoiId(const std::shared_ptr<Roi> &roi) const;
    ///< 获取roi的数组下标（未找到返回 rois_.size()）
    size_t getRoiIndex(const std::shared_ptr<Roi> &roi) const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

///< 判断Roi分组是否为空
bool EXPORTED isEmpty(const std::vector<RoiGroup> &roiGroups);

#endif // INC_ROI_AREA_H_
