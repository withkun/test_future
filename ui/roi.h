#pragma once
#ifndef INC_ROI_H_
#define INC_ROI_H_
#include <memory>
#include <functional>
#include <atomic>
#include <QPainter>
#include "types/vmap.h"
#include "types/vset.h"
#include "types/vlist.h"
#include "roi_area.h"
#include "layer_tool.h"

class  LayerManager;
struct UiRoiFullImage;

#ifdef HAVE_QT

#include <QObject>
#include <QWidget>
#include <QPainterPath>
#include "nlohmann/json.hpp"
#include "utils/data_bind.h"
#include "layer_tool.h"
#include "promise-cpp/add_ons/qt/promise_qt.hpp"
#include "utils/lib_export.h"


// 坐标计算
#define ROI_DEFAULT_VISIABLE_RECT   QRectF(10, 10, 20, 20)    //（默认的）主体区域（不包括拖拉组件）相对父窗口的位置和大小
#define ROI_DEFAULT_MIN_SIZE        20                        // 默认的最小尺寸

// 窗口坐标计算
#define ROI_LINE_WIDTH_THINE        0.8                         // 细线宽
#define ROI_LINE_WIDTH              2                           // 线宽
#define ROI_MASK_MARGIN             8                           // 不规则窗口掩膜额外宽度
#define ROI_HOTSPOT_RADIUS          3                           // 热点半径
#define ROI_HOTSPOT_DISTANCE        8                           // 热点距离
#define ROI_ARROW_LEN               14                          // 箭头长度

// 普通ROI区域颜色
//#define ROI_FOCUS_COLOR   QColor(255, 60, 25, 230)
//#define ROI_UNFOCUS_COLOR QColor(200, 80, 25, 230)
//#define ROI_HOTSPOT_COLOR QColor(255, 20, 25, 230)
//#define ROI_CROSS_COLOR   QColor(0xFF, 0x88, 0xB0)

// 标签区域颜色
#define ROI_LABEL_MARGIN        5
#define ROI_LABEL_FONT_MAX_SIZE 14
#define ROI_LABEL_FONT_MIN_SIZE 7
//#define ROI_LABEL_FONT_COLOR    QColor(40, 180, 25, 230)
//#define ROI_LABEL_FOCUS_COLOR   QColor(40, 200, 25, 230)
//#define ROI_LABEL_HOTSPOT_COLOR QColor(15, 160, 25, 230)
//#define ROI_LABEL_CROSS_COLOR   QColor(0x88, 0xFF, 0xB0)

struct RoiFactory {
    std::string roiType_;
    QString name_;
    QString iconFile_;
    std::function<std::shared_ptr<Roi>()> createArea_;
    std::function<std::shared_ptr<UiRoi>(const std::shared_ptr<Roi> &roi)> createRoi_;
};

class EXPORTED UiRoi
    : public UiLayerTool {
public:
    UiRoi(std::shared_ptr<Roi> area);
    UiRoi(const std::string &roiType, std::shared_ptr<Roi> area);
    UiRoi(UiRoi &) = delete;
    virtual ~UiRoi();

    // 获取所有ROI区域的构建器
    static const std::vector<RoiFactory> &getRoiFactory();
    static const RoiFactory *getRoiFactory(Roi::RoiType roiType);
    static const RoiFactory *getRoiFactory(const std::string &roiType);

    // 获取圈定范围区域的构建器
    static const std::vector<RoiFactory> &getRoiRangeFactory();
    // 获取标签区域的构建器
    static const std::vector<RoiFactory> &getRoiLabelFactory();
    // 获取OSD对象的构建器
    //static const std::vector<OsdFactory> &getOsdFactory();

    // 新建层对象
    static std::shared_ptr<UiRoi> newInstanceBy(const std::shared_ptr<LayerManager> &layerManager,
                                              const RoiFactory &roiFactory,
                                              const std::shared_ptr<LayerTool> &roi,
                                              const std::string &layerName,
                                              bool isRefRoi,
                                              bool isReadonly);
    static std::shared_ptr<UiRoi> newInstance(const std::shared_ptr<LayerManager> &layerManager,
                                            const std::string &roiType,
                                            const std::shared_ptr<LayerTool> &roi,
                                            const std::string &layerName,
                                            bool isRefRoi,
                                            bool isReadonly);

    promise::Promise runContextMenu(const QPointF &mousePos, const std::function<void(QMenu *, const FnAddMenu &)> &onMenuCreated) override;
    virtual promise::Promise runModifyText(const QPointF &mousePos);
    std::shared_ptr<Roi> getData() const;

    std::string               roiType_;    // roi类型
    //bool                      isReadonly_; // 是否为只读
    //bool                      isRefRoi_;   // 是否为引用roi

protected:
    void newRelativeVarBrowser(QMenu *menuRelativeTo);
};


#endif // HAVE_QT
#endif // INC_ROI_H_
