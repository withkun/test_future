#include "ui/osd.h"
#include "ui/osd/osd_circle.h"
#include "ui/osd/osd_line.h"
#include "ui/osd/osd_rectangle.h"
#include "ui/osd/osd_text.h"
#include "ui/osd/osd_arrow.h"
#include "ui/osd/osd_outline.h"


UiOsd::UiOsd(std::shared_ptr<Osd> data)
    : UiLayerTool(data)
    , osdType_(data->osdType_) {

}

UiOsd::~UiOsd() {
}


void UiOsd::registerOsd(
    const std::string &osdType,
    const std::string &localOsdType,
    const std::function<std::shared_ptr<Osd>()> &createOsd,
    const std::function<std::shared_ptr<UiOsd>(const std::shared_ptr<Osd> &osd)> &createUiOsd) {

    const std::vector<OsdFactory> &osdFactorys = UiOsd::getOsdFactory();

    LOG_INFO("osdFactorys = {}", &osdFactorys);
    const_cast<std::vector<OsdFactory> &>(osdFactorys).push_back({
        osdType,
        localOsdType,
        createOsd,
        createUiOsd
        });
}


const std::vector<OsdFactory> &UiOsd::getOsdFactory() {
    static std::vector<OsdFactory> s_osdFactorys;
    return s_osdFactorys;
}


const OsdFactory *UiOsd::getOsdFactory(const std::string &osdType) {
    const std::vector<OsdFactory> &osdFactories = UiOsd::getOsdFactory();
    for (const OsdFactory &osdFactory : osdFactories) {
        if (osdFactory.osdType_ == osdType)
            return &osdFactory;
    }
    return nullptr;
}

std::shared_ptr<UiOsd> UiOsd::newInstanceBy(const std::shared_ptr<LayerManager> &layerManager,
    const OsdFactory &osdFactory,
    const std::shared_ptr<LayerTool> &osd,
    const std::string &layerName,
    bool isReadonly) {
    std::shared_ptr<UiOsd> uiOsd = osdFactory.createUiOsd_(std::dynamic_pointer_cast<Osd>(osd));
    uiOsd->layerName_ = layerName;
    uiOsd->isReadonly_ = isReadonly;
    uiOsd->setLayerManager(layerManager);

    // 强制层设为显示（即使该层已经隐藏）
    //layerManager->hiddenLayers_.remove(layerName); // 移到用户界面添加时才显示

    return uiOsd;
}

std::shared_ptr<UiOsd> UiOsd::newInstance(const std::shared_ptr<LayerManager> &layerManager,
    const std::string &osdType,
    const std::shared_ptr<LayerTool> &osd,
    const std::string &layerName,
    bool isReadonly) {
    const OsdFactory *osdFactory = UiOsd::getOsdFactory(osdType);
    if (osdFactory != nullptr)
        return newInstanceBy(layerManager, *osdFactory, osd, layerName, isReadonly);
    return nullptr;
}

// 判断点是否落在本对象（感兴趣的区域）上面
bool UiOsd::isOnObject(const QPointF &pos, Qt::CursorShape &mouseDirection) {
    return false;
}

// 初始绘制状态
promise::Promise UiOsd::runInitialize() {
    return promise::resolve(PM_LOC);
}

// 绘制完可修改编辑状态
promise::Promise UiOsd::runEdit() {
    return promise::resolve(PM_LOC);
}

// 进入右键菜单
promise::Promise UiOsd::runContextMenu(const QPointF &mousePos, const std::function<void(QMenu *, const FnAddMenu &)> &onMenuCreated) {
    (void)mousePos;
    (void)onMenuCreated;
    return promise::resolve(PM_LOC);
}

std::shared_ptr<Osd> UiOsd::getData() const {
    return std::dynamic_pointer_cast<Osd>(data_);
}
