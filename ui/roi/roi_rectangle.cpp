#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES
#endif
#include "ui/layer_manager.h"
#include "ui/tl_application.h"
#include "ui/roi.h"
#include "ui/roi/roi_rectangle.h"
#include "types/attitude.h"
#include "utils/log.h"
#include "utils/qt_utils.h"
#include "utils/cv_utils.h"
#include "utils/var_type.h"
#include "utils/format.h"
#include "promise-cpp/add_ons/qt/promise_qt.hpp"
#include "utils/event_holder.h"
#include <QPainter>
#include <QPainterPath>
#include <QStyle>
#include <QKeyEvent>
#include <QStyleOption>
#include <QMainWindow>
#include <QStatusBar>

using namespace promise;

AP_REGISTRATION{
    anypro::registration::class_<RoiRectangle>()
        .base<Roi>()
        .property("radian", &RoiRectangle::radian_)
        .property("center", &RoiRectangle::center_)
        .property("radian", &RoiRectangle::size_)
        .property("useScanLine", &RoiRectangle::useScanLine_);
}

class UiRoiRectangle::Impl {
public:
    Impl(UiRoiRectangle *thiz) : thiz_(thiz) { }

    UiRoiRectangle                   *thiz_;
    std::shared_ptr<RoiRectangle>     data_;               // 数据对象
    bool                              isStraightenRect_;   // 是否为正交矩形
    std::shared_ptr<QPointF>          movingStartPoint_;   // 初始移动起点
    std::shared_ptr<QRectF>           textRect_;           // 文本显示的位置
    EventHolder                       eventHolder_;        // 数据绑定句柄

    bool isOnHotSpot_rotate(const QPointF &pos) const;     // 落入旋转柄上
    bool isOnHotSpot_text(const QPointF &pos) const;       // 落入标签文字上
    bool isOnHotSpot_center(const QPointF &pos) const;     // 落入区域中心点
    bool isOnBorder(const QPointF &pos,
                    Qt::WindowFrameSection *edge,
                    Qt::CursorShape *mouseDirection) const;// 落入区域边框

    void showPosition();
};

// 矩形边的位置转成鼠标图标呈现的角度
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

// 根据角度旋转后的鼠标箭头方向
static Qt::CursorShape getRotatedMouseDirection(double radian, const Qt::CursorShape &mouseDirection) {
    Qt::CursorShape ret = mouseDirection;
    bool swapHV = (radianToDirection(radian) != Qt::SizeHorCursor);
    if (swapHV) {
        if (ret == Qt::SizeFDiagCursor) ret = Qt::SizeBDiagCursor;
        else if (ret == Qt::SizeBDiagCursor) ret = Qt::SizeFDiagCursor;
        else if (ret == Qt::SizeHorCursor) ret = Qt::SizeVerCursor;
        else if (ret == Qt::SizeVerCursor) ret = Qt::SizeHorCursor;
    }
    return ret;
}

static void roiInitTable(RoiRectangle *roi) {
    roi->cfgTable_->push_back({ "center", &roi->center_ });
    roi->cfgTable_->push_back({ "radian", &roi->radian_ });
    roi->cfgTable_->push_back({ "size",   &roi->size_ });
    roi->cfgTable_->push_back({ "useScanLine", &roi->useScanLine_ });
}

RoiRectangle::RoiRectangle(const std::string &roiType)
    : Roi(roiType, Roi::kImageArea) {
    if (roiType != "straight_rectangle"
        && roiType != "rectangle")
        throw std::runtime_error("roiType not supported");

    roiInitTable(this);
    // 默认大小和位置
    this->radian_ = (0);
    this->center_ = cv::Point2f(0, 0);
    this->size_ = cv::Size2f(ROI_DEFAULT_VISIABLE_RECT.width(),
        ROI_DEFAULT_VISIABLE_RECT.height());
    this->useScanLine_ = false;

    color_ = QColor();
}

RoiRectangle::RoiRectangle(const RoiRectangle &right) 
    : Roi(right) {
    roiInitTable(this);
    this->center_ = right.center_;
    this->radian_ = right.radian_;
    this->size_ = right.size_;
    this->useScanLine_ = right.useScanLine_;
}

cv::RotatedRect RoiRectangle::minAreaRect() const {
    return cv::RotatedRect(this->center_, size_, radianToAngle(radian_));
}

// 获取roi区域的图像（超出图像范围的部分，使用patchBgra填充）
std::pair<cv::Mat, cv::RotatedRect> RoiRectangle::roiImage(const cv::Mat &inputImage, const cv::Scalar &patchBgra, bool tryInPlace, bool isMarginTransparent) const {
    return Roi::roiImage(inputImage, patchBgra, tryInPlace, isMarginTransparent);
}

// 获取姿态  
Attitude RoiRectangle::getAttitude() const {
    Attitude ret;
    ret.score_ = 0;
    ret.radian_ = this->radian_;
    ret.center_ = this->center_;
    return ret;
}

