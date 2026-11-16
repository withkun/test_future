#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES
#endif
#include "ui/roi_area.h"
#include "ui/roi.h"
#include "utils/var_type.h"
#include "utils/log.h"
#include "utils/any_pro.h"
#include "utils/qt_utils.h"
#include "types/vlist.h"
#include "types/widget.h"
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
using namespace promise;


static nlohmann::json roiToJson(const anypro::any &variant) {
    Roi *value = anypro::rawCastPointer<Roi>(variant);
    if (value == nullptr)
        return {};

    // 防止递归导出
    auto treeRecord = VarTreeRecord::record(value, variant.type().raw_type());
    if (!treeRecord)
        return {};

    return VarTable::saveToJson(*value->cfgTable_);
}

void roiFromJson(anypro::any &variant, const nlohmann::json &json) {
    Roi *value = anypro::rawCastPointer<Roi>(variant);
    if (value != nullptr) {
        VarTable::loadFromJson(*value->cfgTable_, json);
        value->isInitialized_ = true;
    }
}

#if 0
    virtual anypro::any newInstance(const nlohmann::json &json) const {
        LOG_DEBUG("json = {}\n", json.dump(4));
        const auto &jsonRoiType = json("type");
        if (!jsonRoiType.is_string()) return anypro::any();
        const auto &roiType = jsonRoiType.get<std::string>();
        auto roi = Roi::newInstance(roiType);
        VarTable::loadFromJson(*roi->cfgTable_, json);
        roi->isInitialized_ = true;
        return roi;
    }


    virtual anypro::any newSharedInstance(const nlohmann::json &json) const {
        LOG_DEBUG("json = {}\n", json.dump(4));
        const auto &jsonRoiType = json("type");
        if (!jsonRoiType.is_string()) return anypro::any();
        const auto &roiType = jsonRoiType.get<std::string>();
        auto roi = Roi::newInstance(roiType);
        VarTable::loadFromJson(*roi->cfgTable_, json);
        roi->isInitialized_ = true;
        return roi;
    }
#endif

AP_REGISTRATION{
    anypro::registration::class_<Roi>("Roi")
        .base<LayerTool>()
        .constructor<>()
        .property_readonly("roiType", &Roi::roiType_) // 兼容性保留
        .property_readonly("type", &Roi::roiType_)
        .property_readonly("roiCategory", &Roi::roiCategory_)
        .property("color", &Roi::color_)
        .property("text", &Roi::text_)
        .property_readonly("isExtByAttitude", &Roi::isExtByAttitude_)
        .property_readonly("uuid", &Roi::uuid_)
        .property_readonly("config", &Roi::cfgTable_)
        .property_readonly("attitude", [](Roi *roi) {
            return roi->getAttitude();
        });

    registerVarOp<std::shared_ptr<Roi>>()
        .create([](const nlohmann::json &json)->anypro::any {
            std::shared_ptr<Roi> roi;
            if (json.contains("type")) {
                const std::string &type = json.at("type");
                roi = Roi::newInstance(type);
            }
            else
                roi = Roi::newInstance("");

            anypro::any aRoi = roi;
            if (roi) {
                roiFromJson(aRoi, json);
            }
            return aRoi;
        });

    registerVarOp<Roi>()
        .toJson(roiToJson)
        .fromJson(roiFromJson);

    anypro::registration::class_<RoiGroup>("RoiGroup")
        .constructor<>()
        .property_readonly("rois", &RoiGroup::rois_)
        .property_readonly("attitude", &RoiGroup::attitude_)
        .method("getAttitudeId", &RoiGroup::getAttitudeId)
        .method("getRoiId", &RoiGroup::getRoiId)
        .method("getRoiIndex", &RoiGroup::getRoiIndex)
        ;
}

static void roiInitTable(Roi *roi) {
    roi->cfgTable_->push_back({ "color",            &roi->color_ });
    roi->cfgTable_->push_back({ "text",             &roi->text_ });
    roi->cfgTable_->push_back({ "isInitialized_",   &roi->isInitialized_ });
    roi->cfgTable_->push_back({ "isExtByAttitude_", &roi->isExtByAttitude_ });
    roi->cfgTable_->push_back({ "uuid",             &roi->uuid_ });
}

