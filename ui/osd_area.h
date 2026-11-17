#pragma once
#ifndef INC_OSD_AREA_H_
#define INC_OSD_AREA_H_
#include "utils/lib_export.h"
#include "utils/data_bind.h"
#include "types/vset.h"
#include "types/attitude.h"
#include "layer_tool.h"
#include <memory>
#include <string>
//#include <opencv2/viz/types.hpp>
#include <opencv2/core/types.hpp>

class EXPORTED UiOsd;

// Osd 区域定义
struct EXPORTED Osd :
    public LayerTool {

    Osd(const Osd &);
    Osd(const std::string &osdType);
    virtual ~Osd();
    
    // 层工具的类名字
    std::string toolClassName() const override;

    // 创建
    static std::shared_ptr<Osd> newInstance(const std::string &osdType);
    std::shared_ptr<Osd> clone();

    // 从绑定的姿态位置的区域（从this Roi对象），转成相对于图像的区域
    virtual std::shared_ptr<Osd> attitudeToImage(const std::shared_ptr<Osd> &roi, Attitude *attitude) const;
    // 从相对于图像的区域，转到绑定的姿态位置的区域（存于this Roi对象）
    virtual std::shared_ptr<Osd> imageToAttitude(const std::shared_ptr<Osd> &roi, Attitude *attitude) const;

    virtual cv::RotatedRect minAreaRect(); // 获取最小外接矩形
    virtual bool contains(const cv::Point2f &point) const; // 判断点是否在Roi区域之内
    virtual void copyFrom(const Osd &roi);


    // 注册可视类型
    static void registerConvertor(const anypro::type &type, const std::function<std::shared_ptr<Osd>(const anypro::any &from)> &convertor);
    template<typename T>
    static void registerConvertor(const std::function<std::shared_ptr<Osd>(const anypro::any &from)> &convertor) {
        registerConvertor(anypro::type::get<T>(), convertor);
    }
    // 根据注册可是类型创建可视对象
    static std::shared_ptr<Osd> convertFrom(const anypro::any &from);


    // 增加附着显示层（线程安全）
    static void appendOsd(VList<std::shared_ptr<Osd>> &osds, const std::shared_ptr<Osd> &osd);
    // 增加附着显示层
    static void appendOsd(VList<std::shared_ptr<Osd>> &osds, const anypro::any &visiableObject);
    // 增加附着显示层（线程安全）
    static void appendOsd(VMap<std::shared_ptr<Osd>> &osdsEx, const std::string &name, const std::shared_ptr<Osd> &osd);
    // 增加附着显示层
    static void appendOsd(VMap<std::shared_ptr<Osd>> &osdsEx, const std::string &name, const anypro::any &visiableObject);

    std::string                 osdType_;        // osd类型
    DataProperty<std::string>   text_;           // 标签文字
    DataProperty<std::string>   labels_;         // 附加标签，用于保存所在数组下标等，一般和text_合并显示

    // osd区域可绑定到“姿态(Attitude)”
    // 当osd绑定了姿态变量时，角度和位置关系相对于姿态变量
    // 当osd未绑定姿态变量时，角度和位置关系相对于整张图
    //std::weak_ptr<VStrSet>      boundVars_;      // roi绑定的姿态变量

    std::shared_ptr<VariantMap> cfgTable_;       // 参数表
    //bool                        isInitialized_;  // 区域是否已经初始赋值
    //bool                        isFirst_;        // 是否同数组osd中的首个osd
};

#endif
