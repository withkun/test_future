#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES
#endif
#include "ui/layer_manager.h"
#include "ui/roi.h"
#include "ui/roi/roi_circle.h"
#include "types/attitude.h"
#include "utils/log.h"
#include "utils/qt_utils.h"
#include "utils/cv_utils.h"
#include "utils/var_type.h"
#include "utils/format.h"
#include "promise-cpp/add_ons/qt/promise_qt.hpp"
#include "utils/event_holder.h"
#include "utils/any_pro.h"
#include <QPainter>
#include <QPainterPath>
#include <QStyle>
#include <QKeyEvent>
#include <QStyleOption>
using namespace promise;


AP_REGISTRATION{
    anypro::registration::class_<RoiCircle>()
        .base<Roi>()
        .enumeration<RoiCircle::RadialDirection>().guess_values()
        .property("radialDirection", &RoiCircle::radialDirection_)
        .property("radian", &RoiCircle::radian_)
        .property("center", &RoiCircle::center_)
        .property("radian", &RoiCircle::radian_)
        .property("outerRadius", &RoiCircle::outerRadius_)
        .property("innerRadius", &RoiCircle::innerRadius_)
        .property("pieRadian", &RoiCircle::pieRadian_);
}

class UiRoiCircle::Impl {
public:
    Impl(UiRoiCircle *thiz) : thiz_(thiz) {}
    UiRoiCircle *thiz_;
    std::shared_ptr<RoiCircle>     data_;               // 数据对象
    std::shared_ptr<QPointF>       movingStartPoint_;   // 初始移动起点
    std::shared_ptr<QRectF>        textRect_;           // 文本显示的位置
    EventHolder                    eventHolder_;        // 数据绑定句柄

    bool isOnHotSpot_rotate(const QPointF &pos) const;
    bool isOnHotSpot_center(const QPointF &pos) const;
    bool isOnHotSpot_pie(const QPointF &pos, int *pieIndex) const;
    bool isOnHotSpot_text(const QPointF &pos) const;        // 是否落入标签文字上
    bool isOnOuterBorder(const QPointF &pos, float *radian) const;
    bool isOnInnerBorder(const QPointF &pos, float *radian) const;
    bool isOnRadiusBorder(const QPointF &pos, float *radian) const; // 是否在半径代表的边界上（仅扇形有效）  
    bool isOnBorder(const QPointF &pos, float *radian, double radius) const;
    void showPosition();

    Promise runDragPie(int pieIndex);
    Promise runMove(const cv::Point2f &centerOffset);
};

static double getRotateHandleRadian(RoiCircle &roiCircle) {
    double pieRadian = radianToNormalRadian(roiCircle.pieRadian_);
    if (pieRadian < 270 * M_PI / 180) {
        return 300 * M_PI / 180;
    }
    else {
        return M_PI * 2 - (M_PI * 2 - pieRadian) * 2 / 3;
    }
}


// 圆周位置转成鼠标图标呈现的角度
static Qt::CursorShape radianToDirection(double radian) {
    radian = radianToNormalRadian(radian);
#if 0
    if (radian < M_PI / 8)
        return Qt::SizeHorCursor; // ↔
    else if (radian < M_PI * 3 / 8)
        return Qt::SizeFDiagCursor; // ⤡
    else if (radian < M_PI * 5 / 8)
        return Qt::SizeVerCursor; // ↕
    else if (radian < M_PI * 7 / 8)
        return Qt::SizeBDiagCursor; // ⤢
    else if (radian < M_PI * 9 / 8)
        return Qt::SizeHorCursor; // ↔
    else if (radian < M_PI * 11 / 8)
        return Qt::SizeFDiagCursor; // ⤡
    else if (radian < M_PI * 13 / 8)
        return Qt::SizeVerCursor; // ↕
    else if (radian < M_PI * 15 / 8)
        return Qt::SizeBDiagCursor; // ⤢
    else
        return Qt::SizeHorCursor; // ↔
#else
    if (radian < M_PI / 4)
        return Qt::SizeHorCursor; // ↔
    else if (radian < M_PI * 3 / 4)
        return Qt::SizeVerCursor; // ↕
    else if (radian < M_PI * 5 / 4)
        return Qt::SizeHorCursor; // ↔
    else if (radian < M_PI * 7 / 4)
        return Qt::SizeVerCursor; // ↕
    else
        return Qt::SizeHorCursor; // ↔
#endif
}

static void roiInitTable(RoiCircle *roi) {
    roi->cfgTable_->push_back({ "radialDirection", &roi->radialDirection_ });
    roi->cfgTable_->push_back({ "center",          &roi->center_ });
    roi->cfgTable_->push_back({ "radian",          &roi->radian_ });
    roi->cfgTable_->push_back({ "outerRadius",     &roi->outerRadius_ });
    roi->cfgTable_->push_back({ "innerRadius",     &roi->innerRadius_ });
    roi->cfgTable_->push_back({ "pieRadian",       &roi->pieRadian_ });
}

RoiCircle::RoiCircle()
    : Roi("circle", Roi::kImageArea) {
    roiInitTable(this);
    // 默认大小和位置
    this->radialDirection_ = RoiCircle::RadialDirection::kDirectionInner;
    this->radian_ = (0);
    this->center_ = cv::Point2f(0, 0);
    this->outerRadius_ = (ROI_DEFAULT_VISIABLE_RECT.width() / 2);
    this->innerRadius_ = (ROI_DEFAULT_VISIABLE_RECT.width() / 3);
    this->pieRadian_ = 0;
}

RoiCircle::RoiCircle(const RoiCircle &right)
    : Roi(right) {
    roiInitTable(this);
    this->radialDirection_ = right.radialDirection_;
    this->center_ = right.center_;
    this->radian_ = right.radian_;
    this->outerRadius_ = right.outerRadius_;
    this->innerRadius_ = right.innerRadius_;
    this->pieRadian_ = right.pieRadian_;
}

cv::RotatedRect RoiCircle::minAreaRect() const {
    double radius = std::max(outerRadius_, innerRadius_);
    return cv::RotatedRect(this->center_, cv::Size2f(radius*2, radius*2), radianToAngle(radian_));
}

std::pair<cv::Mat, cv::RotatedRect> RoiCircle::roiImage(const cv::Mat &inputImage, const cv::Scalar &patchBgra, bool tryInPlace, bool isMarginTransparent) const {
    const cv::RotatedRect &rect = minAreaRect();

    auto ret = cvGetCroppedImage(
        inputImage,
        rect,
        patchBgra,
        (tryInPlace || isMarginTransparent) // 补充边界透明时，一定可以先在原图上构建而不影响原图  
    );
    
    auto cropped = ret.first;

    if (cropped.empty() || !isMarginTransparent)
        return ret;

    if (cropped.channels() == 1) {
        cv::cvtColor(cropped, cropped, cv::COLOR_GRAY2BGRA);
    }
    else if (cropped.channels() == 3) {
        cv::cvtColor(cropped, cropped, cv::COLOR_BGR2BGRA);
    }

    cv::Mat transparentImage(cropped.size(), CV_8UC4, cv::Scalar(0, 0, 0, 0));

    // 定义圆参数  
    cv::Point center(cropped.cols / 2, cropped.rows / 2);
    double angle = 0;
    cv::Scalar color(255, 255, 255, 255); // 填充颜色，白色  
    int thickness = -1; // 填充圆  

    // 在透明图像上绘制圆  
    cv::circle(transparentImage, center, cropped.cols / 2.0f, color, thickness);

    // 将原始图像与透明图像进行按位与操作，获取圆区域内的图像
    cv::Mat bitWised;
    cv::bitwise_and(cropped, transparentImage, bitWised);

    // 将圆区域内的图像与透明图像进行混合，将圆区域外的部分用透明色填充
    //cv::Mat resultImage;
    //cv::addWeighted(bitWised, 1.0, transparentImage, 0.0, 0, resultImage);

    return { bitWised, ret.second };
}

