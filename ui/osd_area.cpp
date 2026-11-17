#include "ui/osd_area.h"

#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES
#endif
#include "ui/osd_area.h"
#include "ui/osd.h"
#include "utils/var_type.h"
#include "utils/log.h"
#include "utils/any_pro.h"
#include "utils/qt_utils.h"
#include "utils/type_category.h"
#include "types/vlist.h"
#include "promise-cpp/add_ons/qt/promise_qt.hpp"
#include <QPixmap>
#include <QBitmap>
#include <QPainter>
#include <QPainterPath>
#include <QStyle>
#include <QKeyEvent>
#include <QStyleOption>
#include <QMenu>
#include <QWidgetAction>

#define MAX_OSD_ARRAY_CHECK_SIZE 10
using namespace promise;

static nlohmann::json osdToJson(const anypro::any &variant) {
    Osd *value = anypro::rawCastPointer<Osd>(variant);
    if (value == nullptr)
        return {};

    // 防止递归导出
    auto treeRecord = VarTreeRecord::record(value, variant.type().raw_type());
    if (!treeRecord)
        return {};

    return VarTable::saveToJson(*value->cfgTable_);
}

void osdFromJson(anypro::any &variant, const nlohmann::json &json) {
    Osd *value = anypro::rawCastPointer<Osd>(variant);
    if (value != nullptr) {
        VarTable::loadFromJson(*value->cfgTable_, json);
        value->isInitialized_ = true;
    }
}

#if 0
    virtual anypro::any newInstance(const nlohmann::json &json) const {
        LOG_DEBUG("json = {}\n", json.dump(4));
        const auto &jsonOsdType = json("type");
        if (!jsonOsdType.is_string()) return anypro::any();
        const auto &osdType = jsonOsdType.get<std::string>();
        auto osd = Osd::newInstance(osdType);
        VarTable::loadFromJson(*osd->cfgTable_, json);
        osd->isInitialized_ = true;
        return osd;
    }


    virtual anypro::any newSharedInstance(const nlohmann::json &json) const {
        LOG_DEBUG("json = {}\n", json.dump(4));
        const auto &jsonOsdType = json("type");
        if (!jsonOsdType.is_string()) return anypro::any();
        const auto &osdType = jsonOsdType.get<std::string>();
        auto Osd = Osd::newInstance(osdType);
        VarTable::loadFromJson(*Osd->cfgTable_, json);
        Osd->isInitialized_ = true;
        return Osd;
    }
#endif

AP_REGISTRATION{
    anypro::registration::class_<Osd>("Osd")
        .base<LayerTool>()
        .property_readonly("type", &Osd::osdType_)
        .property("text", &Osd::text_)
        .property("labels", &Osd::labels_)
        .property("color", &Osd::color_)
        .property_readonly("config", &Osd::cfgTable_);

    registerVarOp<std::shared_ptr<Osd>>()
        .create([](const nlohmann::json &json)->anypro::any {
            anypro::any osd;
            if (json.contains("type")) {
                const std::string &type = json.at("type");
                osd = Osd::newInstance(type);
                osdFromJson(osd, json);
            }
            return osd;
        });

    registerVarOp<Osd>()
        .toJson(osdToJson)
        .fromJson(osdFromJson);
}

static void osdInitTable(Osd *osd, const std::string &osdType) {
    auto disabled = osd->cfgTable_->scopedDisableChangedEvent();
    osd->cfgTable_->push_back({ "type", osdType });
    osd->cfgTable_->push_back({ "color", &osd->color_ });
    osd->cfgTable_->push_back({ "text", &osd->text_ });
    osd->cfgTable_->push_back({ "labels", &osd->labels_ });
    osd->cfgTable_->push_back({ "isInitialized_", &osd->isInitialized_ });
    osd->cfgTable_->push_back({ "isFirstInArray_", &osd->isFirstInArray_ });
}

Osd::Osd(const Osd &right)
    : Osd(right.osdType_) {
    LOG_DEBUG("copy Osd {} {}", this, &right);

    static_cast<LayerTool &>(*this) = static_cast<const LayerTool &>(right);

    this->text_ = right.text_;
    this->labels_ = right.labels_;
    this->color_ = right.color_;
    // 以下不复制
    //this->cfgTable_ = right.cfgTable_;
    //this->boundVars_ = right.boundVars_;
}

Osd::Osd(const std::string &osdType)
    : osdType_(osdType)
    , text_()
    , labels_()
    , cfgTable_(VariantMap::newInstance(LOC))
    //, boundVariables_(VStrSet::newInstance(LOC))
{

    LOG_DEBUG("this = {}", this);
    osdInitTable(this, osdType);
}

Osd::~Osd() {
    LOG_DEBUG("destroyed");
}