// 获取中心
cv::Point2f RoiRectangle::getCenter() const {
    return this->center_;
}

// 判断点是否在Roi区域之内
bool RoiRectangle::contains(const cv::Point2f &point) const {
    const cv::Point2f &pos = cvRotatedCoordinate(center_, point, -radian_);
    cv::Rect2f rect{
        center_->x - size_->width / 2,
        center_->y - size_->height / 2,
        size_->width,
        size_->height
    };
    return rect.contains(pos);
}

// 从绑定的姿态位置的区域（从this Roi对象），转成相对于图像的区域
std::shared_ptr<Roi> RoiRectangle::attitudeToImage(const Attitude *attitude) const {
    if (attitude == nullptr) {
        // 变量不存在
        return std::dynamic_pointer_cast<Roi>(const_cast<RoiRectangle *>(this)->shared_from_this());
    }
    else {
        std::shared_ptr<RoiRectangle> ret = std::make_shared<RoiRectangle>(*this);
        ret->detachAttitude(attitude);
        return ret;
    }
}

// 从相对于图像的区域，转到绑定的姿态位置的区域
std::shared_ptr<Roi> RoiRectangle::imageToAttitude(const Attitude *attitude) const {
    if (attitude == nullptr) {
        // 变量不存在
        return std::dynamic_pointer_cast<Roi>(const_cast<RoiRectangle *>(this)->shared_from_this());
    }
    else {
        std::shared_ptr<RoiRectangle> ret = std::make_shared<RoiRectangle>(*this);
        ret->attachAttitude(attitude);
        return ret;
    }
}

// 解绑姿态变量（从相对于姿态的坐标，转成图像坐标）
void RoiRectangle::detachAttitude(const Attitude *attitude) {
    if (!attitude) return;
    this->center_ = cvAddAttitude(this->center_, *attitude);
    this->radian_ = this->radian_ + attitude->radian_;
}

// 绑定姿态变量（从图像坐标，转成相对于姿态的坐标）
void RoiRectangle::attachAttitude(const Attitude *attitude) {
    if (!attitude) return;
    this->center_ = cvRemoveAttitude(this->center_, *attitude);
    this->radian_ = this->radian_ - attitude->radian_;
}