Roi::Roi(const Roi &right)
    : Roi(right.roiType_, right.roiCategory_) {
    LOG_DEBUG("copy Roi {} {}", this, &right);

    static_cast<LayerTool &>(*this) = static_cast<const LayerTool &>(right);

    this->color_ = right.color_;
    this->text_ = right.text_;
    this->widget_ = right.widget_;
    this->isExtByAttitude_ = right.isExtByAttitude_;
    this->uuid_ = QUuid::createUuid().toString().toStdString();
    this->roi0_ = right.roi0_;

    // 以下不复制
    //this->cfgTable_ = right.cfgTable_;
    //this->boundVars_ = right.boundVars_;
}

Roi::Roi(const std::string &roiType, Roi::RoiCategory roiCategory)
    : roiType_(roiType)
    , roiCategory_(roiCategory)
    , widget_()
    , cfgTable_(VariantMap::newInstance(LOC))
    //, boundVariables_(VStrSet::newInstance(LOC)) 
{
    this->color_ = QColor();
    this->text_ = QObject::tr("ROI").toStdString();
    this->isExtByAttitude_ = false;
    this->uuid_ = QUuid::createUuid().toString().toStdString();

    //this->color_ = QColor(0, 0, 128);
    //this->color_ = QColor(187, 61, 0);

    LOG_DEBUG("this = {}", this);
    this->cfgTable_->push_back({ "type", roiType });
    roiInitTable(this);
}

Roi::~Roi() {
    LOG_DEBUG("destroyed");
}

// 层工具的类名字
std::string Roi::toolClassName() const {
    auto rois0 = this->roi0_.lock();
    
    if (!rois0) {
        LOG_WARN("roi = {}, uuid = {}", this, uuid_);
    }

    if (rois0.get() == this || !rois0) {
        auto name = fmt::format("roi:{}:{}", roiType_, this->uuid_);
        return name;
    }
    else {
        auto name = fmt::format("roi:{}:{}", roiType_, rois0->uuid_);
        return name;
    }
}

std::shared_ptr<Roi> Roi::newInstance(const std::string &roiType) {
    const RoiFactory *roiFactory = UiRoi::getRoiFactory(roiType);
    if (roiFactory != nullptr) {
        std::shared_ptr<Roi> roi = roiFactory->createArea_();
        roi->roi0_ = roi;
        return roi;
    }
    return nullptr;
}

std::shared_ptr<Roi> Roi::clone() {
    std::shared_ptr<Roi> ret = Roi::newInstance(this->roiType_);
    ret->copyFrom(*this);
    return ret;
}

// 枚举类型转为字符串类型
std::string Roi::roiTypeName(const RoiType &roiType) {
    static std::map<RoiType, std::string> s_types = {
        { RoiType::kFreeDraw, "freedraw" },
        { RoiType::kRotatedRect, "rectangle" },
        { RoiType::kRect, "straight_rectangle" },
        { RoiType::kCircle, "circle" },
        { RoiType::kEllipse, "ellipse" },
        { RoiType::kLabel, "label_area" },
        { RoiType::kLine, "line" }
    };

    auto found = s_types.find(roiType);
    if (found != s_types.end())
        return found->second;
    else
        return std::string();
}

// 从绑定的姿态位置的区域（从this roi对象），转成相对于图像的区域
std::shared_ptr<Roi> Roi::attitudeToImage(const Attitude *attitude) const {
    (void)attitude;
    //TODO ??
    return std::dynamic_pointer_cast<Roi>(const_cast<Roi *>(this)->shared_from_this());
}


// 从相对于图像的区域，转到绑定的姿态位置的区域
std::shared_ptr<Roi> Roi::imageToAttitude(const Attitude *attitude) const {
    (void)attitude;
    //TODO ??
    return std::dynamic_pointer_cast<Roi>(const_cast<Roi *>(this)->shared_from_this());
}

// 解绑姿态变量（从相对于姿态的坐标，转成图像坐标）
void Roi::detachAttitude(const Attitude *attitude) {
    // Nothing
}

// 绑定姿态变量（从图像坐标，转成相对于姿态的坐标）
void Roi::attachAttitude(const Attitude *attitude) {
    // Nothing
}