std::string Osd::toolClassName() const {
    return "osd:" + osdType_;
}

std::shared_ptr<Osd> Osd::newInstance(const std::string &osdType) {
    const OsdFactory *osdFactory = UiOsd::getOsdFactory(osdType);
    if (osdFactory != nullptr)
        return osdFactory->createOsd_();
    return nullptr;
}

std::shared_ptr<Osd> Osd::clone() {
    std::shared_ptr<Osd> ret = Osd::newInstance(this->osdType_);
    ret->copyFrom(*this);
    return ret;
}

// 从绑定的姿态位置的区域（从this osd对象），转成相对于图像的区域
std::shared_ptr<Osd> Osd::attitudeToImage(const std::shared_ptr<Osd> &osd, Attitude *attitude) const {
    //TODO ??
    return osd;
}
// 从相对于图像的区域，转到绑定的姿态位置的区域（存于this osd对象）
std::shared_ptr<Osd> Osd::imageToAttitude(const std::shared_ptr<Osd> &osd, Attitude *attitude) const {
    //TODO ??
    return osd;
}

cv::RotatedRect Osd::minAreaRect() {
    return cv::RotatedRect{};
}

bool Osd::contains(const cv::Point2f &point) const {
    return false;
}

void Osd::copyFrom(const Osd &osd) {
    const auto &varOp = varGetOp<Osd>();
    const nlohmann::json &json = varOp->toJson(&osd);
    anypro::any thiz(this);
    varOp->fromJson(thiz, json);
}




struct OsdConvertor {
    hi_null_mutex mutex_{ LOC };
    std::map<anypro::type, std::function<std::shared_ptr<Osd>(const anypro::any &from)>> convertors_;
};


// 注册可视类型
void Osd::registerConvertor(const anypro::type &type, const std::function<std::shared_ptr<Osd>(const anypro::any &from)> &convertor) {
    tcRegister(type, TypeCategory::kVisiable);
    if (TlApplication::isMainWindowReady()) {
        throw std::runtime_error("need call Osd::registerConvertor before MainWindow ready");
    }
    OsdConvertor &osdConvertor = singleton<OsdConvertor>();
    auto lock = hi_unique_lock(osdConvertor.mutex_);
    osdConvertor.convertors_[type] = convertor;
}

// 根据注册可是类型创建可视对象
std::shared_ptr<Osd> Osd::convertFrom(const anypro::any &from) {

    // <1> 检查可转换为可视变量的类型
    const anypro::type &type = from.type().raw_type();
    std::function<std::shared_ptr<Osd>(const anypro::any &from)> convertor = nullptr;
    {
        OsdConvertor &osdConvertor = singleton<OsdConvertor>();
        auto lock = hi_shared_lock(osdConvertor.mutex_);
        auto found = osdConvertor.convertors_.find(type);
        if (found != osdConvertor.convertors_.end())
            convertor = found->second;
    }
    if (convertor)
        return convertor(from);

    // <1> 自身就是osd变量，并且用 shared_ptr 保存的，直接返回
    try {
        return anypro::parameter_cast<std::shared_ptr<Osd>>(from);
    }
    catch (const anypro::bad_any_cast &) {
    }

    // <2> 自身就是osd变量，并且用非 shared_ptr 保存的，复制出一个新的osd
    Osd *osd = anypro::rawCastPointer<Osd>(from);
    if (osd != nullptr) {
        std::shared_ptr<Osd> ret = Osd::newInstance(osd->osdType_);
        if (ret) {
            ret->copyFrom(*osd);
            return ret;
        }
    }
 
    return nullptr;
}

// 增加附着显示层（线程安全）
static void appendOsd(bool &isFirst, VList<std::shared_ptr<Osd>> &osds, const std::shared_ptr<Osd> &osd) {
    LOG_DEBUG("isFirst = {}", isFirst);
    osd->isFirstInArray_ = isFirst;
    isFirst = false;
    osds.push_back(osd);
}