void RoiRectangle::paint(UiRoi &uiRoi, QPainter &qp, bool isFirstInGroup, bool isDrawHotSpot) {
    QSizeF widgetSize = qp.clipBoundingRect().size();
    LOG_DEBUG("uiRoi paint rc = {}\n", widgetSize);

    QPointF center = uiRoi.imageToWidget(cv2qt(center_));
    double width = uiRoi.imageToWidget(size_->width);
    double height = uiRoi.imageToWidget(size_->height);
    if (!qIsFinite(center)
        || !qIsNormal(width)
        || !qIsNormal(height))
        return;

    LOG_DEBUG("center = {}, size = {}, radian = {}", center_, size_, radian_);
    qp.save();
    qp.setRenderHints(QPainter::Antialiasing);
    qp.rotate(radian_ * 180 / M_PI);

    center = qRotatedCoordinate(QPointF(0, 0), center, -radian_);

    const QColor &color = uiRoi.allocateColor(color_);

    // 画矩形
    qreal lineWidth = ROI_LINE_WIDTH;
    QPen pen;
    uiRoi.setPenStyle(&pen, color, lineWidth);
    pen.setCapStyle(Qt::PenCapStyle::RoundCap);
    pen.setJoinStyle(Qt::PenJoinStyle::RoundJoin);
    qp.setPen(pen);
    QRectF rect = { center.x() - width / 2, center.y() - height / 2, width, height };
    qp.drawRect(rect);

    // 画中间十字
    QColor color1;
    qreal lightness = color.lightnessF();
    lightness = lightness + (1 - lightness) * 0.5;
    color1.setHslF(color.hslHueF(), color.hslSaturationF(), lightness, color.alphaF());
    
    QPen pen1;
    uiRoi.setPenStyle(&pen1, color1, ROI_LINE_WIDTH);
    pen1.setStyle(Qt::PenStyle::DotLine);
    pen1.setCapStyle(Qt::PenCapStyle::RoundCap);
    pen1.setJoinStyle(Qt::PenJoinStyle::RoundJoin);
    qp.setPen(pen1);
    qp.drawLine(rect.x(), center.y(),
        rect.x() + rect.width(), center.y());

    QPen pen3;
    uiRoi.setPenStyle(&pen3, color1, ROI_LINE_WIDTH_THINE); //粗线
    pen3.setStyle(Qt::PenStyle::DotLine);
    pen3.setCapStyle(Qt::PenCapStyle::RoundCap);
    pen3.setJoinStyle(Qt::PenJoinStyle::RoundJoin);
    qp.setPen(pen3);
    qp.drawLine(center.x(), rect.y(),
        center.x(), rect.y() + rect.height());

    // 画箭头方向
    uiRoi.drawArrow(qp,
        QPointF{ rect.x() + rect.width() - ROI_ARROW_LEN, center.y() },
        QPointF{ rect.x() + rect.width(), center.y() },
        color1);

    // 画扫描线
    QColor color2;
    lightness = color.lightnessF();
    lightness = lightness + (1 - lightness) * 0.1;
    color2.setHslF(color.hslHueF(), color.hslSaturationF(), lightness, color.alphaF());
    if (this->useScanLine_) {
#define SCAN_LINE_INTERVAL 15.0
#define SCAN_LINE_LENGTH 25.0
        int count = (int)(width / SCAN_LINE_INTERVAL + 0.5);
        if (count <= 0) count = 1;

        double scanLineLength = std::min(SCAN_LINE_LENGTH, rect.height());
        double y0 = center.y() - scanLineLength / 2;
        double y1 = center.y() + scanLineLength / 2;


        if (count % 2 == 0) {
            // 偶数  
            for (int i = 0; i < count / 2; ++i) {
                double x0 = center.x() - i * SCAN_LINE_INTERVAL - (SCAN_LINE_INTERVAL / 2.0);
                uiRoi.drawArrow(qp, { x0, y0 }, { x0, y1 }, color2);
                double x1 = center.x() + i * SCAN_LINE_INTERVAL + (SCAN_LINE_INTERVAL / 2.0);
                uiRoi.drawArrow(qp, { x1, y0 }, { x1, y1 }, color2);
            }
        }
        else {
            // 奇数  
            double x = center.x();
            uiRoi.drawArrow(qp, { x, y0 }, { x, y1 }, color2);
            for (int i = 0; i < count / 2; ++i) {
                double x0 = center.x() - i * SCAN_LINE_INTERVAL;
                uiRoi.drawArrow(qp, { x0, y0 }, { x0, y1 }, color2);
                double x1 = center.x() + i * SCAN_LINE_INTERVAL;
                uiRoi.drawArrow(qp, { x1, y0 }, { x1, y1 }, color2);
            }
        }
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
        qp.drawEllipse(center, radius, radius);

        QPointF rotateCenter = { center.x() + width / 2,
                                 center.y() - height / 2 };

        if(!static_cast<UiRoiRectangle &>(uiRoi).impl_->isStraightenRect_)
            uiRoi.drawRotateHandler(qp, rotateCenter, color2);
    }

    // 画标签文字
    if (!this->text_->empty()) {
        //QBrush brush4(color);
        //brush4.setStyle(Qt::BrushStyle::SolidPattern);
        QPen penText(color);
        //penText.setStyle(Qt::PenStyle::SolidLine);
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
            font.setBold(false);
            font.setWeight(QFont::ExtraLight);
            qp.setFont(font);
            const QFontMetrics &fm = qp.fontMetrics();
            textRect = fm.boundingRect(text);
            if (textRect.width() + 18 < width
                && textRect.height() + 4 < height) {
                minFontSize = testFontSize;
            }
            else {
                maxFontSize = testFontSize;
            }
        }
        LOG_DEBUG("textRect = {}", textRect);

        QPointF point = { center.x() - width / 2 + ROI_LABEL_MARGIN + lineWidth, center.y() - height / 2 };
        QSize textSize = { textRect.width() + 8, textRect.height() };
        textRect = QRect{ point.toPoint(), textSize };
        // 位置保存到roi对象
        UiRoiRectangle *roiRectangle = dynamic_cast<UiRoiRectangle *>(&uiRoi);
        if (roiRectangle != nullptr && isFirstInGroup) {
            if (roiRectangle->impl_->textRect_)
                *roiRectangle->impl_->textRect_ = textRect;
            else
                roiRectangle->impl_->textRect_ = std::make_shared<QRectF>(textRect);
        }

        LOG_DEBUG("textRect = {}", textRect);
        //qp.drawRect(textRect);
        uiRoi.drawHollowFont(qp, font, text, textRect, color);
        //qp.setClipRect(textRect, Qt::ClipOperation::IntersectClip);
    }

    qp.restore();
}

bool RoiRectangle::equals(const Roi& other) const {
    RoiRectangle* otherRectangle = dynamic_cast<RoiRectangle*>(const_cast<Roi*>(&other));
    if (otherRectangle == nullptr) {
        return false;
    }
    return this->radian_ == otherRectangle->radian_
        && this->center_ == otherRectangle->center_
        && this->size_ == otherRectangle->size_;
}

static std::shared_ptr<Roi> insureRoi(const std::string &roiType, const std::shared_ptr<Roi> &roi) {
    if (roi) {
        if (roiType != roi->roiType_)
            throw std::invalid_argument(fmt::format("roiType not match  = {}, {}", roiType, roi->roiType_));
        return roi;
    }

    auto data = std::make_shared<RoiRectangle>(roiType);
    return data;
}

