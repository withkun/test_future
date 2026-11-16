#include "ui/layer_manager.h"
#include "types/vlist.h"
#include "utils/log.h"
#include "ui/roi/roi_fullimage.h"
#include "promise-cpp/add_ons/qt/promise_qt.hpp"
#include <QMainWindow>
#include "ui/widget_dialog.h"

using namespace promise;

#define PM_WEAK_LISTENER

class LayerManager::Impl {
public:
    Impl(LayerManager *thiz)
        : thiz_(thiz)
        , updateCount_(0)
        , disableUpdateGui_(false) {
    }

    LayerManager *thiz_;

    void setImageController();

    // 分发到某个UiLayerTool对象的回调函数支持
    struct Listener {
        std::function<bool(UiLayerTool *, QEvent *)> cb_;
        using Listeners = std::multimap<std::pair<UiLayerTool *, QEvent::Type>, std::shared_ptr<Listener>>;
        Listeners::iterator self_;
    };
    
    std::weak_ptr<Listener> addEventListener(UiLayerTool *layerTool, QEvent::Type eventType, const std::function<bool(UiLayerTool *, QEvent *)> &func);
    void removeEventListener(std::weak_ptr<Listener> listener);

    //void clearLoopTask();

    // 各UiLayerTool组件的事件监听器
    Listener::Listeners listeners_;

#ifndef PM_WEAK_LISTENER
    // UiLayerTool事件监听器的延后删除缓存
    struct ListenersIteratorCompare {
        bool operator()(const std::weak_ptr<Listener> &left, const std::weak_ptr<Listener> &right) const {
            return left.owner_before(right);
        }
    };
    std::set<std::weak_ptr<Listener>, ListenersIteratorCompare> removeLaters_;

    // eventFilter 嵌套层级
    std::atomic<int> eventFilterRecursiveCount_;
#endif

    // 从全局事件中截取
    using GlobalListenerItr = std::weak_ptr<PromiseEventListener>;
    GlobalListenerItr listenerHoverMove_;
    GlobalListenerItr listenerMouseMove_;
    GlobalListenerItr listenerMouseButtonPress_;
    GlobalListenerItr listenerMouseButtonRelease_;
    GlobalListenerItr listneerMouseButtonDblClick_;

    VMap<std::shared_ptr<UiLayerTool>> layerTools_;  // UiLayerTool列表(key为层名，允许重复)

    promise::Promise initializingTask_;              // 正在初始绘制中的UiLayerTool任务
    std::shared_ptr<UiLayerTool> focusedLayerTool_;  // 当前获取焦点（并且处理中的）UiLayerTool

    QWidget *widget_;                                // 关联的窗口对象（widget_ 和 painter_ 二选一）
    QPainter *painter_;                              // 关联的绘图对象（widget_ 和 painter_ 二选一）
    ImageController *imageController_;               // widget_的父ImageController窗口（仅选widget_时有效）  
    QSize     imageSize_;                            // 当选用painter_时，指定原始图像的大小（以便和OSD/ROI坐标对齐）
    QSizeF    painterSize_;                          // 当选用painter_时，绘图区域的大小

    int updateCount_;
    std::chrono::steady_clock::time_point updateTime_;    // 上一次更新时间  
    QExTimerHandler deferUpdateTimer_;                    // 延迟更新  

    bool isEditable_;                                // 是否可编辑窗口
    std::atomic<bool> disableUpdateGui_;             // 是否禁止更新关联的窗口对象


    // 即将删除的对象
    std::map<std::shared_ptr<UiLayerTool>, VMap<std::shared_ptr<UiLayerTool>>::iterator> aboutToRemoves_;
    std::atomic<bool> isRemoving_;
    size_t completeRemove(bool isRemoving);

    // 关闭所有层  
    void closeAllLayers();

    // 颜色分配器
    ColorManager colorManager_;
};


std::weak_ptr<LayerManager::Impl::Listener> LayerManager::Impl::addEventListener(UiLayerTool *layerTool, QEvent::Type eventType, const std::function<bool(UiLayerTool *, QEvent *)> &func) {
    std::pair<UiLayerTool *, QEvent::Type> key = { layerTool, eventType };
    std::shared_ptr<Listener> listener = std::make_shared<Listener>();
    auto itr = listeners_.insert({ key, listener });
    listener->self_ = itr;
    listener->cb_ = func;

    return listener;
}

void LayerManager::Impl::removeEventListener(std::weak_ptr<LayerManager::Impl::Listener> listener) {
#ifndef PM_WEAK_LISTENER
    removeLaters_.insert(listener);
#else
    auto sListener = listener.lock();
    if (sListener)
        listeners_.erase(sListener->self_);
#endif
}