// 获取姿态  
Attitude RoiCircle::getAttitude() const {
    Attitude ret;
    ret.score_ = 0;
    ret.radian_ = this->radian_;
    ret.center_ = this->center_;
    return ret;
}

// 获取中心
cv::Point2f RoiCircle::getCenter() const {
    return this->center_;
}

// 判断点是否在Roi区域之内
bool RoiCircle::contains(const cv::Point2f &point) const {
    return cvPointInCircle(point, center_, std::max(outerRadius_, innerRadius_));
}


// 从绑定的姿态位置的区域（从this Roi对象），转成相对于图像的区域
std::shared_ptr<Roi> RoiCircle::attitudeToImage(const Attitude *attitude) const {
    if (attitude == nullptr) {
        // 变量不存在
        return std::dynamic_pointer_cast<Roi>(const_cast<RoiCircle *>(this)->shared_from_this());
    }
    else {
        std::shared_ptr<RoiCircle> ret = std::make_shared<RoiCircle>(*this);
        ret->detachAttitude(attitude);
        return ret;
    }
}

// 从相对于图像的区域，转到绑定的姿态位置的区域get
std::shared_ptr<Roi> RoiCircle::imageToAttitude(const Attitude *attitude) const {
    if (attitude == nullptr) {
        // 变量不存在
        return std::dynamic_pointer_cast<Roi>(const_cast<RoiCircle *>(this)->shared_from_this());
    }
    else {
        std::shared_ptr<RoiCircle> ret = std::make_shared<RoiCircle>(*this);
        ret->attachAttitude(attitude);
        return ret;
    }
}

// 解绑姿态变量（从相对于姿态的坐标，转成图像坐标）
void RoiCircle::detachAttitude(const Attitude *attitude) {
    if (!attitude) return;
    this->center_ = cvAddAttitude(this->center_, *attitude);
    this->radian_ = radianToNormalRadian(this->radian_ + attitude->radian_);
}

// 绑定姿态变量（从图像坐标，转成相对于姿态的坐标）
void RoiCircle::attachAttitude(const Attitude *attitude) {
    if (!attitude) return;
    this->center_ = cvRemoveAttitude(this->center_, *attitude);
    this->radian_ = radianToNormalRadian(this->radian_ - attitude->radian_);
}