UiRoiRectangle::UiRoiRectangle(const std::string &roiType, const std::shared_ptr<Roi> &roi)
    : UiRoi(roiType, insureRoi(roiType, roi))
    , impl_(new Impl(this)) {
    //TlApplication::assertGuiThread();
    PTD;
    LOG_DEBUG("new UiRoiRectangle = {}", this);

    auto updateWidget = [=](BindingData::EventType eventType, const boost::any &eventValue, const boost::any &src) {
        if (eventType == BindingData::kEdit) {
            impl_->data_->triggerEvent(eventType, eventValue, (src.empty() ? impl_->data_->triggerSource() : src));
        }
        this->update();
    };

    impl_->isStraightenRect_ = (roiType == "straight_rectangle");
    impl_->data_ = std::dynamic_pointer_cast<RoiRectangle>(data_);

    impl_->eventHolder_ += bindDataToObject(static_cast<BindingData *>(this), static_cast<BindingData *>(&impl_->data_->center_), [=]() {}, updateWidget);
    impl_->eventHolder_ += bindDataToObject(static_cast<BindingData *>(this), static_cast<BindingData *>(&impl_->data_->radian_), [=]() {}, updateWidget);
    impl_->eventHolder_ += bindDataToObject(static_cast<BindingData *>(this), static_cast<BindingData *>(&impl_->data_->size_), [=]() {}, updateWidget);
    impl_->eventHolder_ += bindDataToObject(static_cast<BindingData *>(this), static_cast<BindingData *>(&impl_->data_->useScanLine_), [=]() {}, updateWidget);
    impl_->eventHolder_ += bindDataToObject(static_cast<BindingData *>(this), static_cast<BindingData *>(&impl_->data_->color_), [=]() {}, updateWidget);
    impl_->eventHolder_ += bindDataToObject(static_cast<BindingData *>(this), static_cast<BindingData *>(&impl_->data_->text_), [=]() {}, updateWidget);
    //impl_->eventHolder_ += bindDataToObject(static_cast<BindingData *>(this), static_cast<BindingData *>(&impl_->data_->boundVars_), [=]() {}, updateWidget);
}


UiRoiRectangle::~UiRoiRectangle() {
    LOG_DEBUG("delete UiRoiRectangle = {}", this);
    PTD;
    impl_->eventHolder_.clear();
}


bool UiRoiRectangle::isOnObject(const QPointF &pos, Qt::CursorShape &mouseDirection) {
    std::shared_ptr<LayerManager> layerManager = layerManager_.lock();
    if (!layerManager || layerManager->isLayerToolDisabled(this))
        return false;

    const auto &data = impl_->data_;
    if (data->hasEmptyAttitudes())
        return false;

    QPointF pos2 = qRotatedCoordinate(cv2qt(data->center_), pos, -data->radian_);
    Qt::WindowFrameSection edge = Qt::NoSection;

    // 是否初始移动
    if (impl_->movingStartPoint_) {
        mouseDirection = Qt::CrossCursor;
    }
    // 判断是否落在圆周区域（先判断圆周避免缩放到很小没法再放大）
    else if (impl_->isOnBorder(pos2, &edge, &mouseDirection)) {
        // 抓住鼠标`
        mouseDirection = getRotatedMouseDirection(data->radian_, mouseDirection);
    }
    // 判断是否落在热点区域
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
    else {
        mouseDirection = Qt::ArrowCursor;
        return false;
    }
    return true;
}





