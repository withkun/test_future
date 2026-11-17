#pragma once
#ifndef INC_OSD_H_
#define INC_OSD_H_
#include "utils/lib_export.h"
#include "osd_area.h"
#include "layer_tool.h"
#include "layer_manager.h"
#include <memory>
//#include <opencv2/viz/types.hpp>
#include <opencv2/core/types.hpp>
#include "utils/type_category.h"

// 坐标计算
#define OSD_DEFAULT_VISIABLE_RECT   QRectF(10, 10, 20, 20)    //（默认的）主体区域（不包括拖拉组件）相对父窗口的位置和大小
#define OSD_DEFAULT_LINE_LENGTH     40                        // 默认的线长
#define OSD_DEFAULT_LINE_WIDTH      2                         // 线宽
//#define OSD_DEFAULT_COLOR           QColor(20, 25, 255, 230)  // 默认颜色
#define OSD_DEFAULT_TEXT_SIZE       10                        // 默认文本大小

struct OsdFactory {
    std::string osdType_;
    std::string localOsdType_;
    //QString iconFile_;
    std::function<std::shared_ptr<Osd>()> createOsd_;
    std::function<std::shared_ptr<UiOsd>(const std::shared_ptr<Osd> &osd)> createUiOsd_;

};


class EXPORTED UiOsd : public UiLayerTool {
public:
    static const int DEFAULT_LINE_WIDTH = 2;
    static const int DEFAULT_CIRCLE_RADIUS = 5;
    static const int DEFAULT_ARROW_SIZE = 10;

    UiOsd(std::shared_ptr<Osd> area);
    UiOsd(const UiOsd &) = delete;
    virtual ~UiOsd();

    // 获取所有OSD区域的构建器
    static void registerOsd(
        const std::string &osdType,
        const std::string &localOsdType,
        const std::function<std::shared_ptr<Osd>()> &createOsd,
        const std::function<std::shared_ptr<UiOsd>(const std::shared_ptr<Osd> &osd)> &createUiOsd);
    static const std::vector<OsdFactory> &getOsdFactory();
    static const OsdFactory *getOsdFactory(const std::string &osdType);

    // 新建层对象
    static std::shared_ptr<UiOsd> newInstanceBy(const std::shared_ptr<LayerManager> &layerManager,
        const OsdFactory &osdFactory,
        const std::shared_ptr<LayerTool> &osd,
        const std::string &layerName,
        bool isReadonly);
    static std::shared_ptr<UiOsd> newInstance(const std::shared_ptr<LayerManager> &layerManager,
        const std::string &osdType,
        const std::shared_ptr<LayerTool> &osd,
        const std::string &layerName,
        bool isReadonly);

    // 判断点是否落在本对象（感兴趣的区域）上面
    bool isOnObject(const QPointF &pos, Qt::CursorShape &mouseDirection) override;
    // 初始绘制状态
    promise::Promise runInitialize() override;
    // 绘制完可修改编辑状态
    promise::Promise runEdit() override;
    // 进入右键菜单
    promise::Promise runContextMenu(const QPointF &mousePos, const std::function<void(QMenu *, const FnAddMenu &)> &onMenuCreated) override;

    // 绘制函数，使用 UiLayerTool::paint
    std::shared_ptr<Osd> getData() const;

    std::string               osdType_;    // osd类型
    bool                      isReadonly_; // 是否为只读
};


template<typename OSD, typename UI_OSD>
inline void registerOsd(
    const std::string &osdType,
    const std::string &localOsdType) {
    UiOsd::registerOsd(osdType, localOsdType,
        []() { return std::make_shared<OSD>(); },
        [](const std::shared_ptr<Osd> &osd) {
        return std::dynamic_pointer_cast<UiOsd>(std::make_shared<UI_OSD>(osd));
    });
    tcRegister<OSD>({ TypeCategory::kVisiable });
    anypro::registration::class_<OSD>()
        .template base<Osd>();
}


#endif