bool LayerManager::eventFilter(UiLayerTool *layerTool, QEvent *event) {

#ifndef PM_WEAK_LISTENER
    impl_->eventFilterRecursiveCount_.fetch_add(1);

    std::pair<UiLayerTool *, QEvent::Type> key = { layerTool, event->type() };

    // may not safe if one handler is removed by other ? 
    std::list<Impl::Listener::Listeners::iterator> itrs;
    for (Impl::Listener::Listeners::iterator itr = impl_->listeners_.lower_bound(key);
        itr != impl_->listeners_.end() && key == itr->first; ++itr) {
        itrs.push_back(itr);
    }
    for (Impl::Listener::Listeners::iterator itr : itrs) {
        if (!impl_->focusedLayerTool_ || impl_->focusedLayerTool_.get() == itr->first.first) {
            itr->second->cb_(layerTool, event);
        }
    }

    int value = impl_->eventFilterRecursiveCount_.fetch_sub(1);

    if (value <= 1) {
        for (const std::weak_ptr<Impl::Listener> &listener : impl_->removeLaters_) {
            auto sListener = listener.lock();
            if(sListener)
                impl_->listeners_.erase(sListener->self_);
        }
        impl_->removeLaters_.clear();
    }

#else

    std::pair<UiLayerTool *, QEvent::Type> key = { layerTool, event->type() };

    // may not safe if one handler is removed by other ? 
    std::list<std::weak_ptr<Impl::Listener>> listeners;
    for (Impl::Listener::Listeners::iterator itr = impl_->listeners_.lower_bound(key);
        itr != impl_->listeners_.end() && key == itr->first; ++itr) {

        if (!impl_->focusedLayerTool_ || impl_->focusedLayerTool_.get() == itr->first.first) {
            listeners.push_back(itr->second);
        }
    }
    for (const std::weak_ptr<Impl::Listener> &listener: listeners) {
        auto sListener = listener.lock();
        if (sListener) {
            sListener->cb_(layerTool, event);
        }
    }

#endif

    return false;
}

LayerManager::LayerManager()
    : impl_(new Impl(this)) {
    impl_->focusedLayerTool_ = nullptr;
    impl_->widget_ = nullptr;
    impl_->painter_ = nullptr;
    impl_->imageController_ = nullptr;
    impl_->isEditable_ = true;
#ifndef PM_WEAK_LISTENER
    impl_->eventFilterRecursiveCount_ = 0;
#endif
    impl_->isRemoving_ = false;

    auto updateWidget = [=]() {
        if (!impl_->disableUpdateGui_ && impl_->widget_) {
            this->update();
        }
    };
    hiddenLayers_.onChanged(updateWidget);

    isOsdLabelVisible_ = true;
}

LayerManager::~LayerManager() {
    close();
    //clearLoopTask();

    // 先保存到临时变量，再清除 layerTools_
    // 避免内部的layerTool析构时，再次调用到layerTools_结构
    LOG_INFO("this = {} {}", this, impl_->layerTools_.size());
    auto layerTools = impl_->layerTools_;
    impl_->layerTools_.clear();
    layerTools.clear();

    PromiseEventFilter &filter = PromiseEventFilter::getSingleInstance();
    filter.removeEventListener(impl_->listneerMouseButtonDblClick_);
    filter.removeEventListener(impl_->listenerMouseButtonRelease_);
    filter.removeEventListener(impl_->listenerMouseButtonPress_);
    filter.removeEventListener(impl_->listenerMouseMove_);
    filter.removeEventListener(impl_->listenerHoverMove_);
}