void UiRoiRectangle::paint(QPainter &qp, bool isDrawHotSpot) {
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

bool UiRoiRectangle::Impl::isOnHotSpot_rotate(const QPointF &pos) const {
    if (this->isStraightenRect_)
        return false;

    const auto &data = this->data_;
    QPointF rotatePoint = { data->center_->x + data->size_->width / 2 + thiz_->widgetToImage(ROI_HOTSPOT_DISTANCE),
                            data->center_->y - data->size_->height / 2 - thiz_->widgetToImage(ROI_HOTSPOT_DISTANCE) };
    qreal r = thiz_->widgetToImage(ROI_HOTSPOT_RADIUS + ROI_MASK_MARGIN);
    if (qDistance2(pos, rotatePoint) <= r * r)
        return true;
    else
        return false;
}


bool UiRoiRectangle::Impl::isOnHotSpot_text(const QPointF &pos) const {

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


bool UiRoiRectangle::Impl::isOnHotSpot_center(const QPointF &pos) const {
    const auto &data = this->data_;
    qreal r = thiz_->widgetToImage(ROI_HOTSPOT_RADIUS + ROI_MASK_MARGIN);
    if (qDistance2(pos, cv2qt(data->center_)) <= r * r)
        return true;
    else
        return false;
}

bool UiRoiRectangle::Impl::isOnBorder(const QPointF &pos, Qt::WindowFrameSection *edge, Qt::CursorShape *mouseDirection) const {
    //  落入大框不落入小框内
    const auto &data = this->data_;
    double halfBorder = thiz_->widgetToImage(ROI_MASK_MARGIN + ROI_LINE_WIDTH/2);
    QRectF bigRect = { data->center_->x - data->size_->width / 2 - halfBorder,
                       data->center_->y - data->size_->height / 2 - halfBorder,
                       data->size_->width + halfBorder * 2,
                       data->size_->height + halfBorder * 2 };
    QRectF smallRect = { data->center_->x - data->size_->width / 2 + halfBorder,
                         data->center_->y - data->size_->height / 2 + halfBorder,
                         std::max(data->size_->width - halfBorder * 2, 0.),
                         std::max(data->size_->height - halfBorder * 2, 0.) };
	
    bool inBigRect = bigRect.contains(pos);
    bool inSmallRect = smallRect.contains(pos);
    bool matched = (inBigRect && !inSmallRect);

    if (matched && edge != nullptr && mouseDirection != nullptr) {
        // 计算鼠标所在何处边沿
        double r2 = (halfBorder) * (halfBorder); // 角度热点区域的半径的平方
        if (qDistance2(pos, QPointF(data->center_->x - data->size_->width / 2, data->center_->y - data->size_->height / 2)) <= r2) {
            *edge = Qt::TopLeftSection;
            *mouseDirection = Qt::SizeFDiagCursor;
        }
        else if (qDistance2(pos, QPointF(data->center_->x + data->size_->width / 2, data->center_->y - data->size_->height / 2)) <= r2) {
            *edge = Qt::TopRightSection;
            *mouseDirection = Qt::SizeBDiagCursor;
        }
        else if (qDistance2(pos, QPointF(data->center_->x - data->size_->width / 2, data->center_->y + data->size_->height / 2)) <= r2) {
            *edge = Qt::BottomLeftSection;
            *mouseDirection = Qt::SizeBDiagCursor;
        }
        else if (qDistance2(pos, QPointF(data->center_->x + data->size_->width / 2, data->center_->y + data->size_->height / 2)) <= r2) {
            *edge = Qt::BottomRightSection;
            *mouseDirection = Qt::SizeFDiagCursor;
        }
        else if (bigRect.left() <= pos.x() && smallRect.left() >= pos.x()) {
            *edge = Qt::LeftSection;
            *mouseDirection = Qt::SizeHorCursor;
        }
        else if (bigRect.right() >= pos.x() && smallRect.right() <= pos.x()) {
            *edge = Qt::RightSection;
            *mouseDirection = Qt::SizeHorCursor;
        }
        else if (bigRect.top() <= pos.y() && smallRect.top() >= pos.y()) {
            *edge = Qt::TopSection;
            *mouseDirection = Qt::SizeVerCursor;
        }
        else if (bigRect.bottom() >= pos.y() && smallRect.bottom() <= pos.y()) {
            *edge = Qt::BottomSection;
            *mouseDirection = Qt::SizeVerCursor;
        }
        else {
            *edge = Qt::NoSection;
            *mouseDirection = Qt::ArrowCursor;
        }
    }

    return matched;
}

void UiRoiRectangle::Impl::showPosition() {
    std::string text;
    if (::abs(::fmod(data_->radian_, 360.0)) < 1e-2) {
        cv::Point2f pos = data_->center_ - cv::Point2f(data_->size_ / 2.0f);
        text = fmt::format("pos={:.3f}, size={:.3f}", pos, data_->size_);
    }
    else {
        text = fmt::format("center={:.3f}, size={:.3f}, angle={:3f}", data_->center_, data_->size_, radianToAngle(data_->radian_));
    }

    thiz_->showStatusMessage(QString::fromStdString(text), 3000);
}

Promise UiRoiRectangle::runInitialize() {
    PTD;
    std::shared_ptr<LayerManager> layerManager = layerManager_.lock();
    this->widget()->grabMouse(Qt::CrossCursor);

    impl_->data_->size_ = cv::Size2f(0, 0);
    impl_->data_->center_ = cv::Point2f(0, 0);
    impl_->movingStartPoint_ = std::make_shared<QPointF>(cv2qt(impl_->data_->center_));

    // 设定移动中的顶点

    // 等待左键点击
    layerManager->setFocusedLayerTool(this);
    return pendingTask_ = doWhile(PM_LOC, [=](const DeferLoop &loop) {
        PTD;

        // 鼠标移动，根据位置设置鼠标形状
        auto onMoving = [=](QEvent *event) {
            PTD;
            QPointF pos = widgetToImage(getMousePos(event));
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
            PTD;
            impl_->movingStartPoint_ = std::make_shared<QPointF>(widgetToImage(mouseEvent->pos()));

            auto onScopeExit = impl_->data_->scopedTriggerSource(Widget::kFromConfigRois);

            impl_->data_->isInitialized_ = true;
            impl_->data_->center_ = qt2cv(*impl_->movingStartPoint_);
            impl_->data_->size_ = { 0, 0 };
            this->widget()->setCursor(Qt::SizeFDiagCursor);
            impl_->showPosition();
            this->update();
        }
        else {
            // 取消ROI
            PTD;
            throw mouseEvent;
        }
    }).

    // 不断等待更多鼠标事件
    then(PM_LOC, [=]() {
        PTD;
        // 设定鼠标独占状态
        layerManager->setFocusedLayerTool(this);

        return doWhile(PM_LOC, [=](const DeferLoop &loop) {
            PTD;
            // 鼠标移动，根据位置设置鼠标形状
            auto onMoving = [=](QEvent *event) {
                QPointF pos = widgetToImage(getMousePos(event));

                // 设定移动中的顶点
                auto onScopeExit = impl_->data_->scopedTriggerSource(Widget::kFromConfigRois);

                impl_->data_->center_ = qt2cv((pos + *impl_->movingStartPoint_) / 2);
                impl_->data_->size_ = cv::Size2f(::abs(pos.x() - impl_->movingStartPoint_->x()),
                                                 ::abs(pos.y() - impl_->movingStartPoint_->y()));

                if ((pos.x() < impl_->movingStartPoint_->x() && pos.y() < impl_->movingStartPoint_->y())
                    || (pos.x() >= impl_->movingStartPoint_->x() && pos.y() >= impl_->movingStartPoint_->y())) {
                    this->widget()->setCursor(Qt::SizeFDiagCursor); // ⤡
                }
                else {
                    this->widget()->setCursor(Qt::SizeBDiagCursor); // ⤡
                }

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
                impl_->data_->size_ = cv::Size2f(::abs(pos.x() - impl_->movingStartPoint_->x()),
                                                 ::abs(pos.y() - impl_->movingStartPoint_->y()));
                if (impl_->data_->size_->width == 0 && impl_->data_->size_->height == 0) {
                    impl_->data_->size_ = cv::Size2f((float)ROI_DEFAULT_MIN_SIZE,
                                                     (float)(ROI_DEFAULT_MIN_SIZE * 2 / 3.0));
                }
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
    }).then(PM_LOC, [=]() {
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

Promise UiRoiRectangle::runEdit() {
    std::shared_ptr<LayerManager> layerManager = layerManager_.lock();
    return pendingTask_ = (data_->isRef_ || this->isReadonly_)
        ? promise::newPromise(PM_LOC)
        : doWhile(PM_LOC, [=](const DeferLoop &loop) {
        PTD;
        // 鼠标移动，根据位置设置鼠标形状
        auto onMoving = [=](QEvent *event) {
            PTD;
            QPointF pos = widgetToImage(getMousePos(event));

            // 判断是否落在周边区域（先判断周边避免缩放到很小没法再放大）
            Qt::CursorShape mouseDirection = Qt::ArrowCursor;
            if (isOnObject(pos, mouseDirection))
                this->widget()->setCursor(mouseDirection);

            return loop.doContinue(PM_LOC); // will goto doWhile
        };
        auto waitHoverMove = layerManager->waitEvent(this, QEvent::HoverMove).then(PM_LOC, onMoving);
        auto waitMouseMove = layerManager->waitEvent(this, QEvent::MouseMove).then(PM_LOC, onMoving);

        // 鼠标点击，根据位置判断进入其他状态
        auto waitMouseButtonPress = layerManager->waitEvent(this, QEvent::MouseButtonPress).then(PM_LOC, [=](QEvent *event) {
            PTD;
            if (getData()->hasEmptyAttitudes())
                return loop.doContinue(PM_LOC); // 空姿态就不要操作了

            QMouseEvent *mouseEvent = dynamic_cast<QMouseEvent *>(event);
            if (!mouseEvent) return loop.doContinue(PM_LOC); // will goto doWhile

            // 获取姿态调整后的位置
            const auto &data = impl_->data_;
            QPointF pos = qRotatedCoordinate(cv2qt(data->center_), widgetToImage(mouseEvent->pos()), -data->radian_);

            // 判断是否落在边的区域（先判断圆周避免缩放到很小没法再放大）
            Qt::WindowFrameSection edge = Qt::NoSection;
            Qt::CursorShape mouseDirection = Qt::ArrowCursor;
            if (impl_->isOnBorder(pos, &edge, &mouseDirection)) {
                if (mouseEvent->buttons().testFlag(Qt::RightButton)) {
                    runContextMenu(mouseEvent->pos(), {}).then(PM_LOC, loop);
                    return;
                }
                else if (mouseEvent->buttons().testFlag(Qt::LeftButton)) {
                    layerManager->setFocusedLayerTool(this);
                    runResize(edge, mouseDirection).finally(PM_LOC, [=]() {layerManager->setFocusedLayerTool(nullptr); }).then(PM_LOC, loop);
                    return;
                }
            }
            // 判断是否落在热点区域
            else if (impl_->isOnHotSpot_rotate(pos)) {
                layerManager->setFocusedLayerTool(this);
                runRotate().finally(PM_LOC, [=]() {layerManager->setFocusedLayerTool(nullptr); }).then(PM_LOC, loop);
                return;
            }
            // 判断是否落在中心热点区域
            else if (impl_->isOnHotSpot_center(pos)) {
                if (mouseEvent->buttons().testFlag(Qt::RightButton)) {
                    runContextMenu(mouseEvent->pos(), {}).then(PM_LOC, loop);
                    return;
                }
                else if (mouseEvent->buttons().testFlag(Qt::LeftButton)) {
                    layerManager->setFocusedLayerTool(this);
                    runMove().finally(PM_LOC, [=]() {layerManager->setFocusedLayerTool(nullptr); }).then(PM_LOC, loop);
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

            this->widget()->setCursor(Qt::ArrowCursor);
            return loop.doContinue(PM_LOC); // will goto doWhile
        });

        // 以上几个事件只处理其中任一个
        raceAndReject2nd(loop, { waitHoverMove, waitMouseMove, waitMouseButtonPress });

    }).finally(PM_LOC, [=]() {
        PTD;
        layerManager->remove(this);
        pendingTask_.clear();
    });
}

Promise UiRoiRectangle::runRotate() {
    // 旋转状态
    std::shared_ptr<LayerManager> layerManager = layerManager_.lock();
    return doWhile(PM_LOC, [=](const DeferLoop &loop) {
        // 抓住鼠标
        this->widget()->grabMouse(Qt::ClosedHandCursor);

        // 鼠标移动，旋转本组件
        auto waitMouseMove = layerManager->waitEvent(this, QEvent::MouseMove).then(PM_LOC, [=](QEvent *event) {
            QMouseEvent *mouseEvent = dynamic_cast<QMouseEvent *>(event);
            if (!mouseEvent) return loop.doContinue(PM_LOC); // will goto doWhile

            const QPointF &pos = widgetToImage(mouseEvent->pos());
            LOG_DEBUG("mouse = {}\n", mouseEvent->pos());

            auto onScopeExit = impl_->data_->scopedTriggerSource(Widget::kFromConfigRois);

            // 获取姿态调整后的位置
            //const std::shared_ptr<Attitude> &attitude = impl_->data_->getAttitude0();
            const std::shared_ptr<RoiRectangle> &data = impl_->data_;
            data->radian_ = (::atan2(pos.y() - data->center_->y, pos.x() - data->center_->x)
                          + ::atan2(data->size_->height, data->size_->width));

            // 转换为不含姿态调整的位置
            //impl_->data_->imageToAttitude(data, attitude.get());
            // 
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
    }).then(PM_LOC, [=]() {
        this->triggerEvent(BindingData::kCustomEvent, "__rotate__");
    }).finally(PM_LOC, [=]() {
        PTD;
        this->widget()->releaseMouse();
        layerManager->notifyMove(this);
    });
}

Promise UiRoiRectangle::runMove() {
    // 移动状态
    std::shared_ptr<LayerManager> layerManager = layerManager_.lock();
    return doWhile(PM_LOC, [=](const DeferLoop &loop) {
        // 抓住鼠标
        PTD;
        this->widget()->grabMouse(Qt::SizeAllCursor);

        // 鼠标移动，移动本组件
        auto waitMouseMove = layerManager->waitEvent(this, QEvent::MouseMove).then(PM_LOC, [=](QEvent *event) {
            PTD;
            QMouseEvent *mouseEvent = dynamic_cast<QMouseEvent *>(event);
            if (!mouseEvent) return loop.doContinue(PM_LOC); // will goto doWhile

            const QPointF &pos = widgetToImage(mouseEvent->pos());

            auto onScopeExit = impl_->data_->scopedTriggerSource(Widget::kFromConfigRois);

            // 获取姿态调整后的位置
            //const std::shared_ptr<Attitude> &attitude = impl_->data_->getAttitude0();
            const std::shared_ptr<RoiRectangle> &data = impl_->data_;
            data->center_ = qt2cv(pos);
            // 转换为不含姿态调整的位置
            //impl_->data_->imageToAttitude(data, attitude.get());

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
    }).then(PM_LOC, [=]() {
        this->triggerEvent(BindingData::kCustomEvent, "__move__");
    }).finally(PM_LOC, [=]() {
        PTD;
        this->widget()->releaseMouse();
        layerManager->notifyMove(this);
    });
}

Promise UiRoiRectangle::runResize(Qt::WindowFrameSection edge, const Qt::CursorShape &mouseDirection) {
    // 尺寸拉伸状态
    std::shared_ptr<LayerManager> layerManager = layerManager_.lock();

    // 获取姿态调整后的位置
    const std::shared_ptr<RoiRectangle> &data = impl_->data_;
    QPointF center = qRotatedCoordinate(QPointF(0, 0), cv2qt(data->center_), -data->radian_);
    auto left = std::make_shared<double>(center.x() - data->size_->width / 2);
    auto right = std::make_shared<double>(center.x() + data->size_->width / 2);
    auto top = std::make_shared<double>(center.y() - data->size_->height / 2);
    auto bottom = std::make_shared<double>(center.y() + data->size_->height / 2);

    PTD;
    return doWhile(PM_LOC, [=](const DeferLoop &loop) {
        PTD;

        // 获取姿态调整后的位置
        const std::shared_ptr<RoiRectangle> &data = impl_->data_;
        // 抓住鼠标
        this->widget()->grabMouse(getRotatedMouseDirection(data->radian_, mouseDirection));

        // 鼠标移动，拉伸
        auto waitMouseMove = layerManager->waitEvent(this, QEvent::MouseMove).then(PM_LOC, [=](QEvent *event) {
            PTD;
            QMouseEvent *mouseEvent = dynamic_cast<QMouseEvent *>(event);
            if (!mouseEvent) return loop.doContinue(PM_LOC); // will goto doWhile

            // 获取姿态调整后的位置
            //const std::shared_ptr<Attitude> &attitude = impl_->data_->getAttitude0();
            const std::shared_ptr<RoiRectangle> &data = impl_->data_;

            // 拉伸时中心点会移动，采用相对于原点的坐标判断
            QPointF pos = qRotatedCoordinate(QPointF(0, 0), widgetToImage(mouseEvent->pos()), -data->radian_);

            // 保障鼠标在矩形的边上
            // 中心点会变化，保障其他边位置不变，
            switch (edge) {
            case Qt::LeftSection: {
                // 仅改变左边
                *left = pos.x();
                break;
            }
            case Qt::TopSection: {
                // 仅改变上边
                *top = pos.y();
                break;
            }
            case Qt::RightSection: {
                // 仅改变右边
                *right = pos.x();
                break;
            }
            case Qt::BottomSection: {
                // 仅改变下边
                *bottom = pos.y();
                break;
            }
            case Qt::TopLeftSection: {
                // 改变左上边
                *left = pos.x();
                *top = pos.y();
                break;
            }
            case Qt::TopRightSection: {
                // 改变右上边
                *right = pos.x();
                *top = pos.y();
                break;
            }
            case Qt::BottomLeftSection: {
                // 改变左下边
                *left = pos.x();
                *bottom = pos.y();
                break;
            }
            case Qt::BottomRightSection: {
                // 改变右下边
                *right = pos.x();
                *bottom = pos.y();
                break;
            }
            default:
                // 同比例拉伸横纵坐标
                ;
            }

            auto onScopeExit = impl_->data_->scopedTriggerSource(Widget::kFromConfigRois);

            data->size_ = cv::Size2f(::abs(*right - *left),
                                     ::abs(*bottom - *top));
            QPointF center = QPointF((*left + *right) / 2, (*top + *bottom) / 2);
            data->center_ = qt2cv(qRotatedCoordinate(QPointF(0, 0), center, data->radian_));
            // 转换为不含姿态调整的位置
            //impl_->data_->imageToAttitude(data, attitude.get());
            //impl_->data_->size_ = data->size_;

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
    }).then(PM_LOC, [=]() {
        this->triggerEvent(BindingData::kResize);
    }).finally(PM_LOC, [=]() {
        PTD;
        this->widget()->releaseMouse();
        layerManager->notifyMove(this);
    });
}


// 右键菜单
Promise UiRoiRectangle::runContextMenu(const QPointF &mousePos, const std::function<void(QMenu *, const FnAddMenu &)> &onMenuCreated) {
    return UiRoi::runContextMenu(mousePos, [=](QMenu *menu, const FnAddMenu &addMenu) {

        QAction *actionScanLine = menu->addAction(
            QIcon(QPixmap(":/icons/scan_line.png")),
            tr("Show scan lines"));
        actionScanLine->setObjectName("actionScanLine");
        actionScanLine->setCheckable(true);
        actionScanLine->setChecked(impl_->data_->useScanLine_);
        addMenu(actionScanLine, [=] {
            return newPromise(PM_LOC, [=](Defer defer) {
                auto onScopeExit = impl_->data_->scopedTriggerSource(Widget::kFromConfigRois);
                impl_->data_->useScanLine_ = !impl_->data_->useScanLine_;
                defer.resolve(PM_LOC);
            });
        });

        // 允许继承者定制菜单
        if (onMenuCreated)
            onMenuCreated(menu, addMenu);
    });
}