// 获取所有经姿态扩展的Roi列表
std::vector<std::shared_ptr<Roi>> Roi::getExtRoiByAttitude() const {
    //const std::vector<std::shared_ptr<Roi>> &rois = { impl_->data_ };
    std::vector<std::shared_ptr<Roi>> rois;

    bool hasRefVar = false;
    std::shared_ptr<Widget> widget = widget_.lock();
    if (widget && this->isExtByAttitude_) {
        const Widget::RefAttitudes &attitudes = widget->getAttitudes();
        if (attitudes.hasRefVar_) {
            hasRefVar = true;
            for (const std::shared_ptr<Attitude> attitude : attitudes.attitudes_) {
                const std::shared_ptr<Attitude> &attitude0 = attitudes.attitudes_.front();
                std::shared_ptr<Roi> roi0 = imageToAttitude(attitude0.get());
                std::shared_ptr<Roi> roi1 = roi0->attitudeToImage(attitude.get());
                roi1->roi0_ = this->roi0_;
                rois.push_back(roi1);
            }
        }
    }

    if (!hasRefVar) {
        rois.push_back(std::dynamic_pointer_cast<Roi>(const_cast<Roi *>(this)->shared_from_this()));
    }

    return rois;
}

// 是否参考姿态为空（有姿态变量，但是数量为0）
bool Roi::hasEmptyAttitudes() const {
    std::shared_ptr<Widget> widget = widget_.lock();
    if (widget) {
        return widget->hasEmptyAttitudes();
    }
    return false;
}

#if 0
// 获取参考姿态变量
static std::shared_ptr<Attitude> getAttitude0_orNull(const Roi *thiz) {
    // 获取所有的姿态变量
    for (const std::string &varName : thiz->boundVars_) {
        // 获取变量对应的显示数据
        LOG_DEBUG("varName = {}", varName);

        // 检测数组变量
        const std::string &varArrayName = varIsArray(varName);
        if (!varArrayName.empty()) {
            // 数组变量
            LOG_DEBUG("found array = {}", varArrayName);
            const anypro::any &varSelectedArray = varGet(varArrayName);
            VList<std::shared_ptr<Attitude>> *attitudes = anypro::rawCastPointer<VList<std::shared_ptr<Attitude>>>(varSelectedArray);
            if (attitudes != nullptr) {
                for (const auto &attitude : *attitudes) {
                    if (attitude) return attitude;
                }
            }
        }
        else {
            // 非数组变量
            const anypro::any &varSelected = varGet(varName);
            Attitude *attitude = nullptr;
            try {
                const anypro::any &aAttitude = anypro::parameter_cast<Attitude *>(varSelected);
                attitude = anypro::rawCastPointer<Attitude>(aAttitude);
            }
            catch (const anypro::bad_any_cast &ex) {
                (void)ex;
            }

            if (attitude != nullptr) {
                std::shared_ptr<Attitude> sAttitude = attitude->shared_from_this();
                return sAttitude;
            }
        }
    }

    return nullptr;
}

// 获取参考姿态变量
std::shared_ptr<Attitude> Roi::getAttitude0() const {
    const std::shared_ptr<Attitude> &attitude = getAttitude0_orNull(this);
    if (attitude != nullptr)
        return attitude;
    else
        return std::make_shared<Attitude>();
}

// 获取参考姿态变量
std::vector<std::shared_ptr<Attitude>> Roi::getAttitudes() const {
    // 获取所有的姿态变量
    std::vector<std::shared_ptr<Attitude>> attitudeList;  // 按照自然获取顺序
    std::set<std::shared_ptr<Attitude>> attitudeSet;    // 去重
    for (const std::string &varName : this->boundVars_) {
        // 获取变量对应的显示数据
        LOG_DEBUG("varName = {}", varName);

        // 检测数组变量
        const std::string &varArrayName = varIsArray(varName);
        if (!varArrayName.empty()) {
            // 数组变量
            LOG_DEBUG("found array = {}", varArrayName);
            const anypro::any &varSelectedArray = varGet(varArrayName);
            VList<std::shared_ptr<Attitude>> *attitudes = anypro::rawCastPointer<VList<std::shared_ptr<Attitude>>>(varSelectedArray);
            if (attitudes != nullptr) {
                for (const auto &attitude : *attitudes) {
                    if (attitude && attitudeSet.find(attitude) == attitudeSet.end()) {
                        attitudeList.push_back(attitude);
                        attitudeSet.insert(attitude);
                    }
                }
            }
        }
        else {
            // 非数组变量
            const anypro::any &varSelected = varGet(varName);
            Attitude *pAttitude = nullptr;
            try {
                const anypro::any &aAttitude = anypro::parameter_cast<Attitude *>(varSelected);
                pAttitude = anypro::rawCastPointer<Attitude>(aAttitude);
            }
            catch (const anypro::bad_any_cast &ex) {
                (void)ex;
            }

            if (pAttitude != nullptr) {
                std::shared_ptr<Attitude> attitude = pAttitude->shared_from_this();
                if (attitude && attitudeSet.find(attitude) == attitudeSet.end()) {
                    attitudeList.push_back(attitude);
                    attitudeSet.insert(attitude);
                }
            }
        }
    }

    //LOG_DEBUG("attitudeList.size() = {}", attitudeList.size());    

    if (attitudeList.size() == 0) {
        attitudeList.push_back(std::make_shared<Attitude>());
    }

    return attitudeList;
}