void LayerManager::initialize(QWidget *widget) {
    if (impl_->widget_ != nullptr) return; //already initialized
    impl_->widget_ = widget;
    impl_->setImageController();

#if 0
    auto getMatchedRoi = [=](const QPointF &pos) {

        //TODO：暂时没有实现层的z-index关系，全屏幕区域层，最后处理
        std::list<std::shared_ptr<UiLayerTool>> fullImageRois;

        for (auto roi : impl_->rois_) {
            if (std::dynamic_pointer_cast<UiRoiFullImage>(roi)) {
                fullImageRois.push_back(roi);
            }
            else {
                Qt::CursorShape mouseDirection;
                if (roi->isOnObject(roi->widgetToImage(pos), mouseDirection)) {
                    return roi;
                }
            }
        }

        for (auto roi : fullImageRois) {
            Qt::CursorShape mouseDirection;
            if (roi->isOnObject(roi->widgetToImage(pos), mouseDirection)) {
                return roi;
            }
        }

        return std::shared_ptr<UiLayerTool>(nullptr);
    };

    PTI;
    baseLoopTask_ = doWhile(PM_LOC, [=](DeferLoop loop) {
        PTD;
        // 鼠标移动，根据位置设置鼠标形状
        auto onMoving = [=](const any &arg) {
            QEvent *event = arg.cast<QEvent *>();
            const QPointF &pos = getMousePos(event);
            std::shared_ptr<UiLayerTool> roi = getMatchedRoi(pos);
            if (!roi) {
                impl_->widget_->setCursor(Qt::ArrowCursor);
            }

            return loop.doContinue(PM_LOC); // will goto doWhile
        };

        // 鼠标点击，根据位置判断进入其他状态
        auto waitMouseButtonPress = promise::waitEvent(impl_->widget_, QEvent::MouseButtonPress).then([=](const any &arg) {
            QEvent *event = arg.cast<QEvent *>();
            const QPointF &pos = getMousePos(event);
            if (!focusedLayerTool_) {
                std::shared_ptr<UiLayerTool> roi = getMatchedRoi(pos);
                if (!roi && roiFullImage_) {
                    // 处理图像移动事件
                    this->setFocusedRoi(roiFullImage_.get());
                    PTD;
                    roiFullImage_->runMove(pos).finally(PM_LOC, [=] {
                        PTD;
                        this->setFocusedRoi(nullptr);
                        PTD;
                    }).then(loop);
                    return;
                }
            }

            return loop.doContinue(PM_LOC); // will goto doWhile
        });

        auto waitHoverMove = promise::waitEvent(impl_->widget_, QEvent::HoverMove).then(onMoving);
        auto waitMouseMove = promise::waitEvent(impl_->widget_, QEvent::MouseMove).then(onMoving);


        // 以上几个事件只处理其中任一个
        raceAndReject2nd(loop.getPromise(), { waitHoverMove, waitMouseMove, waitMouseButtonPress });
    });
#endif

    auto listenerFunc = [=](QObject *object, QEvent *event) {
        PTD;
        if (false && impl_->focusedLayerTool_ != nullptr) {
            // 因为UiLayerTool会改变 focusedLayerTool_ 状态，所以此处不做统一处理
            this->eventFilter(impl_->focusedLayerTool_.get(), event);
        }
        else {
            VMap<std::shared_ptr<UiLayerTool>> layerTools = impl_->layerTools_;
            for (const auto &item : layerTools) {
                auto layerTool = item.second;
                if (!std::dynamic_pointer_cast<UiRoiFullImage>(layerTool)) {
                    this->eventFilter(layerTool.get(), event);
                }
            }
            for (const auto &item : layerTools) {
                auto layerTool = item.second;
                if (std::dynamic_pointer_cast<UiRoiFullImage>(layerTool)) {
                    this->eventFilter(layerTool.get(), event);
                }
            }
        }
        return false;
    };
    PTD;
    PromiseEventFilter &filter = PromiseEventFilter::getSingleInstance();
    impl_->listenerHoverMove_ = filter.addEventListener(impl_->widget_, QEvent::HoverMove, listenerFunc);
    impl_->listenerMouseMove_ = filter.addEventListener(impl_->widget_, QEvent::MouseMove, listenerFunc);
    impl_->listenerMouseButtonPress_ = filter.addEventListener(impl_->widget_, QEvent::MouseButtonPress, listenerFunc);
    impl_->listenerMouseButtonRelease_ = filter.addEventListener(impl_->widget_, QEvent::MouseButtonRelease, listenerFunc);
    impl_->listneerMouseButtonDblClick_ = filter.addEventListener(impl_->widget_, QEvent::MouseButtonDblClick, listenerFunc);
}

void LayerManager::initialize(QPainter *painter, const QSize &imageSize) {
    if (impl_->painter_ != nullptr) return; //already initialized
    impl_->painter_ = painter;
    impl_->imageSize_ = imageSize;

    if (impl_->painter_ != nullptr) {
        impl_->painterSize_ = impl_->painter_->clipBoundingRect().size();
        LOG_DEBUG("imageSize = {}, cropSize = {}", imageSize, impl_->painter_->clipBoundingRect().size());
    }
}

void LayerManager::close() {
    impl_->disableUpdateGui_ = true;
    qClearTimeout(impl_->deferUpdateTimer_);

    impl_->closeAllLayers();
}

void LayerManager::Impl::closeAllLayers() {
    auto layerTools = this->layerTools_;
    for (auto layerTool : layerTools) {
        layerTool.second->close();
    }
}

void LayerManager::paint(QPainter &qp, bool isDrawHotSpot) {
    this->paint(impl_->layerTools_, qp, isDrawHotSpot);
}

void LayerManager::paint(const VMap<std::shared_ptr<UiLayerTool>> &layerTools, QPainter &qp, bool isDrawHotSpot) {
    for (const auto &item : layerTools) {
        auto layerTool = item.second;
        if (!isLayerToolDisabled(layerTool.get())) {
            qp.setBrush(Qt::NoBrush);
            qp.setPen(Qt::NoPen);
            layerTool->paint(qp, isDrawHotSpot);
        }
    }
}