void RoiCircle::paint(UiRoi &uiRoi, QPainter &qp, bool isFirstInGroup, bool isDrawHotSpot) {
    //QSizeF widgetSize = qp.clipBoundingRect().size();

    QPointF center = uiRoi.imageToWidget(cv2qt(center_));
    double outerRadius = uiRoi.imageToWidget(outerRadius_);
    double innerRadius = uiRoi.imageToWidget(innerRadius_);
    if (!qIsFinite(center)
        || !qIsFinite(outerRadius)
        || !qIsFinite(innerRadius))
        return;

    qp.setRenderHints(QPainter::Antialiasing);
    qp.save();
    qp.rotate(radian_ * 180 / M_PI);

    double maxRadius = std::max(innerRadius, outerRadius);
    double minRadius = std::min(innerRadius, outerRadius);
    center = qRotatedCoordinate(QPointF(0, 0), center, -radian_);

    const QColor &color = uiRoi.allocateColor(color_);


    // 画圆
    qreal lineWidth = ROI_LINE_WIDTH;
    QPen pen;
    uiRoi.setPenStyle(&pen, color, lineWidth);
    pen.setCapStyle(Qt::PenCapStyle::RoundCap);
    pen.setJoinStyle(Qt::PenJoinStyle::RoundJoin);
    qp.setPen(pen);

    double pieRadian = radianToNormalRadian(this->pieRadian_);
    if (pieRadian == 0) {
        qp.drawEllipse(center, outerRadius, outerRadius);
        qp.drawEllipse(center, innerRadius, innerRadius);
    }
    else {
        double angle = ::fmodu(-pieRadian * 180 / M_PI, 360.0);

        LOG_DEBUG("angle = {}", angle);

        qp.drawArc(QRectF(
            center.x() - minRadius,
            center.y() - minRadius,
            minRadius * 2,
            minRadius * 2
        ), 0 * 16, (int)(angle * 16 + 0.5));
        qp.drawPie(QRectF(
            center.x() - maxRadius,
            center.y() - maxRadius,
            maxRadius * 2,
            maxRadius * 2
        ), 0 * 16, (int)(angle * 16 + 0.5));
    }

    // 画径向方向
    double dirRadian = 0;
    if (pieRadian < 300 * M_PI / 180) {
        dirRadian = 330 * M_PI / 180;
    }
    else {
        dirRadian = M_PI * 2 - (M_PI * 2 - pieRadian) / 2;
    }
    if (getRadialDirection() == RoiCircle::RadialDirection::kDirectionInner) {
        double startRadius = maxRadius;
        double endRaidus = startRadius - ROI_ARROW_LEN;
        QPointF start = { center.x() + startRadius * ::cos(dirRadian),
                          center.y() + startRadius * ::sin(dirRadian) };
        QPointF end = { center.x() + endRaidus * ::cos(dirRadian),
                        center.y() + endRaidus * ::sin(dirRadian) };
        uiRoi.drawArrow(qp,
            start,
            end,
            color);
    }
    else {
        double startRadius = maxRadius;
        double endRaidus = startRadius + ROI_ARROW_LEN;
        QPointF start = { center.x() + startRadius * ::cos(dirRadian),
                          center.y() + startRadius * ::sin(dirRadian) };
        QPointF end = { center.x() + endRaidus * ::cos(dirRadian),
                        center.y() + endRaidus * ::sin(dirRadian) };
        uiRoi.drawArrow(qp,
            start,
            end,
            color);
    }

    // 画中间十字
    if (pieRadian == 0) {
        QColor color1;
        qreal lightness = color.lightnessF();
        lightness = lightness + (1 - lightness) * 0.5;
        color1.setHslF(color.hslHueF(), color.hslSaturationF(), lightness, color.alphaF());

        QPen pen1;
        uiRoi.setPenStyle(&pen1, color1, ROI_LINE_WIDTH); //细线
        pen1.setStyle(Qt::PenStyle::DotLine);
        pen1.setCapStyle(Qt::PenCapStyle::RoundCap);
        pen1.setJoinStyle(Qt::PenJoinStyle::RoundJoin);
        qp.setPen(pen1);
        qp.drawLine(center.x() - maxRadius, center.y(),
            center.x() + maxRadius, center.y());

        QPen pen3;
        uiRoi.setPenStyle(&pen3, color1, ROI_LINE_WIDTH_THINE); //细线
        pen3.setStyle(Qt::PenStyle::DotLine);
        pen3.setCapStyle(Qt::PenCapStyle::RoundCap);
        pen3.setJoinStyle(Qt::PenJoinStyle::RoundJoin);
        qp.setPen(pen3);
        qp.drawLine(center.x(), center.y() - maxRadius,
            center.x(), center.y() + maxRadius);

        // 画箭头方向
        uiRoi.drawArrow(qp,
            QPointF{ center.x() + maxRadius - ROI_ARROW_LEN, center.y() },
            QPointF{ center.x() + maxRadius, center.y() },
            color1);
    }

    // 画热点
    if (isDrawHotSpot && this->isFirstInArray_ && !this->isRef_ && !uiRoi.isReadonly_) {
        QColor color2;
        qreal lightness = color.lightnessF();
        lightness = lightness * 0.9;
        color2.setHslF(color.hslHueF(), color.hslSaturationF(), lightness, color.alphaF());

        QBrush brush2(color2);
        QPen pen2;
        uiRoi.setPenStyle(&pen2, color2, lineWidth);
        qp.setPen(pen2);
        qp.setBrush(brush2);
        qreal radius = ROI_HOTSPOT_RADIUS;
        // 画中心点
        qp.drawEllipse(center, radius, radius);

        // 画扇形拖拽点  
        QPointF piePos = { center.x() + maxRadius * ::cos(pieRadian_),
                            center.y() + maxRadius * ::sin(pieRadian_) };
        qp.drawEllipse(piePos, radius, radius);
        if (pieRadian_ != 0) {
            QPointF piePos = { center.x() + maxRadius * ::cos(0),
                            center.y() + maxRadius * ::sin(0) };
            qp.drawEllipse(piePos, radius, radius);
        }

        // 旋转点
        double rotateRadian = getRotateHandleRadian(*this);
        QPointF rotateCenter = { center.x() + maxRadius * ::cos(rotateRadian),
                                 center.y() + maxRadius * ::sin(rotateRadian) };
        uiRoi.drawRotateHandler(qp, rotateCenter, color2, true, rotateRadian);

        //qp.drawEllipse(rotateCenter, radius, radius);

        // 旋转点鼠标感应区域
        //double rotateRadius = maxRadius + (ROI_HOTSPOT_DISTANCE);
        //QPointF rotatePoint = { center.x() + rotateRadius * ::cos(rotateRadian),
        //                        center.y() + rotateRadius * ::sin(rotateRadian) };
        //qreal r = (ROI_HOTSPOT_RADIUS + ROI_MASK_MARGIN);
        //qp.drawEllipse(rotatePoint, r, r);
    }


    // 画标签文字
    if (!this->text_->empty()) {
        QPen penText(color);
        qp.setPen(penText);
        QString text = QString::fromStdString(this->text_);
        QFont font = qp.font();

        // 评估文字大小
        int minFontSize = ROI_LABEL_FONT_MIN_SIZE;
        int maxFontSize = ROI_LABEL_FONT_MAX_SIZE;
        //int fontSize = maxFontSize;
        QRect textRect;
        while (minFontSize + 1 < maxFontSize) {
            int testFontSize = (minFontSize + maxFontSize) / 2;

            font.setPointSize(testFontSize);
            qp.setFont(font);
            const QFontMetrics &fm = qp.fontMetrics();
            textRect = fm.boundingRect(text);
            if (textRect.width() + 18 < maxRadius * 2
                && textRect.height() + 4 < maxRadius * 2) {
                minFontSize = testFontSize;
            }
            else {
                maxFontSize = testFontSize;
            }
        }
        LOG_DEBUG("textRect = {}", textRect);

        QPointF point = { center.x() + maxRadius - ROI_LABEL_MARGIN * 2 - textRect.width(), center.y() - textRect.height() - ROI_LABEL_MARGIN };
        QSize textSize = { textRect.width() + 8, textRect.height() };
        textRect = QRect{ point.toPoint(), textSize };
        // 位置保存到roi对象
        UiRoiCircle *roiCircle = dynamic_cast<UiRoiCircle *>(&uiRoi);
        if (roiCircle != nullptr && isFirstInGroup) {
            if (roiCircle->impl_->textRect_)
                *roiCircle->impl_->textRect_ = textRect;
            else
                roiCircle->impl_->textRect_ = std::make_shared<QRectF>(textRect);
        }

        LOG_DEBUG("textRect = {}", textRect);
        //qp.drawRect(textRect);
        uiRoi.drawHollowFont(qp, font, text, textRect, color);
        //qp.setClipRect(textRect, Qt::ClipOperation::IntersectClip);
    }


    qp.restore();
}

bool RoiCircle::equals(const Roi& other) const {
    RoiCircle *otherCircle = dynamic_cast<RoiCircle *>(const_cast<Roi*>(&other));
    if (otherCircle == nullptr) {
        return false;
    }
    return this->radialDirection_ == otherCircle->radialDirection_
        && this->radian_ == otherCircle->radian_
        && this->center_ == otherCircle->center_
        && this->outerRadius_ == otherCircle->outerRadius_
        && this->innerRadius_ == otherCircle->innerRadius_
        && this->pieRadian_ == otherCircle->pieRadian_;
}

// 获取圆的径向方向
RoiCircle::RadialDirection RoiCircle::getRadialDirection() const {
#if 1
    // 使用明确的方向
    return radialDirection_;
#else
    if (innerRadius_ < outerRadius_)
        return RoiCircle::RadialDirection::kDirectionInner;
    else
        return RoiCircle::RadialDirection::kDirectionOuter;
#endif
}

static std::shared_ptr<Roi> insureRoi(const std::shared_ptr<Roi> &roi) {
    if (roi) return roi;

    auto data = std::make_shared<RoiCircle>();
    return data;
}


UiRoiCircle::UiRoiCircle(const std::shared_ptr<Roi> &roi)
    : UiRoi(insureRoi(roi))
    , impl_(new Impl(this)) {

    auto updateWidget = [=](BindingData::EventType eventType, const boost::any &eventValue, const boost::any &src) {
        impl_->data_->triggerEvent(eventType, eventValue, (src.empty() ? impl_->data_->triggerSource() : src));
        this->update();
    };

    impl_->data_ = std::dynamic_pointer_cast<RoiCircle>(data_);
    impl_->eventHolder_ += bindDataToObject(static_cast<BindingData *>(this), static_cast<BindingData *>(&impl_->data_->radialDirection_), [=]() {}, updateWidget);
    impl_->eventHolder_ += bindDataToObject(static_cast<BindingData *>(this), static_cast<BindingData *>(&impl_->data_->center_), [=]() {}, updateWidget);
    impl_->eventHolder_ += bindDataToObject(static_cast<BindingData *>(this), static_cast<BindingData *>(&impl_->data_->radian_), [=]() {}, updateWidget);
    impl_->eventHolder_ += bindDataToObject(static_cast<BindingData *>(this), static_cast<BindingData *>(&impl_->data_->outerRadius_), [=]() {}, updateWidget);
    impl_->eventHolder_ += bindDataToObject(static_cast<BindingData *>(this), static_cast<BindingData *>(&impl_->data_->innerRadius_), [=]() {}, updateWidget);
    impl_->eventHolder_ += bindDataToObject(static_cast<BindingData *>(this), static_cast<BindingData *>(&impl_->data_->pieRadian_), [=]() {}, updateWidget);
    impl_->eventHolder_ += bindDataToObject(static_cast<BindingData *>(this), static_cast<BindingData *>(&impl_->data_->color_), [=]() {}, updateWidget);
    impl_->eventHolder_ += bindDataToObject(static_cast<BindingData *>(this), static_cast<BindingData *>(&impl_->data_->text_), [=]() {}, updateWidget);
}

