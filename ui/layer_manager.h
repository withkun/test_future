#pragma once
#ifndef INC_LAYER_MANAGER_H_
#define INC_LAYER_MANAGER_H_

#include "utils/lib_export.h"
#include "layer_tool.h"
#include "types/vset.h"
#include "utils/misc.h"
#include "color_manager.h"
#include <QWidget>
#include <QStatusBar>

class ImageController;

// 图像叠加层，roi, osd等
class EXPORTED LayerManager
    : public QObject
    , public std::enable_shared_from_this<LayerManager> {
    Q_OBJECT

public:
    LayerManager();
    LayerManager(LayerManager &) = delete;
    ~LayerManager();

    void initialize(QWidget *widget);                       // layer管理器初始化
    void initialize(QPainter *qp, const QSize &imageSize);  // layer管理器初始化

    void close();
    void paint(QPainter &qp, bool isDrawHotSpot);
    void paint(const VMap<std::shared_ptr<UiLayerTool>> &layerTools_, QPainter &qp, bool isDrawHotSpot);
    void update();  // 更新画图对象  

    void runInitialize(UiLayerTool *layerTool); // 运行UiLayerTool初始状态机
    void runEdit(UiLayerTool *layerTool);       // 运行UiLayerTool编辑状态机

    QWidget *widget() const;           // 获取所属窗口（widget_ 和 painter_ 二选一）
    QPainter *painter() const;         // 获取绘图对象（widget_ 和 painter_ 二选一）

    std::shared_ptr<Image> getImage() const;     // 获取所属窗口当前的图片
    QSizeF imageSize() const;                    // 图像分辨率
    double imageZoom() const;                    // 图像放大倍数
    QPointF imageZoomCenter() const;             // 图像放大区域中心在整体图像的位置（比例 0 - 1）
    QSizeF widgetSize() const;                   // 绘图区域的大小

    // 窗口坐标转成图像坐标
    QPointF widgetToImage(const QPointF &pos) const;
    // 图像坐标转成窗口坐标
    QPointF imageToWidget(const QPointF &pos) const;
    // 窗口坐标转成图像坐标
    QLineF widgetToImage(const QLineF &pos) const;
    // 图像坐标转成窗口坐标
    QLineF imageToWidget(const QLineF &pos) const;
    // 窗口坐标转成图像坐标
    QSizeF widgetToImage(const QSizeF &size) const;
    // 图像坐标转成窗口坐标
    QSizeF imageToWidget(const QSizeF &size) const;
    // 窗口坐标转成图像坐标
    QRectF widgetToImage(const QRectF &rect) const;
    // 图像坐标转成窗口坐标
    QRectF imageToWidget(const QRectF &rect) const;
    // 窗口距离转成图像距离
    double widgetToImage(double size) const;
    // 图像距离转成窗口距离
    double imageToWidget(double size) const;

    bool isEditable() const;           // 是否可编辑窗口
    void setEditable(bool isEditable); // 是否可编辑窗口

    // 判断层对象是否在本管理器中（遍历，效率慢）
    bool isValid(const UiLayerTool *layerTool) const;
    // 移除层对象
    size_t remove(const UiLayerTool *layerTool);
    // 移除所有同名的层
    size_t remove(const std::string &layerName);
    // 移除所有同名的层（仅layerToolSet中的）
    size_t remove(const std::string &layerName, const std::set<std::shared_ptr<LayerTool>> &layerToolSet);
    // 移除所有同前缀的层
    size_t removeByLayerPrefix(const std::string &layerNamePrefix);
    // 移除所有层
    size_t removeAllLayerTools();
    // 返回所有层
    VMap<std::shared_ptr<UiLayerTool>> &layerTools();

    // 判断层是否被禁止显示
    bool isLayerDisabled(const std::string &layerName) const;
    // 判断层对象是否被禁止显示
    bool isLayerToolDisabled(const UiLayerTool *layerTool) const;

    // 某个UiLayerTool对象等待某事件
    promise::Promise waitEvent(UiLayerTool *layerTool, QEvent::Type  eventType);
    // 监听一堆的事件
    // 事件格式按照如下排列：
    //    对象1，事件...，处理函数...
    //    对象2，事件...，处理函数...
    promise::Promise listenEvents(
        const Loc &loc,
        const std::initializer_list<boost::variant<UiLayerTool *, QObject *, QEvent::Type, std::function<void(QEvent *event, const promise::Defer &defer)>>> &eventOrCallbacks);
    template<typename ...EventOrCallback, typename std::enable_if<(sizeof...(EventOrCallback) > 1)>::type *dummy = nullptr>
    promise::Promise listenEvents(const Loc &loc, EventOrCallback ...eventOrCallbacks) {
        return listenEvents(loc, { eventOrCallbacks... });
    }

    // 清除初始任务
    void clearInitialingTask();

    // 设定某个layerTool组件选中
    void setFocusedLayerTool(UiLayerTool *layerTool);
    // 获取选定的layerTool组件
    UiLayerTool *getFocusedLayerTool() const;
    // 判断是否被某个组件选中
    bool isFocusedOnLayerTool() const;

    // 通知位置移动  
    void notifyMove(UiLayerTool *layerTool);

    // 隐藏的层名列表，例如：full_image, normal_roi, label_roi, ref_roi, osd
    VSet<std::string> hiddenLayers_;
    //DataProperty<bool> isHideNormalRoi_; // layerName = "normal_roi"
    //DataProperty<bool> isHideRefRoi_;    // layerName = "ref_roi"
    //DataProperty<bool> isHideLabelRoi_;  // layerName = "label_roi"
    //DataProperty<bool> isHideOsd_;       // layerName = "osd"

    // 是否显示OSD标签  
    DataProperty<bool> isOsdLabelVisible_;

    // 设置使能更新界面
    void enableUpdateGui() const;
    // 设置禁止更新界面
    void disableUpdateGui() const;
    // 设置禁止更新界面（返回值析构时自动使能）
    OnScopeExit scopedDisableUpdateGui() const;
    // 是否禁止了更新界面
    bool isUpdateGuiDisabled() const;

    // 获取颜色管理器
    ColorManager &colorManager();
    // color无效时，根据addressTag分配颜色（index为任意值，但需要保障对同一个layerTool的不同颜色变量能否加以区分）
    QColor allocateColor(UiLayerTool *layerTool, const QColor &color, int index);

    // 设置颜色值缓存器
    void setColorCache(const std::shared_ptr<ColorCache> &colorCache);

    // 获取对应的状态显示栏
    QStatusBar *statusBar();
    // 获取对应的image controller
    ImageController *imageController();

    // 设置不显示层
    void setHiddenLayer(const VStrSet& hiddenlayers);
     
signals:
    // 取得焦点
    void focusIn(UiLayerTool *layerTool);
    // 移动层工具  
    void moveLayer(UiLayerTool *layerTool);

private:
    friend class UiLayerTool;
    class Impl;
    std::unique_ptr<Impl> impl_;
    bool eventFilter(UiLayerTool *layerTool, QEvent *event);
};


// 参数中任一任务状态达成时，reject第二个参数promise2nd中的所有任务
void raceAndReject2nd(const promise::DeferLoop &loop, const std::initializer_list<promise::Promise> &promise2nd);


#endif