void LayerManager::update() {
    if (!this->isUpdateGuiDisabled() && impl_->widget_) {
        static const int MIN_UPDATE_TIME_MS = (1000 / 24);   // 最高更新不超过24帧  

#if 1
        impl_->widget_->update();
#elif 0
        if (impl_->deferUpdateTimer_.isValid()) {
            ++impl_->updateCount_;
            LOG_WARN("defer upate = {}", impl_->updateCount_);
        }
        else {
            impl_->updateCount_ = 0;
            impl_->deferUpdateTimer_ = qSetTimeout(LOC, [this](const Loc &) {
                impl_->widget_->update();
                if (impl_->updateCount_ > 0) {
                    this->update();
                }
            }, MIN_UPDATE_TIME_MS);
        }
#else
        auto now = std::chrono::steady_clock::now();
        auto updateInterval = now - impl_->updateTime_;
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(updateInterval).count();

        qClearTimeout(impl_->deferUpdateTimer_);
        if (ms < MIN_UPDATE_TIME_MS) {
            LOG_WARN("defer upate = {}", MIN_UPDATE_TIME_MS - ms);
            impl_->deferUpdateTimer_ = qSetTimeout(LOC, [this, now](const Loc &) {
                impl_->updateTime_ = std::chrono::steady_clock::now();
                impl_->widget_->update();
            }, MIN_UPDATE_TIME_MS - ms);
        }
        else {
            impl_->updateTime_ = now;
            impl_->widget_->update();
        }
#endif
    }
}


// Wait event will wait the event for only once
Promise LayerManager::waitEvent(UiLayerTool *layerTool,
    QEvent::Type eventType) {
    auto listeners = std::make_shared<std::vector<std::weak_ptr<Impl::Listener>>>();
    Promise promise = newPromise(PM_LOC, [this, listeners, layerTool, eventType](const Defer &cb) {
        listeners->push_back(impl_->addEventListener(
            layerTool, eventType, [cb](UiLayerTool *layerTool, QEvent *event) {
            (void)layerTool;
            //if (event->type() == QEvent::Destroy
            //    || event->type() == QEvent::Close) {
            //    cb.reject(PM_LOC);
            //    return false;
            //}
            // The next then function will be call immediately
            // Be care that do not use event in the next event loop
            //else {
                cb.resolve(PM_LOC, event);
                return false;
            //}
        }));

        if (eventType != QEvent::Destroy) {
            listeners->push_back(impl_->addEventListener(
                layerTool, QEvent::Destroy, [cb](UiLayerTool *layerTool, QEvent *event) {
                (void)layerTool;
                (void)event;
                cb.reject(PM_LOC);
                return false;
            }));
        }

        if (eventType != QEvent::Close) {
            listeners->push_back(impl_->addEventListener(
                layerTool, QEvent::Close, [cb](UiLayerTool *layerTool, QEvent *event) {
                (void)layerTool;
                (void)event;
                cb.reject(PM_LOC);
                return false;
            }));
        }

    }).finally(PM_LOC, [this, listeners]() {
        for (const auto &listener : *listeners) {
            impl_->removeEventListener(listener);
        }
    });

    return promise;
}


promise::Promise LayerManager::listenEvents(
    const Loc &loc,
    const std::initializer_list<boost::variant<UiLayerTool *, QObject *, QEvent::Type, std::function<void(QEvent *event, const promise::Defer &defer)>>> &eventOrCallbacks) {

    struct Callback {
        std::vector<QEvent::Type> events_;
        std::function<void(QEvent *event, const Defer &defer)> cb_;
    };

    struct ObjCallback {
        UiLayerTool *layerTool_{ nullptr };
        QObject *object_{ nullptr };
        std::list<Callback> callbacks_;
    };
    std::shared_ptr<std::list<ObjCallback>> objCallbacks = std::make_shared<std::list<ObjCallback>>();

    for (const auto &item : eventOrCallbacks) {
        if (item.type() == typeid(UiLayerTool *)) {
            objCallbacks->emplace_back();
            objCallbacks->back().layerTool_ = boost::get<UiLayerTool *>(item);
            objCallbacks->back().callbacks_.emplace_back();
        }
        else if (item.type() == typeid(QObject *)) {
            objCallbacks->emplace_back();
            objCallbacks->back().object_ = boost::get<QObject *>(item);
            objCallbacks->back().callbacks_.emplace_back();
        }
        else if (item.type() == typeid(QEvent::Type)) {
            if (objCallbacks->size() == 0)
                throw std::runtime_error("no object");
            
            const auto &event = boost::get<QEvent::Type>(item);
            objCallbacks->back().callbacks_.back().events_.push_back(event);
        }
        else if (item.type() == typeid(std::function<void(QEvent * event, const Defer & defer)>)) {
            if (objCallbacks->size() == 0)
                throw std::runtime_error("no object");
            auto &callback = objCallbacks->back().callbacks_.back();
            if (callback.cb_)
                throw std::runtime_error("duplicated callback");

            const auto &cb = boost::get<std::function<void(QEvent *event, const Defer &defer)>>(item);
            callback.cb_ = cb;
            objCallbacks->back().callbacks_.emplace_back();
        }
    }

    std::shared_ptr<std::vector<std::weak_ptr<LayerManager::Impl::Listener>>> layerToolListeners
        = std::make_shared<std::vector<std::weak_ptr<LayerManager::Impl::Listener>>>();
    std::shared_ptr<std::vector<std::weak_ptr<PromiseEventListener>>> qObjectListeners
        = std::make_shared<std::vector<std::weak_ptr<PromiseEventListener>>>();

    return promise::newPromise(PM_LOC, [=](const promise::Defer &defer) {
        for (const auto &objCallback : *objCallbacks) {
            for (const auto &callback : objCallback.callbacks_) {
                if (callback.cb_ && callback.events_.size() > 0) {
                    auto cb = callback.cb_;
                    for (const QEvent::Type &eventType : callback.events_) {
                        if (objCallback.layerTool_ != nullptr) {
                            layerToolListeners->push_back(impl_->addEventListener(
                                objCallback.layerTool_, eventType, [cb, defer](UiLayerTool *layerTool, QEvent *event) {
                                (void)layerTool;
                                cb(event, defer);
                                return false;
                            }));
                        }
                        else if (objCallback.object_ != nullptr) {
                            qObjectListeners->push_back(promise::addEventListener(
                                objCallback.object_, eventType, [cb, defer](QObject *object, QEvent *event) {
                                (void)object;
                                cb(event, defer);
                                return false;
                            }));
                        }
                    }
                }
            }
        }
    }).finally(PM_LOC, [=] {
        for (const auto &listener : *layerToolListeners) {
            impl_->removeEventListener(listener);
        }
        for (const auto &listener : *qObjectListeners) {
            promise::removeEventListener(listener);
        }
    });
}