UiRoiCircle::~UiRoiCircle() {
    impl_->eventHolder_.clear();
}

bool UiRoiCircle::isOnObject(const QPointF &pos, Qt::CursorShape &mouseDirection) {
    std::shared_ptr<LayerManager> layerManager = layerManager_.lock();
    if (!layerManager || layerManager->isLayerToolDisabled(this))
        return false;

    const auto &data = impl_->data_;
    if (data->hasEmptyAttitudes())
        return false;

    QPointF pos2 = qRotatedCoordinate(cv2qt(data->center_), pos, -data->radian_);
    float radian = 0;

    // 是否初始移动
    if (impl_->movingStartPoint_) {
        mouseDirection = Qt::CrossCursor;
        return true;
    }
    
    // 判断是否落在扇形拖拽点区域
    if (impl_->isOnHotSpot_pie(pos2, nullptr)) {
        mouseDirection = Qt::OpenHandCursor;
    }
    // 判断是否落在圆周区域（先判断圆周避免缩放到很小没法再放大）
    else if (impl_->isOnOuterBorder(pos2, &radian)
        || impl_->isOnInnerBorder(pos2, &radian)) {
        mouseDirection = radianToDirection(radian + data->radian_);
    }
    // 判断是否落在旋转热点区域
    else if (impl_->isOnHotSpot_rotate(pos2)) {
        mouseDirection = Qt::OpenHandCursor;
    }
    // 判断是否落在中心热点区域
    else if (impl_->isOnHotSpot_center(pos2)) {
        mouseDirection = Qt::SizeAllCursor;
    }
    else if (impl_->isOnHotSpot_text(pos2)) {
        mouseDirection = Qt::IBeamCursor;
    }
    else if (impl_->isOnRadiusBorder(pos2, &radian)) {
        //mouseDirection = radianToDirection(radian + data->radian_);
        mouseDirection = Qt::SizeAllCursor;
    }
    else {
        mouseDirection = Qt::ArrowCursor;
        return false;
    }
    return true;
}

void UiRoiCircle::paint(QPainter &qp, bool isDrawHotSpot) {
    if (!impl_->data_ || !impl_->data_->isInitialized_)
        return;
    const std::vector<std::shared_ptr<Roi>> &rois = impl_->data_->getExtRoiByAttitude();

    bool isFirst = true;
    for (const std::shared_ptr<Roi> &roi : rois) {
        // (优化) 借用下本对象，临时替换克隆对象的数据
        auto layerTool = this->data_;
        this->data_ = roi;
        OnScopeExit onScopeExit([&] {this->data_ = layerTool; });

        // 绘画本组roi
        roi->isFirstInArray_ = isFirst;
        roi->paint(*this, qp,
            isFirst,
            (isFirst ? (isDrawHotSpot && !impl_->movingStartPoint_) : false));
        isFirst = false;
    }
}


bool UiRoiCircle::Impl::isOnHotSpot_rotate(const QPointF &pos) const {
    const auto &data = data_;
    double maxRadius = std::max(data->innerRadius_, data->outerRadius_);

    double rotateRadian = getRotateHandleRadian(*data);
    double rotateRadius = maxRadius + thiz_->widgetToImage(ROI_HOTSPOT_DISTANCE);

    QPointF rotatePoint = { data->center_->x + rotateRadius * ::cos(rotateRadian),
                            data->center_->y + rotateRadius * ::sin(rotateRadian) };
    qreal r = thiz_->widgetToImage(ROI_HOTSPOT_RADIUS + ROI_MASK_MARGIN);
    if (qDistance2(pos, rotatePoint) <= r * r)
        return true;
    else
        return false;
}

bool UiRoiCircle::Impl::isOnHotSpot_center(const QPointF &pos) const {
    const auto &data = data_;
    qreal r = thiz_->widgetToImage(ROI_HOTSPOT_RADIUS + ROI_MASK_MARGIN);
    if (qDistance2(pos, cv2qt(data->center_)) <= r * r)
        return true;
    else
        return false;
}

bool UiRoiCircle::Impl::isOnHotSpot_pie(const QPointF &pos, int *pieIndex) const {
    const auto &data = data_;
    qreal r = thiz_->widgetToImage(ROI_HOTSPOT_RADIUS + ROI_MASK_MARGIN);

    // 判断可视大小是否比热点区域还大，否则的画，热点应该优先在边框上（避免太小无法拖大边框）  
    double maxRadius = std::max(data->innerRadius_, data->outerRadius_);
    bool isBigThanHotSpot = (maxRadius > r);
    if (!isBigThanHotSpot)
        return false;

    QPointF piePos1 = { data->center_->x + maxRadius * ::cos(data->pieRadian_),
        data->center_->y + maxRadius * ::sin(data->pieRadian_) };
    LOG_DEBUG("piePos = {}, {}", piePos1, pos);

    if (qDistance2(pos, piePos1) <= r * r) {
        if (pieIndex != nullptr) *pieIndex = 1;
        return true;
    }

    QPointF piePos0 = { data->center_->x + maxRadius * ::cos(0),
        data->center_->y + maxRadius * ::sin(0) };
    LOG_DEBUG("piePos = {}, {}", piePos0, pos);

    if (qDistance2(pos, piePos0) <= r * r) {
        if (pieIndex != nullptr) *pieIndex = 0;
        return true;
    }

    if (pieIndex != nullptr) *pieIndex = -1;
    return false;
}

bool UiRoiCircle::Impl::isOnHotSpot_text(const QPointF &pos) const {

    if (!this->textRect_)
        return false;

    const auto &data = this->data_;
    // 注意 textRect_ 相对于窗口的旋转后的坐标

    // 相对于窗口摆正后的坐标
    QPointF textPos = qRotatedCoordinate(QPointF(0, 0), this->textRect_->topLeft(), data->radian_);
    // 相对于原图摆正后的坐标
    textPos = thiz_->widgetToImage(textPos);

    // 相对于roi摆正后的坐标
    textPos = qRotatedCoordinate(cv2qt(data->center_), textPos, -data->radian_);

    //QPointF pos2 = qRotatedCoordinate(cv2qt(data->center_), pos, data->radian_);
    QRectF rextRect = { textPos, thiz_->widgetToImage(this->textRect_->size()) };

    LOG_DEBUG("textRect_ = {}, {}", rextRect, pos);
    LOG_DEBUG("rextRect.isOnObject(pos) = {}", rextRect.contains(pos));

    if (rextRect.contains(pos))
        return true;

    return false;
}

bool UiRoiCircle::Impl::isOnOuterBorder(const QPointF &pos, float *radian) const {
    const auto &data = data_;
    return isOnBorder(pos, radian, data->outerRadius_);
}

bool UiRoiCircle::Impl::isOnInnerBorder(const QPointF &pos, float *radian) const {
    const auto &data = data_;
    return isOnBorder(pos, radian, data->innerRadius_);
}