// 获取参考姿态的首个位置信息
std::shared_ptr<Roi> Roi::getRealArea0() const {
    //const std::shared_ptr<Attitude> &attitude = getAttitude0_orNull(this);
    //if (!attitude) {
        auto thiz = std::dynamic_pointer_cast<Roi>(const_cast<Roi *>(this)->shared_from_this());
        return thiz;
    //}
    //else {
    //    return attitudeToImage(attitude.get());
    //}
}

// 获取参考姿态的位置信息
std::vector<std::shared_ptr<Roi>> Roi::getRealAreas() const {
    const std::vector<std::shared_ptr<Attitude>> &attitudes = this->getAttitudes();
    std::vector<std::shared_ptr<Roi>> areas;
    areas.reserve(std::max(attitudes.size(), (size_t)1));
    if (attitudes.size() == 0) {
        auto thiz = std::dynamic_pointer_cast<Roi>(const_cast<Roi *>(this)->shared_from_this());
        areas.push_back(thiz);
    }
    else {
        const std::shared_ptr<Attitude> &attitude0 = getAttitude0_orNull(this);
        std::shared_ptr<Roi> area0 = getRealArea0();
        std::shared_ptr<Roi> attiArea0 = area0->imageToAttitude(attitude0.get());

        auto thiz = std::dynamic_pointer_cast<Roi>(const_cast<Roi *>(this)->shared_from_this());
        areas.push_back(thiz);

        for (const auto &attitude : attitudes)
            areas.push_back(attiArea0->attitudeToImage(attitude.get()));
    }

    return areas;
}

// 获取参考姿态的首个最小外接矩形
cv::RotatedRect Roi::minAreaRect0() {
    return getRealArea0()->minAreaRect();
}

// 获取参考姿态的所有最小外接矩形
std::vector<cv::RotatedRect> Roi::minAreaRects() {
    const auto &realAreas = getRealAreas();
    std::vector<cv::RotatedRect> rects;
    rects.reserve(realAreas.size());
    for (const auto &realArea : realAreas) {
        rects.push_back(realArea->minAreaRect());
    }
    return rects;
}
#endif


cv::RotatedRect Roi::minAreaRect() const {
    return cv::RotatedRect{};
}

// 获取roi区域的图像（超出图像范围的部分，使用patchBgra填充）  
std::pair<cv::Mat, cv::RotatedRect> Roi::roiImage(const cv::Mat &inputImage, const cv::Scalar &patchBgra, bool tryInPlace, bool isMarginTransparent) const {
    (void)isMarginTransparent;
    const cv::RotatedRect &rect = minAreaRect();

    auto cropped = cvGetCroppedImage(
        inputImage,
        rect,
        patchBgra,
        tryInPlace);

    return cropped;
}

// 获取姿态  
Attitude Roi::getAttitude() const {
    Attitude ret;
    ret.score_ = 0;
    ret.radian_ = 0;
    return ret;
}

// 获取中心
cv::Point2f Roi::getCenter() const {
    return cv::Point2f(0, 0);
}

bool Roi::contains(const cv::Point2f &point) const {
    return false;
}

void Roi::copyFrom(const Roi &roi) {
    const auto &varOp = varGetOp<Roi>();
    const nlohmann::json &json = varOp->toJson(&roi);
    anypro::any thiz(this);
    varOp->fromJson(thiz, json);
}

void Roi::paint(UiRoi &uiRoi, QPainter &qp, bool isFirstInGroup, bool isDrawHotSpot) {
    (void)uiRoi;
    (void)qp;
    (void)isFirstInGroup;
    (void)isDrawHotSpot;
}