void LayerManager::clearInitialingTask() {
    impl_->initializingTask_.clear();
}

// 设定某个layerTool组件选中
void LayerManager::setFocusedLayerTool(UiLayerTool *layerTool) {
    if (layerTool == nullptr)
        impl_->focusedLayerTool_ = nullptr;
    else
        impl_->focusedLayerTool_ = layerTool->shared_from_this();;
    //this->update();

    LOG_DEBUG("setFocusedlayerTool = {}\n", layerTool);
    if (layerTool != nullptr) {
        emit this->focusIn(layerTool);
    }
}

UiLayerTool *LayerManager::getFocusedLayerTool() const {
    return impl_->focusedLayerTool_.get();
}

bool LayerManager::isFocusedOnLayerTool() const {
    return impl_->focusedLayerTool_ != nullptr;
}

// 通知位置移动  
void LayerManager::notifyMove(UiLayerTool *layerTool) {
    emit this->moveLayer(layerTool);
}

void LayerManager::enableUpdateGui() const {
    impl_->disableUpdateGui_ = false;
}

void LayerManager::disableUpdateGui() const {
    impl_->disableUpdateGui_ = true;
}

OnScopeExit LayerManager::scopedDisableUpdateGui() const {
    disableUpdateGui();
    return OnScopeExit([this] {
        enableUpdateGui();
    });
}

// 是否禁止了更新界面
bool LayerManager::isUpdateGuiDisabled() const {
    return impl_->disableUpdateGui_;
}

// 获取颜色管理器
ColorManager &LayerManager::colorManager() {
    return impl_->colorManager_;
}



// color无效时，根据addressTag分配颜色（addressTag为任意地址数据，但需要保障对同一个layerTool的不同颜色变量能否加以区分）
QColor LayerManager::allocateColor(UiLayerTool *layerTool, const QColor &color, int index) {
    return impl_->colorManager_.allocateColor(color, layerTool->layerName_, layerTool->data_->toolClassName(), index);
}

// 设置颜色值缓存器
void LayerManager::setColorCache(const std::shared_ptr<ColorCache> &colorCache) {
    impl_->colorManager_.setColorCache(colorCache);
}

QStatusBar *LayerManager::statusBar(){
    QWidget *widget = this->widget();

    while (widget != nullptr) {
        WidgetDialog *widgetDialog = dynamic_cast<WidgetDialog *>(widget);
        if (widgetDialog != nullptr)
            return widgetDialog->statusBar();
        QMainWindow *mainWindow = dynamic_cast<QMainWindow *>(widget);
        if (mainWindow != nullptr)
            return mainWindow->statusBar();
        widget = widget->parentWidget();
    }
    return nullptr;
}

// 获取对应的image controller
ImageController *LayerManager::imageController() {
    return impl_->imageController_;
}


void LayerManager::Impl::setImageController() {
    QWidget *widget = thiz_->widget();

    while (widget != nullptr) {
        ImageController *imageController = dynamic_cast<ImageController *>(widget);
        if (imageController != nullptr) {
            this->imageController_ = imageController;
            return;
        }
        widget = widget->parentWidget();
    }

    this->imageController_ = nullptr;
}

void LayerManager::runInitialize(UiLayerTool *layerTool) {
    // 如果有不能取消的任务，退出
    if (isFocusedOnLayerTool())
        return;

    // 如果有初始化任务未完成，先取消它
    if (impl_->initializingTask_) {
        impl_->initializingTask_.reject(PM_LOC);
        impl_->initializingTask_.clear();
        //Will removed by the layerTool state machine itself
        //if (impl_->layerTools_.size() > 0) {
        //    remove(impl_->layerTools_.back().get());
        //}

    }
    impl_->initializingTask_ = layerTool->runInitialize();
}

void LayerManager::runEdit(UiLayerTool *layerTool) {
    layerTool->runEdit();
}

// 获取所属窗口
QWidget *LayerManager::widget() const {
    return impl_->widget_;
}