bool UiRoiCircle::Impl::isOnRadiusBorder(const QPointF &pos, float *radian) const {
    const auto &data = data_;
    if (data->pieRadian_ == 0)
        return false;

    double maxRadius = std::max(data->innerRadius_, data->outerRadius_);
    double hotSpotRadius = thiz_->widgetToImage(ROI_HOTSPOT_RADIUS + ROI_MASK_MARGIN);
    cv::Size2f size(maxRadius, hotSpotRadius);
    cv::Rect2f rect{
        data->center_->x,
        data->center_->y - size.height / 2,
        size.width,
        size.height
    };

    const cv::Point2f &pos1 = qt2cv(pos);
    const cv::Point2f &pos2 = cvRotatedCoordinate(data->center_, pos1, -data->pieRadian_);

    bool match1 = rect.contains(pos1);
    if (match1 && radian != nullptr) {
        // 计算角度
        *radian = 0;
    }

    bool match2 = rect.contains(pos2);
    if (match1 && radian != nullptr) {
        // 计算角度
        *radian = data->pieRadian_;
    }

    LOG_DEBUG("match = {}, {}", match1, match2);

    return match1 || match2;
}


bool UiRoiCircle::Impl::isOnBorder(const QPointF &pos, float *radian, double r) const {
    const auto &data = data_;
    //  落入大圆不落入小圆内
    double halfBorder = thiz_->widgetToImage(ROI_MASK_MARGIN + ROI_LINE_WIDTH/2);
    bool inBigEllipse = qPointInEllipse(pos, cv2qt(data->center_), r + halfBorder, r + halfBorder);
    bool inSmallEllipse = qPointInEllipse(pos, cv2qt(data->center_), std::max(r - halfBorder, 0.), std::max(r - halfBorder, 0.));
    bool matched = (inBigEllipse && !inSmallEllipse);

    double radian2 = atan2(pos.y() - data->center_->y, pos.x() - data->center_->x);
    radian2 = radianToNormalRadian(radian2);

    double pieRadian = radianToNormalRadian(data->pieRadian_);

    LOG_DEBUG("pieRadian = {}, radian2 = {}", pieRadian, radian2);
    if (pieRadian > 0 && radian2 < pieRadian)
        matched = false;

    if (matched && radian != nullptr) {
        // 计算角度
        *radian = (float)radian2;
    }

    return matched;
}

void UiRoiCircle::Impl::showPosition() {
    std::string text = fmt::format("center={:.3f}, radius={:.3f} ~ {:.3f}, angle={:.3f}, direction={}",
        data_->center_,
        data_->innerRadius_,
        data_->outerRadius_,
        radianToAngle(data_->radian_),
        (data_->getRadialDirection() == RoiCircle::kDirectionInner ? "<-" : "->")
        );
   
    thiz_->showStatusMessage(QString::fromStdString(text), 3000);
}

promise::Promise UiRoiCircle::runInitialize() {
    std::shared_ptr<LayerManager> layerManager = layerManager_.lock();
    this->widget()->grabMouse(Qt::CrossCursor);

    impl_->data_->radialDirection_ = RoiCircle::kDirectionInner;
    impl_->data_->outerRadius_ = (0);
    impl_->data_->innerRadius_ = (0);
    impl_->data_->center_ = cv::Point2f(0, 0);
    impl_->data_->pieRadian_ = 0;
    impl_->movingStartPoint_ = std::make_shared<QPointF>(cv2qt(impl_->data_->center_));
    layerManager->setFocusedLayerTool(this);

    // 设定移动中的顶点

    // 等待左键点击
    return pendingTask_ = doWhile(PM_LOC, [=](const DeferLoop &loop) {
        PTD;
        // 鼠标移动，根据位置设置鼠标形状
        auto onMoving = [=](QEvent *event) {
            PTD;
            const QPointF &pos = widgetToImage(getMousePos(event));
            auto onScopeExit = impl_->data_->scopedTriggerSource(Widget::kFromConfigRois);
            impl_->data_->center_ = qt2cv(pos);
            this->update();

            // 设定移动中的顶点
            this->widget()->setCursor(Qt::CrossCursor);
            return loop.doContinue(PM_LOC); // will goto doWhile
        };
        auto waitHoverMove = layerManager->waitEvent(this, QEvent::HoverMove).then(PM_LOC, onMoving);
        auto waitMouseMove = layerManager->waitEvent(this, QEvent::MouseMove).then(PM_LOC, onMoving);

        auto waitMouseButtonPress = layerManager->waitEvent(this, QEvent::MouseButtonPress).then(PM_LOC, [=](QEvent *event) {
            PTD;
            QMouseEvent *mouseEvent = dynamic_cast<QMouseEvent *>(event);
            if (!mouseEvent) return loop.doContinue(PM_LOC); // will goto doWhile

            if (mouseEvent->buttons().testFlag(Qt::LeftButton)) {
                // 检测鼠标位置落在控件之内
                QPoint pos = getMousePos(event);
                bool isOnWidget = this->widget()->rect().contains(pos);
                if(!isOnWidget) return loop.doContinue(PM_LOC); // will goto doWhile
                else            return loop.doBreak(PM_LOC, mouseEvent);
            }
            else return loop.doBreak(PM_LOC, mouseEvent);
        });

        // 以上几个事件只处理其中任一个
        raceAndReject2nd(loop, { waitHoverMove, waitMouseMove, waitMouseButtonPress });
    })
    
    // 判断鼠标事件类型
    .then(PM_LOC, [=](QMouseEvent *mouseEvent) {
        PTD;
        if (mouseEvent->buttons().testFlag(Qt::LeftButton)) {
            auto onScopeExit = impl_->data_->scopedTriggerSource(Widget::kFromConfigRois);
            impl_->movingStartPoint_ = std::make_shared<QPointF>(widgetToImage(mouseEvent->pos()));
            impl_->data_->isInitialized_ = true;
            impl_->data_->center_ = qt2cv(*impl_->movingStartPoint_);
            impl_->data_->outerRadius_ = (0);
            impl_->data_->innerRadius_ = (0);
            this->widget()->setCursor(Qt::SizeFDiagCursor);
            impl_->showPosition();
            this->update();
        }
        else {
            // 取消ROI
            throw mouseEvent;
        }
    }).

    // 不断等待更多鼠标事件
    then(PM_LOC, [=]() {
        PTD;
        // 设定鼠标独占状态
        layerManager->setFocusedLayerTool(this);

        return doWhile(PM_LOC, [=](const DeferLoop &loop) {
            // 鼠标移动，根据位置设置鼠标形状
            auto onMoving = [=](QEvent *event) {
                const QPointF &pos = widgetToImage(getMousePos(event));

                // 设定移动中的顶点
                auto onScopeExit = impl_->data_->scopedTriggerSource(Widget::kFromConfigRois);
                impl_->data_->center_ = qt2cv((pos + *impl_->movingStartPoint_) / 2);
                impl_->data_->outerRadius_ = (qDistance(pos, cv2qt(impl_->data_->center_)));
                impl_->data_->innerRadius_ = (impl_->data_->outerRadius_ * 2./3.); // 默认内圈大小取外圈的2/3

                Qt::CursorShape cursorShape = radianToDirection(::atan2(pos.y() - impl_->data_->center_->y, pos.x() - impl_->data_->center_->x));
                this->widget()->setCursor(cursorShape);
                impl_->showPosition();
                this->update();
                return loop.doContinue(PM_LOC); // will goto doWhile
            };
            auto waitHoverMove = layerManager->waitEvent(this, QEvent::HoverMove).then(PM_LOC, onMoving);
            auto waitMouseMove = layerManager->waitEvent(this, QEvent::MouseMove).then(PM_LOC, onMoving);

            // 鼠标单击事件处理
            auto onClicked = [=](QEvent *event) {
                QMouseEvent *mouseEvent = dynamic_cast<QMouseEvent *>(event);
                if (!mouseEvent) return loop.doContinue(PM_LOC); // will goto doWhile
                QPointF pos = widgetToImage(mouseEvent->pos());

                auto onScopeExit = impl_->data_->scopedTriggerSource(Widget::kFromConfigRois);
                impl_->data_->center_ = qt2cv((pos + *impl_->movingStartPoint_) / 2);
                impl_->data_->outerRadius_ = qDistance(pos, cv2qt(impl_->data_->center_));
                if (impl_->data_->outerRadius_ == 0) impl_->data_->outerRadius_ = (ROI_DEFAULT_MIN_SIZE / 2.0);
                impl_->data_->innerRadius_ = (impl_->data_->outerRadius_ * 2./3.); // 默认内圈大小取外圈的2/3
                impl_->movingStartPoint_.reset();

                impl_->showPosition();
                this->update();
                loop.doBreak(PM_LOC);
            };
            auto waitMouseButtonRelease = layerManager->waitEvent(this, QEvent::MouseButtonRelease).then(PM_LOC, onClicked);
            auto waitMouseButtonPress = layerManager->waitEvent(this, QEvent::MouseButtonPress).then(PM_LOC, onClicked);
            auto waitMouseButtonDblClick = layerManager->waitEvent(this, QEvent::MouseButtonDblClick).then(PM_LOC, onClicked);

            // 以上几个事件只处理其中任一个
            raceAndReject2nd(loop, { waitHoverMove, waitMouseMove, waitMouseButtonRelease, waitMouseButtonPress, waitMouseButtonDblClick });
        });
    }).

    // 取消事件独占状态
    finally(PM_LOC, [=]() {
        PTD;
        this->widget()->releaseMouse();
        this->widget()->setCursor(Qt::ArrowCursor);
        layerManager->setFocusedLayerTool(nullptr);
        layerManager->clearInitialingTask();
    }).

    // 转到可编辑状态
    then(PM_LOC, [=]() {
        PTD;
        this->triggerEvent(BindingData::kInit);
        return runWorkaroundMaskCursor(Qt::ArrowCursor);
    }).then(PM_LOC, [=](){
        PTD;
        return this->runEdit();
    }).

    // 状态机结束处理
    finally(PM_LOC, [=]() {
        // TODO
        PTD;
        layerManager->remove(this);
        pendingTask_.clear();
    });
}