// 比较roi基类R部分
bool Roi::equalsBase(const Roi& other) const {
    return this->roiType_ == other.roiType_
        && this->roiCategory_ == other.roiCategory_
        && this->text_ == other.text_;
}

// 比较两个Roi对象是否相等
bool Roi::operator==(const Roi& other) const {

    // 理论上roiType_相同的话roi类型也相同,这里确认下
    if (typeid(*this) != typeid(other)) {
        return false;
    }

    // 检查roi基类部分是否相等
    if (!this->equalsBase(other)) {
        return false;
    }

    // 检查具体roi部分是否相等
    if (!this->equals(other)) {
        return false;
    }

    return true;
}

// 判断Roi分组是否有元素
bool isEmpty(const std::vector<RoiGroup> &roiGroups) {
    return (roiGroups.size() == 0 || roiGroups[0].rois_.size() == 0);
}


struct RoiGroup::Impl {
    Impl(RoiGroup *thiz)
        : thiz_(thiz) {
    }
    void flushIndexes() {
        if (roiIndexes_.size() == 0 && thiz_->rois_.size() > 0) {
            for (size_t index = 0; index < thiz_->rois_.size(); ++index) {
                std::shared_ptr<Roi> roi = thiz_->rois_[index];
                //LOG_WARN("roi = {}", roi);
                roiIndexes_.insert({ roi, index });
            }
        }
    }
    RoiGroup *thiz_;
    std::map<std::shared_ptr<Roi>, size_t> roiIndexes_;  // Roi快速索引
};

RoiGroup::RoiGroup()
    : impl_(new Impl(this)) {
}

RoiGroup::RoiGroup(const RoiGroup &right)
    : rois_(right.rois_)
    , attitude_(right.attitude_)
    , impl_(new Impl(this)) {
    if(right.impl_)
        impl_->roiIndexes_ = right.impl_->roiIndexes_;
}
    
RoiGroup::RoiGroup(RoiGroup &&right)
    : rois_(std::move(right.rois_))
    , attitude_(std::move(right.attitude_))
    , impl_(std::move(right.impl_)) {
    if (impl_)
        impl_->thiz_ = this;
}

RoiGroup::~RoiGroup() {
}

RoiGroup &RoiGroup::operator=(const RoiGroup &right) {
    RoiGroup temp(right);
    std::swap(this->attitude_, temp.attitude_);
    std::swap(this->rois_, temp.rois_);
    std::swap(this->impl_->roiIndexes_, temp.impl_->roiIndexes_);
    return *this;
}

///< 获取分组的姿态id（格式为: "nid,A,B,C"，nid为数字的标签序号，A,B,C为所有命中的标签）
std::string RoiGroup::getAttitudeId(size_t groupIndex) const {
    // 格式 "nid,A,B,C" 
    // 其中nid为数字的标签序号，A,B,C为所有命中的标签
    std::string nid = fmt::format("{}", groupIndex);

    if (this->attitude_ && this->attitude_->labels_.size() > 0) {
        // 有标签命中时，
        size_t len = nid.length();
        for (size_t i = 0; i < this->attitude_->labels_.size(); ++i) {
            len += 1 + this->attitude_->labels_[i].length();
        }

        std::string id;
        id.reserve(len + 1);

        id = nid;
        for (size_t i = 0; i < this->attitude_->labels_.size(); ++i) {
            id += ",";
            id += this->attitude_->labels_[i];
        }
        return id;
    }
    else {
        return nid;
    }
}

///< 获取roi id（格式为: "rid,text"，rid为roi数组下标，text为roi文本）
std::string RoiGroup::getRoiId(const std::shared_ptr<Roi> &roi) const {
    size_t index = getRoiIndex(roi);
    if (index != impl_->roiIndexes_.size())
        return fmt::format("{},{}", index, roi->text_);
    else
        return fmt::format(",{}", roi->text_);
}

///< 获取roi的数组下标（未找到返回 rois_.size()）
size_t RoiGroup::getRoiIndex(const std::shared_ptr<Roi> &roi) const {
    impl_->flushIndexes();
    auto found = impl_->roiIndexes_.find(roi);
    if (found != impl_->roiIndexes_.end())
        return found->second;
    else
        return impl_->roiIndexes_.size();
}