QPainter *LayerManager::painter() const {
    return impl_->painter_;
}

// 获取所属窗口当前的图片
std::shared_ptr<Image> LayerManager::getImage() const {
    if (impl_->painter_ != nullptr)
        return nullptr;

    ImageController *imageController = const_cast<LayerManager *>(this)->imageController();
    if (imageController != nullptr)
        return imageController->getImage();
    return nullptr;
}

// 图像分辨率
QSizeF LayerManager::imageSize() const {
    if (impl_->painter_ != nullptr) {
        return impl_->imageSize_;
        //return impl_->painter_->clipBoundingRect().size();
    }

    ImageController *imageController = const_cast<LayerManager *>(this)->imageController();
    if (imageController != nullptr)
        return imageController->imageSize_;

    return QSizeF(0, 0);
}

// 图像放大倍数
double LayerManager::imageZoom() const {
    if (impl_->painter_ != nullptr)
        return 1;

    ImageController *imageController = const_cast<LayerManager *>(this)->imageController();
    if (imageController != nullptr)
        return imageController->zoom_;

    return 1;
}

// 图像放大区域中心在整体图像的位置（比例 0 - 1）
QPointF LayerManager::imageZoomCenter() const {
    if (impl_->painter_ != nullptr)
        return QPointF(0.5, 0.5);

    ImageController *imageController = const_cast<LayerManager *>(this)->imageController();
    if (imageController != nullptr)
        return cv2qt(imageController->zoomCenter_);

    return QPointF(0.5, 0.5);
}

// 绘图区域的大小
QSizeF LayerManager::widgetSize() const {
    if (impl_->painter_ != nullptr)
        return impl_->painterSize_;

    ImageController *imageController = const_cast<LayerManager *>(this)->imageController();
    if (imageController != nullptr)
        return imageController->imageViewerRect().size();
    else
        return QSizeF(0, 0);
}

// 窗口坐标转成图像坐标
QPointF LayerManager::widgetToImage(const QPointF &pos) const {
    QRectF fullViewRect;     // 图像全图区域：相对于窗口显示的位置和大小
    QSizeF imageOriginalSize = imageSize();
    getZoomRect(imageOriginalSize,
        widgetSize(),
        imageZoomCenter(),
        imageZoom(),
        nullptr,
        nullptr,
        &fullViewRect);

    const QPointF &imagePos = ::widgetToImage(fullViewRect, imageOriginalSize, pos);
    return imagePos;
}

// 图像坐标转成窗口坐标
QPointF LayerManager::imageToWidget(const QPointF &pos) const {
    QRectF fullViewRect;     // 图像全图区域：相对于窗口显示的位置和大小
    QSizeF imageOriginalSize = imageSize();
    getZoomRect(imageOriginalSize,
        widgetSize(),
        imageZoomCenter(),
        imageZoom(),
        nullptr,
        nullptr,
        &fullViewRect);

    const QPointF &imagePos = ::imageToWidget(fullViewRect, imageOriginalSize, pos);
    return imagePos;
}

// 窗口坐标转成图像坐标
QLineF LayerManager::widgetToImage(const QLineF &pos) const {
    QPointF p1 = widgetToImage(pos.p1());
    QPointF p2 = widgetToImage(pos.p2());
    return QLineF(p1, p2);
}

// 图像坐标转成窗口坐标
QLineF LayerManager::imageToWidget(const QLineF &pos) const {
    QPointF p1 = imageToWidget(pos.p1());
    QPointF p2 = imageToWidget(pos.p2());
    return QLineF(p1, p2);
}

// 窗口坐标转成图像坐标
QSizeF LayerManager::widgetToImage(const QSizeF &size) const {
    return QSizeF(widgetToImage(size.width()), widgetToImage(size.height()));
}

// 图像坐标转成窗口坐标
QSizeF LayerManager::imageToWidget(const QSizeF &size) const {
    return QSizeF(imageToWidget(size.width()), imageToWidget(size.height()));
}

// 窗口坐标转成图像坐标
QRectF LayerManager::widgetToImage(const QRectF &rect) const {
    return QRectF(widgetToImage(rect.topLeft()), widgetToImage(rect.size()));
}
// 图像坐标转成窗口坐标
QRectF LayerManager::imageToWidget(const QRectF &rect) const {
    return QRectF(imageToWidget(rect.topLeft()), imageToWidget(rect.size()));
}


// 窗口距离转成图像距离
double LayerManager::widgetToImage(double size) const {
    QRectF fullViewRect;     // 图像全图区域：相对于窗口显示的位置和大小
    QSizeF imageOriginalSize = imageSize();
    getZoomRect(imageOriginalSize,
        widgetSize(),
        imageZoomCenter(),
        imageZoom(),
        nullptr,
        nullptr,
        &fullViewRect);

    return size * imageOriginalSize.width() / fullViewRect.width();
}