Promise UiRoiCircle::runEdit() {
    std::shared_ptr<LayerManager> layerManager = layerManager_.lock();
    return pendingTask_ = (data_->isRef_ || this->isReadonly_)
        ? promise::newPromise(PM_LOC)
        : doWhile(PM_LOC, [=](const DeferLoop &loop) {
        // 鼠标移动，根据位置设置鼠标形状
        auto onMoving = [=](QEvent *event) {
            const QPointF &pos = widgetToImage(getMousePos(event));

            // 判断是否落在圆周区域（先判断圆周避免缩放到很小没法再放大）
            Qt::CursorShape mouseDirection = Qt::ArrowCursor;
            if (isOnObject(pos, mouseDirection))
                this->widget()->setCursor(mouseDirection);

            return loop.doContinue(PM_LOC); // will goto doWhile
        };
        auto waitHoverMove = layerManager->waitEvent(this, QEvent::HoverMove).then(PM_LOC, onMoving);
        auto waitMouseMove = layerManager->waitEvent(this, QEvent::MouseMove).then(PM_LOC, onMoving);

        // 鼠标点击，根据位置判断进入其他状态
        auto waitMouseButtonPress = layerManager->waitEvent(this, QEvent::MouseButtonPress).then(PM_LOC, [=](QEvent *event) {
            if (getData()->hasEmptyAttitudes())
                return loop.doContinue(PM_LOC); // 空姿态就不要操作了

            QMouseEvent *mouseEvent = dynamic_cast<QMouseEvent *>(event);
            if (!mouseEvent) return loop.doContinue(PM_LOC); // will goto doWhile

            // 获取姿态调整后的位置
            const std::shared_ptr<RoiCircle> &data = impl_->data_;
            QPointF pos = qRotatedCoordinate(cv2qt(data->center_), widgetToImage(mouseEvent->pos()), -data->radian_);

            bool isOnOuterBorder = false;
            bool isOnInnerBorder = false;
            int pieIndex = 0;
            bool isOnHotSpotPie = impl_->isOnHotSpot_pie(pos, &pieIndex);
            if (isOnHotSpotPie) {
                layerManager->setFocusedLayerTool(this);
                impl_->runDragPie(pieIndex).finally(PM_LOC, [=]() {layerManager->setFocusedLayerTool(nullptr); }).then(PM_LOC, loop);
                return;
            }
            // 判断是否落在圆周区域（先判断圆周避免缩放到很小没法再放大）
            else {
                float radian = 0;
                isOnOuterBorder = impl_->isOnOuterBorder(pos, &radian);
                isOnInnerBorder = impl_->isOnInnerBorder(pos, &radian);
                if (isOnOuterBorder || isOnInnerBorder) {
                    if (mouseEvent->buttons().testFlag(Qt::RightButton)) {
                        runContextMenu(mouseEvent->pos(), {}).then(PM_LOC, loop);
                        return;
                    }
                    else if (mouseEvent->buttons().testFlag(Qt::LeftButton)) {
                        auto mouseDirection = radianToDirection(radian + data->radian_);
                        layerManager->setFocusedLayerTool(this);
                        runResize(mouseDirection, isOnOuterBorder).finally(PM_LOC, [=]() {layerManager->setFocusedLayerTool(nullptr); }).then(PM_LOC, loop);
                        return;
                    }
                }
            }

            if (isOnHotSpotPie || isOnOuterBorder || isOnInnerBorder) {
                // 此处代码，上面已经处理
            }
            // 判断是否落在热点区域
            else if (impl_->isOnHotSpot_rotate(pos)) {
                layerManager->setFocusedLayerTool(this);
                runRotate().finally(PM_LOC, [=]() {layerManager->setFocusedLayerTool(nullptr); }).then(PM_LOC, loop);
                return;
            }
            // 判断是否落在中心热点区域
            else if (impl_->isOnHotSpot_center(pos)
                ) {
                if (mouseEvent->buttons().testFlag(Qt::RightButton)) {
                    runContextMenu(mouseEvent->pos(), {}).then(PM_LOC, loop);
                    return;
                }
                else if (mouseEvent->buttons().testFlag(Qt::LeftButton)) {
                    layerManager->setFocusedLayerTool(this);

                    cv::Point2f centerOffset = qt2cv(pos) - data->center_;
                    centerOffset = cvRotatedCoordinate(cv::Point2f(0, 0), centerOffset, data->radian_);
                    impl_->runMove(centerOffset).finally(PM_LOC, [=]() {layerManager->setFocusedLayerTool(nullptr); }).then(PM_LOC, loop);
                    return;
                }
            }
            // 判断是否落在文字上
            else if (impl_->isOnHotSpot_text(pos)) {
                if (mouseEvent->buttons().testFlag(Qt::RightButton)) {
                    runContextMenu(mouseEvent->pos(), {}).then(PM_LOC, loop);
                    return;
                }
                else if (mouseEvent->buttons().testFlag(Qt::LeftButton)) {
                    layerManager->setFocusedLayerTool(this);
                    runModifyText(mouseEvent->pos()).finally(PM_LOC, [=]() {layerManager->setFocusedLayerTool(nullptr); }).then(PM_LOC, loop);
                    return;
                }
            }
            // 判断是否落在扇形半径上
            else if (impl_->isOnRadiusBorder(pos, nullptr)) {
                if (mouseEvent->buttons().testFlag(Qt::RightButton)) {
                    runContextMenu(mouseEvent->pos(), {}).then(PM_LOC, loop);
                    return;
                }
                else if (mouseEvent->buttons().testFlag(Qt::LeftButton)) {
                    layerManager->setFocusedLayerTool(this);

                    cv::Point2f centerOffset = qt2cv(pos) - data->center_;
                    centerOffset = cvRotatedCoordinate(cv::Point2f(0, 0), centerOffset, data->radian_);
                    impl_->runMove(centerOffset).finally(PM_LOC, [=]() {layerManager->setFocusedLayerTool(nullptr); }).then(PM_LOC, loop);
                    return;
                }
            }

            this->widget()->setCursor(Qt::ArrowCursor);
            return loop.doContinue(PM_LOC); // will goto doWhile
        });

        // 以上几个事件只处理其中任一个
        raceAndReject2nd(loop, { waitHoverMove, waitMouseMove, waitMouseButtonPress });
    }).

    // 状态机结束处理
    finally(PM_LOC, [=]() {
        // TODO
        PTD;
        layerManager->remove(this);
        pendingTask_.clear();
    });

}