// 增加附着显示层
static void appendOsd(bool &isFirst, VList<std::shared_ptr<Osd>> &osds, const std::string &text, const anypro::any &visiableObject) {

    const auto &varOp = varGetOp(visiableObject);
    auto varLock = hi_shared_lock(varOp->mutexProxy(visiableObject));
    // 处理数组类型
    if (varOp != nullptr && varOp->toVarList) {
        const std::vector<anypro::any> &vector = varOp->toVarList(visiableObject);
        bool isFirst2 = true;
        size_t osdCount = osds.size();
        for(size_t i = 0; i < vector.size(); ++i) {
            if (i >= MAX_OSD_ARRAY_CHECK_SIZE && osds.size() == osdCount)
                break;
            const anypro::any &item = vector[i];
            std::string newText = strJoin({ text, fmt::format("{}", i) }, ",", true);
            appendOsd(isFirst2, osds, newText, item);
        }
    }
    // 处理映射类型
    else if (varOp != nullptr && varOp->toVarMap) {
        const std::map<std::string, anypro::any> &map = varOp->toVarMap(visiableObject);
        bool isFirst2 = true;
        size_t osdCount = osds.size();
        size_t i = 0;
        for (const auto &item : map) {
            if (i++ >= MAX_OSD_ARRAY_CHECK_SIZE && osds.size() == osdCount)
                break;
            std::string newText = strJoin({ text, item.first }, ",", true);
            appendOsd(isFirst2, osds, newText, item.second);
        }
    }
    else {
        std::shared_ptr<Osd> osd = Osd::convertFrom(visiableObject);
        if (osd) {
            osd->labels_ = text;
            appendOsd(isFirst, osds, osd);
        }
    }
}


// 增加附着显示层（线程安全）
static void appendOsd(bool &isFirst, VMap<std::shared_ptr<Osd>> &osdsEx, const std::string &name, const std::shared_ptr<Osd> &osd) {
    LOG_DEBUG("name = {}, isFirst = {}", name, isFirst);
    osd->isFirstInArray_ = isFirst;
    isFirst = false;
    osdsEx.insert({ name, osd });
}

// 增加附着显示层
static void appendOsd(bool &isFirst, VMap<std::shared_ptr<Osd>> &osdsEx, const std::string &name, const std::string &text, const anypro::any &visiableObject) {
    const auto &varOp = varGetOp(visiableObject);
    auto varLock = hi_shared_lock(varOp->mutexProxy(visiableObject));

    // 处理数组类型
    if (varOp != nullptr && varOp->toVarList) {
        const std::vector<anypro::any> &vector = varOp->toVarList(visiableObject);
        bool isFirst2 = true;
        size_t osdExCount = osdsEx.size();
        for (size_t i = 0; i < vector.size(); ++i) {
            if (i >= MAX_OSD_ARRAY_CHECK_SIZE && osdsEx.size() == osdExCount)
                break;

            const anypro::any &item = vector[i];
            std::string newText = strJoin({text, fmt::format("{}", i) }, ",", true);
            appendOsd(isFirst2, osdsEx, name, newText, item);
            //std::shared_ptr<Osd> osd = Osd::convertFrom(item);
            //if (osd)
            //    appendOsd(osdsEx, name, osd);
        }
    }
    // 处理映射类型
    else if (varOp != nullptr && varOp->toVarMap) {
        // 如果前10个元素，均没有可视变量，就不再探索了  
        const std::map<std::string, anypro::any> &map = varOp->toVarMap(visiableObject);
        bool isFirst2 = true;
        size_t osdExCount = osdsEx.size();
        size_t i = 0;
        for (const auto &item : map) {
            if (i++ >= MAX_OSD_ARRAY_CHECK_SIZE && osdsEx.size() == osdExCount)
                break;

            //appendOsd(osdsEx, fmt::format("{}.{}", name, item.first), item.second);
            std::string newText = strJoin({ text, item.first }, ",", true);
            appendOsd(isFirst2, osdsEx, name, newText, item.second); // 不显示更细节的名字

            //std::shared_ptr<Osd> osd = Osd::convertFrom(item.second);
            //if (osd)
            //    appendOsd(osdsEx, name, osd);
        }
    }
    else {
        std::shared_ptr<Osd> osd = Osd::convertFrom(visiableObject);
        if (osd) {
            osd->labels_ = text;
            appendOsd(isFirst, osdsEx, name, osd);
        }
    }
}


// 增加附着显示层（线程安全）
void Osd::appendOsd(VList<std::shared_ptr<Osd>> &osds, const std::shared_ptr<Osd> &osd) {
    bool isFirst = true;
    ::appendOsd(isFirst, osds, osd);
}

// 增加附着显示层
void Osd::appendOsd(VList<std::shared_ptr<Osd>> &osds, const anypro::any &visiableObject) {
    bool isFirst = true;
    ::appendOsd(isFirst, osds, "", visiableObject);
}

// 增加附着显示层（线程安全）
void Osd::appendOsd(VMap<std::shared_ptr<Osd>> &osdsEx, const std::string &name, const std::shared_ptr<Osd> &osd) {
    bool isFirst = true;
    ::appendOsd(isFirst, osdsEx, name, osd);
}

// 增加附着显示层
void Osd::appendOsd(VMap<std::shared_ptr<Osd>> &osdsEx, const std::string &name, const anypro::any &visiableObject) {
    bool isFirst = true;
    ::appendOsd(isFirst, osdsEx, name, "", visiableObject);
}