// 图像距离转成窗口距离
double LayerManager::imageToWidget(double size) const {
    QRectF fullViewRect;     // 图像全图区域：相对于窗口显示的位置和大小
    QSizeF imageOriginalSize = imageSize();
    getZoomRect(imageOriginalSize,
        widgetSize(),
        imageZoomCenter(),
        imageZoom(),
        nullptr,
        nullptr,
        &fullViewRect);

    return size * fullViewRect.width() / imageOriginalSize.width();
}


// 是否可编辑窗口
bool LayerManager::isEditable() const {
    return impl_->isEditable_;
}

// 是否可编辑窗口
void LayerManager::setEditable(bool isEditable) {
    impl_->isEditable_ = isEditable;
}

bool LayerManager::isValid(const UiLayerTool *layerTool) const {
    if (layerTool == nullptr) return false;

    //assert(this == layerTool->layerManager_);
    auto &layerTools = impl_->layerTools_;
    auto it = layerTools.begin();
    while (it != layerTools.end()) {
        if (it->second.get() == layerTool) {
            return true;
        }
    }

    return false;
}

size_t LayerManager::Impl::completeRemove(bool isRemoving) {
    size_t removeCount = 0;
    if (!isRemoving) {
        removeCount = this->aboutToRemoves_.size();
        for (auto &aboutToRemove : this->aboutToRemoves_) {
            auto it = aboutToRemove.second;
            this->layerTools_.erase(it);
        }
        this->aboutToRemoves_.clear();

        LOG_DEBUG("layerTools.size() = {}", this->layerTools_.size());
        if (!this->disableUpdateGui_ && this->widget_) {
            thiz_->update();
        }
    }

    this->isRemoving_.exchange(isRemoving);

    return removeCount;
}

size_t LayerManager::remove(const UiLayerTool *layerTool) {
    //std::shared_ptr<UiLayerTool> thiz = std::dynamic_pointer_cast<UiLayerTool>(layerTool->shared_from_this());
    //assert(this == layerTool->layerManager_);
    auto &layerTools = impl_->layerTools_;
    LOG_DEBUG("layerTools.size() = {}, remove = {}", layerTools.size(), layerTool);


    //vector 删除办法，不适用VList
    // layerTools.erase(std::remove(layerTools.begin(), layerTools.end(), nullptr), layerTools.end());

    bool isRemoving = impl_->isRemoving_.exchange(true);

    // 此时layerTool可能已经删除，只能根据指针匹配，不可以根据层名快速查找！
    auto it = layerTools.begin();
    while (it != layerTools.end()) {
        if (it->second.get() == layerTool) {

            // 加入aboutToRemoves_
            // 1. 防止析构函数中又赋值 shared_ptr，全乱了！！
            // 2. 防止closeEvent冲入
            if (impl_->aboutToRemoves_.find(it->second) == impl_->aboutToRemoves_.end()) {
                impl_->aboutToRemoves_.insert({ it->second, it });
                //LOG_DEBUG("remove {}", it->get());
                QEvent closeEvent(QEvent::Close);
                this->eventFilter(it->second.get(), &closeEvent);
            }
            //std::shared_ptr<UiLayerTool> keeper = *it;
        }
        ++it;
    }

    // 从管理器删除
    return impl_->completeRemove(isRemoving);
}

// 移除层对象
size_t LayerManager::remove(const std::string &layerName, const std::set<std::shared_ptr<LayerTool>> &layerToolSet) {
    auto &layerTools = impl_->layerTools_;
    LOG_DEBUG("layerTools.size() = {}", layerTools.size());


    //vector 删除办法，不适用VList
    // layerTools.erase(std::remove(layerTools.begin(), layerTools.end(), nullptr), layerTools.end());

    bool isRemoving = impl_->isRemoving_.exchange(true);

    auto it = layerTools.find(layerName);
    while (it != layerTools.end() && it->first == layerName) {
        const UiLayerTool *layerTool = it->second.get();
        if (layerTool != nullptr
            && it->second
            && layerToolSet.find(it->second->data_) != layerToolSet.end()
            ) {
            //LOG_DEBUG("remove {}", it->get());
            // 加入aboutToRemoves_
            // 1. 防止析构函数中又赋值 shared_ptr，全乱了！！
            // 2. 防止closeEvent冲入
            if (impl_->aboutToRemoves_.find(it->second) == impl_->aboutToRemoves_.end()) {
                impl_->aboutToRemoves_.insert({ it->second, it });
                QEvent closeEvent(QEvent::Close);
                this->eventFilter(it->second.get(), &closeEvent);
            }
        }
        ++it;
    }

    // 从管理器删除
    return impl_->completeRemove(isRemoving);
}