Promise UiRoiCircle::Impl::runDragPie(int pieIndex) {
    std::shared_ptr<LayerManager> layerManager = thiz_->layerManager_.lock();

    // 旋转状态
    return doWhile(PM_LOC, [=](const DeferLoop &loop) {
        // 抓住鼠标
        thiz_->widget()->grabMouse(Qt::ClosedHandCursor);

        // 鼠标移动，旋转本组件
        auto waitMouseMove = layerManager->waitEvent(thiz_, QEvent::MouseMove).then(PM_LOC, [=](QEvent *event) {
            QMouseEvent *mouseEvent = dynamic_cast<QMouseEvent *>(event);
            if (!mouseEvent) return loop.doContinue(PM_LOC); // will goto doWhile

            const QPointF &pos = thiz_->widgetToImage(mouseEvent->pos());
            LOG_DEBUG("mouse = {}\n", mouseEvent->pos());

            auto onScopeExit = this->data_->scopedTriggerSource(Widget::kFromConfigRois);

            // 获取姿态调整后的位置
            //const std::shared_ptr<Attitude> &attitude = impl_->data_->getAttitude0();
            const std::shared_ptr<RoiCircle> &data = this->data_;
            double radian = ::atan2(pos.y() - data->center_->y, pos.x() - data->center_->x);

            if (pieIndex == 0) {
                // 拖拽的第0个点（和中心点持平的）  
                double diff = radian - data->radian_;
                data->radian_ = radian;
                data->pieRadian_ = radianToNormalRadian(data->pieRadian_ - diff);
            }
            else {
                // 拖拽的第1个点（和pieRadian_关联的点）  
                radian = radianToNormalRadian(radian - data->radian_);
                data->pieRadian_ = radian;
            }

            // 转换为不含姿态调整的位置
            //data->imageToAttitude(data, attitude.get());
            //QPointF pos = qRotatedCoordinate(data->center_, mouseEvent->pos(), -data->radian_);

            layerManager->notifyMove(thiz_);
            this->showPosition();
            thiz_->update();

            return loop.doContinue(PM_LOC);
        });

        // 鼠标松开
        auto waitMouseButtonRelease = layerManager->waitEvent(thiz_, QEvent::MouseButtonRelease).then(PM_LOC, [=](QEvent *event) {
            return loop.doBreak(PM_LOC);
        });

        // 两个事件只处理其中任一个
        raceAndReject2nd(loop, { waitMouseMove, waitMouseButtonRelease });
    }).finally(PM_LOC, [=]() {
        thiz_->widget()->releaseMouse();
        layerManager->notifyMove(thiz_);
    });
}

Promise UiRoiCircle::runRotate() {
    std::shared_ptr<LayerManager> layerManager = layerManager_.lock();
    // 旋转状态
    return doWhile(PM_LOC, [=](const DeferLoop &loop) {
        // 抓住鼠标
        this->widget()->grabMouse(Qt::ClosedHandCursor);

        // 鼠标移动，旋转本组件
        auto waitMouseMove = layerManager->waitEvent(this, QEvent::MouseMove).then(PM_LOC, [=](QEvent *event) {
            QMouseEvent *mouseEvent = dynamic_cast<QMouseEvent *>(event);
            if (!mouseEvent) return loop.doContinue(PM_LOC); // will goto doWhile

            const QPointF &pos = widgetToImage(mouseEvent->pos());
            LOG_DEBUG("mouse = {}\n", mouseEvent->pos());

            auto onScopeExit = this->data_->scopedTriggerSource(Widget::kFromConfigRois);

            // 获取姿态调整后的位置
            //const std::shared_ptr<Attitude> &attitude = impl_->data_->getAttitude0();
            const std::shared_ptr<RoiCircle> &data = impl_->data_;

            double rotateRadian = getRotateHandleRadian(*data);
            double radian = (::atan2(pos.y() - data->center_->y, pos.x() - data->center_->x) - rotateRadian);
            radian = radianToNormalRadian(radian);
            data->radian_ = radian;

            // 转换为不含姿态调整的位置
            //data->imageToAttitude(data, attitude.get());
            //QPointF pos = qRotatedCoordinate(data->center_, mouseEvent->pos(), -data->radian_);

            layerManager->notifyMove(this);
            impl_->showPosition();
            this->update();
            
            return loop.doContinue(PM_LOC);
        });

        // 鼠标松开
        auto waitMouseButtonRelease = layerManager->waitEvent(this, QEvent::MouseButtonRelease).then(PM_LOC, [=](QEvent *event) {
            return loop.doBreak(PM_LOC);
        });

        // 两个事件只处理其中任一个
        raceAndReject2nd(loop, { waitMouseMove, waitMouseButtonRelease });
    }).finally(PM_LOC, [=]() {
        this->widget()->releaseMouse();
        layerManager->notifyMove(this);
    });
}

