#pragma once
#ifndef INC_LAYER_TOOL_H_
#define INC_LAYER_TOOL_H_

#include "utils/lib_export.h"
#include "promise-cpp/add_ons/qt/promise_qt.hpp"
#include "utils/data_bind.h"
#include "types/vset.h"
#include "types/image.h"
#include <future>
#include <typeindex>
#define _USE_MATH_DEFINES
#include <math.h>
#include <QWidget>
#include <QStatusBar>
#include <QMenu>

class LayerManager;
class ImageController;

class EXPORTED LayerTool
    : public std::enable_shared_from_this<LayerTool>
    , public BindingData {
public:
    LayerTool();
    virtual ~LayerTool() {}

    enum Cs {
        kUseImageCs = 0, // 使用图像坐标
        kUseGuiCs = 1,   // 使用窗口坐标
    };

    int  zIndex_;          // 层顺序（越大越上层）
    bool isFirstInArray_;  // 是否同数组中的首个层对象
    bool isInitialized_;   // 是否初始化做好
    bool isRef_;           // 是否为引用对象
    DataProperty<QColor> color_;  // 主要颜色，无效时自动选择颜色，参考QColor::isValid()

    using LayerTools = std::list<std::shared_ptr<LayerTool>>;
    static LayerTools listByVarName(const VStrSet &varNames);

    // 层工具的类名字
    virtual std::string toolClassName() const = 0;

    // 获取颜色属性
    virtual DataProperty<QColor> &getColorProperty(int index);
};

class EXPORTED UiLayerTool
    : public std::enable_shared_from_this<UiLayerTool>
    , public QObject
    , public BindingData {
public:

    // Roi对象应由 LayerManager::newInstanceBy() 或 LayerManager::newInstance() 构建
    UiLayerTool(std::shared_ptr<LayerTool> data);
    UiLayerTool(UiLayerTool &) = delete;
    virtual ~UiLayerTool();
    virtual void close();

    // 设定所属的层管理器
    std::shared_ptr<LayerManager> layerManager() const;
    void setLayerManager(std::weak_ptr<LayerManager> layerManager);

    // 将自己从 LayerManager 管理器中移除
    void remove();
    // 检测在 LayerManager 管理器中是否有效
    bool isValid() const;

    std::string                 layerName_;    // 可视对象所属层
    std::weak_ptr<LayerManager> layerManager_; // 可视对象层管理器
    std::shared_ptr<LayerTool>  data_;         // 数据对象
    bool isFocused_;       // 是否为焦点对象
    bool isReadonly_;      // 是否为只读

    // 判断点是否落在本对象（感兴趣的区域）上面
    virtual bool isOnObject(const QPointF &pos, Qt::CursorShape &mouseDirection) = 0;

    // 绘制函数
    virtual void paint(QPainter &qp, bool isDrawHotSpot) = 0;

    virtual void update();  // 更新画图对象

    // 分配颜色（color无效时，自动分配；color有效时，返回color）
    QColor allocateColor(const QColor &color, int index = 0);

    // 运行状态机
    //virtual promise::Promise runInitialize() = 0; // 初始绘制状态
    //virtual promise::Promise runEdit() = 0;       // 绘制完可修改编辑状态

    virtual std::future<int> runInitialize() = 0;   // 初始绘制状态
    virtual std::future<int> runEdit() = 0;         // 绘制完可修改编辑状态

    virtual std::future<int> runMove(const QPointF &mousePos) = 0;    // 移动状态
    virtual std::future<int> runResize(const QPointF &mousePos, size_t cornerIndex) = 0;    // 尺寸拉伸状态
    virtual std::future<int> runCornerContextMenu(const QPointF &mousePos, size_t cornerIndex) = 0;    // 点击连接点弹出菜单处理
    virtual std::future<int> runBorderContextMenu(const QPointF &mousePos, size_t startIndex, size_t endIndex) = 0;    // 点击边框弹出菜单处理

    // 默认的右键菜单
    using FnAddMenu = std::function<void(QAction *, const std::function<promise::Promise()> &)>;
    virtual promise::Promise runContextMenu(const QPointF &mousePos, const std::function<void(QMenu *, const FnAddMenu &)> &onMenuCreated) = 0; // 进入右键菜单
    virtual promise::Promise runSetColor(const QPointF &mousePos, int index = 0);
    virtual promise::Promise runDispText(const QStringList &texts);
    virtual void setAutoColor(int index = 0);
    // 获取本对象的颜色组的名字，默认为空，则使用唯一的颜色组
    virtual std::vector<std::pair<std::string, int>> getColorNames() const;
    // 覆盖一个窗体并显示关闭，该窗体鼠标移动即退出（一个workaround）
    promise::Promise runWorkaroundMaskCursor(const QCursor &cursor);

    QWidget *widget() const;                     // 获取所属窗口（注意该窗口不是ImageController，是其中的图像窗口）
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


    // 一些通用的绘制办法
    // 画旋转柄
    void drawRotateHandler(QPainter &qp,
        const QPointF &rotateCenter,
        const QColor &color,
        bool isSetPen = true, /* 是否需要设置画笔 */
        double radian = - M_PI / 4);
    // 画箭头线
    void drawArrow(QPainter &qp,
        const QPointF &start,
        const QPointF &end,
        const QColor &color,
        float arrowSize = 8, /* 箭头大小 */
        float lineWidth = 2, /* 线宽 */
        bool isSetPen = true /* 是否需要设置画笔 */);
    // 画空心文字
    void drawHollowFont(QPainter &qp,
        const QFont &font,
        const QString &text,
        const QRect &textRect,
        const QColor &textColor,
        const QColor &borderColor = QColor());

    // 统一样式：设定在主线宽比例下，根据不同条件的显示修改（isFirstInArray_，isRef_ ）
    float getLineWidth(double width);
    void setPenStyle(QPen *pen, const QColor &color, double width);

    // 获取对应的状态显示栏
    QStatusBar *statusBar();
    // 状态栏位显示文字（支持富文本）
    void showStatusMessage(const QString &message, int timeoutMs);
protected:
    promise::Promise         pendingTask_;   // 当前处理的任务
};

#endif