size_t LayerManager::remove(const std::string &layerName) {
    auto &layerTools = impl_->layerTools_;
    LOG_DEBUG("layerTools.size() = {}", layerTools.size());


    //vector 删除办法，不适用VList
    // layerTools.erase(std::remove(layerTools.begin(), layerTools.end(), nullptr), layerTools.end());

    bool isRemoving = impl_->isRemoving_.exchange(true);

    auto it = layerTools.find(layerName);
    while (it != layerTools.end() && it->first == layerName) {
        const UiLayerTool *layerTool = it->second.get();
        if(layerTool != nullptr) {
            //LOG_DEBUG("remove {}", it->get());
            // 加入aboutToRemoves_
            // 1. 防止析构函数中又赋值 shared_ptr，全乱了！！
            // 2. 防止closeEvent冲入
            if (impl_->aboutToRemoves_.find(it->second) == impl_->aboutToRemoves_.end()) {
                impl_->aboutToRemoves_.insert({ it->second, it });
                QEvent closeEvent(QEvent::Close);
                this->eventFilter(it->second.get(), &closeEvent);
            }
        }
        ++it;
    }

    // 从管理器删除
    return impl_->completeRemove(isRemoving);
}


// 移除所有同前缀的层
size_t LayerManager::removeByLayerPrefix(const std::string &layerNamePrefix) {
    auto &layerTools = impl_->layerTools_;
    LOG_DEBUG("layerTools.size() = {}", layerTools.size());


    //vector 删除办法，不适用VList
    // layerTools.erase(std::remove(layerTools.begin(), layerTools.end(), nullptr), layerTools.end());

    bool isRemoving = impl_->isRemoving_.exchange(true);

    auto it = layerTools.lower_bound(layerNamePrefix);

    while (it != layerTools.end() && 
        strStartWith(it->first, layerNamePrefix)) {
        const UiLayerTool *layerTool = it->second.get();
        if (layerTool != nullptr) {
            //LOG_DEBUG("remove {}", it->get());
            // 加入aboutToRemoves_
            // 1. 防止析构函数中又赋值 shared_ptr，全乱了！！
            // 2. 防止closeEvent冲入
            if (impl_->aboutToRemoves_.find(it->second) == impl_->aboutToRemoves_.end()) {
                impl_->aboutToRemoves_.insert({ it->second, it });
                QEvent closeEvent(QEvent::Close);
                this->eventFilter(it->second.get(), &closeEvent);
            }
        }
        ++it;
    }

    // 从管理器删除
    return impl_->completeRemove(isRemoving);

}

size_t LayerManager::removeAllLayerTools() {
    impl_->closeAllLayers();
    // 先保存到临时变量，再清除 layerTools_
    // 避免内部的layerTool析构时，再次调用到layerTools_结构

    bool isRemoving = impl_->isRemoving_.exchange(true);
    auto &layerTools = impl_->layerTools_;
    for (auto it = layerTools.begin(); it != layerTools.end(); ++it) {
        //LOG_DEBUG("remove {}", it->get());
        // 加入aboutToRemoves_
        // 1. 防止析构函数中又赋值 shared_ptr，全乱了！！
        // 2. 防止closeEvent冲入
        if (impl_->aboutToRemoves_.find(it->second) == impl_->aboutToRemoves_.end()) {
            impl_->aboutToRemoves_.insert({ it->second, it });
            QEvent closeEvent(QEvent::Close);
            this->eventFilter(it->second.get(), &closeEvent);
        }
    }
    impl_->layerTools_.clear();

    // 从管理器删除
    return impl_->completeRemove(isRemoving);
}

VMap<std::shared_ptr<UiLayerTool>> &LayerManager::layerTools() {
    return impl_->layerTools_;
}

bool LayerManager::isLayerDisabled(const std::string &layerName) const {
    if (hiddenLayers_.has(layerName))
        return true;
    else
        return false;
}

bool LayerManager::isLayerToolDisabled(const UiLayerTool *layerTool) const {
    return isLayerDisabled(layerTool->layerName_);
}




// 参数中任一任务状态达成时，reject第二个参数promise2nd中的所有任务
void raceAndReject2nd(const DeferLoop &loop, const std::initializer_list<Promise> &promise2nd) {
    std::vector<Promise> copy_list = promise2nd;

    std::shared_ptr<bool> isSuccess = std::make_shared<bool>(false);
    std::shared_ptr<int> winner = std::make_shared<int>(-1);

    int index = 0;
    for (auto it = copy_list.begin(); it != copy_list.end(); ++it, ++index) {
        Promise &promise = *it;
        promise.then(PM_LOC, [=](const pm_any &arg) {
            *isSuccess = true;
            if(*winner == -1)
                *winner = index;
            return arg;
        }, [=](const pm_any &arg) {
            if (*winner == -1)
                *winner = index;
            // 有成功时，reject后不做处理
            if (!*isSuccess) {
                loop.reject(PM_LOC, arg);
                //throw arg;
            }
        });
    }

    loop.getPromise().finally(PM_LOC, [copy_list, isSuccess, winner]() {
        PTD;
        *isSuccess = true;

        int index = 0;
        for (auto it = copy_list.begin(); it != copy_list.end(); ++it, ++index) {
            if (index != *winner) {
                const Promise &promise = *it;
                promise.reject(PM_LOC);
            }
        }
    });

    PTD;
    raceAndReject(PM_LOC, promise2nd);
}

void LayerManager::setHiddenLayer(const VStrSet& hiddenlayers) {
    hiddenLayers_ = hiddenlayers;
}