Promise UiRoiCircle::Impl::runMove(const cv::Point2f &centerOffset) {
    std::shared_ptr<LayerManager> layerManager = thiz_->layerManager_.lock();
    // 移动状态
    return doWhile(PM_LOC, [=](const DeferLoop &loop) {
        // 抓住鼠标
        thiz_->widget()->grabMouse(Qt::SizeAllCursor);

        // 鼠标移动，移动本组件
        auto waitMouseMove = layerManager->waitEvent(thiz_, QEvent::MouseMove).then(PM_LOC, [=](QEvent *event) {
            QMouseEvent *mouseEvent = dynamic_cast<QMouseEvent *>(event);
            if (!mouseEvent) return loop.doContinue(PM_LOC); // will goto doWhile

            const QPointF &pos = thiz_->widgetToImage(mouseEvent->pos());

            auto onScopeExit = this->data_->scopedTriggerSource(Widget::kFromConfigRois);

            // 获取姿态调整后的位置
            //const std::shared_ptr<Attitude> &attitude = impl_->data_->getAttitude0();
            const std::shared_ptr<RoiCircle> &data = this->data_;
            data->center_ = qt2cv(pos) - centerOffset;
            // 转换为不含姿态调整的位置
            //data->imageToAttitude(data, attitude.get());

            layerManager->notifyMove(thiz_);
            this->showPosition();
            thiz_->update();

            return loop.doContinue(PM_LOC);
        });

        // 鼠标松开
        auto waitMouseButtonRelease = layerManager->waitEvent(thiz_, QEvent::MouseButtonRelease).then(PM_LOC, [=](QEvent *event) {
            return loop.doBreak(PM_LOC);
        });

        // 两个事件只处理其中任一个
        raceAndReject2nd(loop, { waitMouseMove, waitMouseButtonRelease });
    }).finally(PM_LOC, [=]() {
        thiz_->widget()->releaseMouse();
        layerManager->notifyMove(thiz_);
    });
}

Promise UiRoiCircle::runMove() {
    return impl_->runMove(cv::Point2f(0, 0));
}

Promise UiRoiCircle::runResize(const Qt::CursorShape &mouseDirection, bool isOnOuterBorder) {
    std::shared_ptr<LayerManager> layerManager = layerManager_.lock();
    // 尺寸拉伸状态
    std::shared_ptr<bool> sIsOnOuterBorder = std::make_shared<bool>(isOnOuterBorder);
    return doWhile(PM_LOC, [=](const DeferLoop &loop) {
        // 抓住鼠标
        this->widget()->grabMouse(mouseDirection);

        auto checkSwapInnerOuter = [=] {
            if (impl_->data_->outerRadius_ < impl_->data_->innerRadius_) {
                double tmp = impl_->data_->innerRadius_;
                impl_->data_->innerRadius_ = (impl_->data_->outerRadius_);
                impl_->data_->outerRadius_ = (tmp);

                if (impl_->data_->radialDirection_ == RoiCircle::kDirectionInner)
                    impl_->data_->radialDirection_ = RoiCircle::kDirectionOuter;
                else
                    impl_->data_->radialDirection_ = RoiCircle::kDirectionInner;

                if (*sIsOnOuterBorder) {
                    *sIsOnOuterBorder = false;
                }
                else {
                    *sIsOnOuterBorder = true;
                }

            }
        };

        // 鼠标移动，拉伸
        auto waitMouseMove = layerManager->waitEvent(this, QEvent::MouseMove).then(PM_LOC, [=](QEvent *event) {
            QMouseEvent *mouseEvent = dynamic_cast<QMouseEvent *>(event);
            if (!mouseEvent) return loop.doContinue(PM_LOC); // will goto doWhile

            auto onScopeExit = this->data_->scopedTriggerSource(Widget::kFromConfigRois);

            // 获取姿态调整后的位置
            //const std::shared_ptr<Attitude> &attitude = impl_->data_->getAttitude0();
            const std::shared_ptr<RoiCircle> &data = impl_->data_;
            // 拉伸时中心点不动，简单器件，采用相对于中心点的坐标判断
            QPointF pos = qRotatedCoordinate(cv2qt(data->center_), widgetToImage(mouseEvent->pos()), -data->radian_);

            // 因为传入的direction为鼠标显示的方向，跟实际椭圆的方向不一致
            // 通过判断椭圆的旋转方向，决定是否交换拉伸的方向
            bool swapHV = (radianToDirection(data->radian_) != Qt::SizeHorCursor);
            Qt::CursorShape operation = mouseDirection;
            if (swapHV) {
                if (operation == Qt::SizeHorCursor) operation = Qt::SizeVerCursor;
                else if (operation == Qt::SizeVerCursor) operation = Qt::SizeHorCursor;
            }

            LOG_DEBUG("swapHV = {}, operation = {}\n", swapHV, (int)operation);

            // 保障鼠标尽量在椭圆的圆周上
            double dx = ::abs(pos.x() - data->center_->x);
            double dy = ::abs(pos.y() - data->center_->y);
            *(*sIsOnOuterBorder ? &data->outerRadius_ : &data->innerRadius_) = (::sqrt(dx*dx + dy*dy));

            // 转换为不含姿态调整的位置
            //impl_->data_->imageToAttitude(data, attitude.get());
            impl_->data_->outerRadius_ = data->outerRadius_;
            impl_->data_->innerRadius_ = data->innerRadius_;
            checkSwapInnerOuter();

            layerManager->notifyMove(this);
            impl_->showPosition();
            this->update();

            return loop.doContinue(PM_LOC);
        });

        // 鼠标松开
        auto waitMouseButtonRelease = layerManager->waitEvent(this, QEvent::MouseButtonRelease).then(PM_LOC, [=](QEvent *event) {
            // 检查是否要交换内外圈半径
            checkSwapInnerOuter();
            impl_->showPosition();
            this->update();
            return loop.doBreak(PM_LOC);
        });

        // 两个事件只处理其中任一个
        raceAndReject2nd(loop, { waitMouseMove, waitMouseButtonRelease });
    }).finally(PM_LOC, [=]() {
        this->widget()->releaseMouse();
        layerManager->notifyMove(this);
    });
}

// 右键菜单
Promise UiRoiCircle::runContextMenu(const QPointF &mousePos, const std::function<void(QMenu *, const FnAddMenu &)> &onMenuCreated) {
    return UiRoi::runContextMenu(mousePos, [=](QMenu *menu, const FnAddMenu &addMenu) {
        if (impl_->data_->pieRadian_ != 0) {
            QAction *actionFullCircle = menu->addAction(
                QIcon(QPixmap(":/icons/roi_circle.png")),
                tr("Revert to full circle"));
            actionFullCircle->setObjectName("actionFullCircle");
            addMenu(actionFullCircle, [=] {
                return newPromise(PM_LOC, [=](Defer defer) {
                    impl_->data_->pieRadian_ = 0;
                    defer.resolve(PM_LOC);
                });
            });
        }

        const char *iconPath = (impl_->data_->radialDirection_ == RoiCircle::kDirectionInner
            ? ":/icons/radial_direction_out.png"
            : ":/icons/radial_direction_in.png");

        QAction *actionRadialDirection = menu->addAction(
            QIcon(QPixmap(iconPath)),
            tr("Change radial direction"));
        actionRadialDirection->setObjectName("actionRadialDirection");
        addMenu(actionRadialDirection, [=] {
            return newPromise(PM_LOC, [=](Defer defer) {

                if(impl_->data_->radialDirection_ == RoiCircle::kDirectionInner)
                    impl_->data_->radialDirection_ = RoiCircle::kDirectionOuter;
                else
                    impl_->data_->radialDirection_ = RoiCircle::kDirectionInner;

                defer.resolve(PM_LOC);
            });
        });

        // 允许继承者定制菜单
        if (onMenuCreated)
            onMenuCreated(menu, addMenu);
    });
}

