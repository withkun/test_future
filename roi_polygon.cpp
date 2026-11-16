#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES
#endif
#include "roi_polygon.h"
#include "ui/layer_manager.h"
#include "ui/roi.h"
#include "types/attitude.h"
#include "types/widget.h"
#include "utils/log.h"
#include "utils/qt_utils.h"
#include "utils/cv_utils.h"
#include "utils/var_type.h"
#include "utils/event_holder.h"
#include <QPainter>
#include <QPainterPath>
#include <QStyle>
#include <QKeyEvent>
#include <QStyleOption>
#include <QMenu>

using namespace promise;

AP_REGISTRATION{
    anypro::registration::class_<RoiPolygon>()
        .base<Roi>()
        .property_readonly("points", &RoiPolygon::points_);
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

class UiRoiPolygon::Impl {
public:
    Impl(UiRoiPolygon *thiz) : thiz_(thiz) {}
    UiRoiPolygon *thiz_;
    std::shared_ptr<RoiPolygon>     data_;         // 数据对象
    std::shared_ptr<QPointF>         movingPoint_;  // 初始移动中（尚未确定的顶点）
    std::shared_ptr<QRectF>          textRect_;     // 文本显示的位置
    EventHolder                      eventHolder_;  // 数据绑定句柄
    bool                             isPolyline_;   // 是否为闭合范围

    bool isOnHotSpot_text(const QPointF &pos) const;        // 落入标签文字上
    bool isOnCorner(const QPointF &pos, size_t *cornerIndex) const;
    bool isOnBorder(const QPointF &pos, size_t *startIndex, size_t *endIndex) const;
    void showPosition();
    void setPoint(size_t offset, const QPointF &point, bool isTriggerEvent);
};



static void roiInitTable(RoiPolygon *roi) {
    roi->cfgTable_->set("points", &roi->points_);
}

RoiPolygon::RoiPolygon(const std::string &roiType)
    : Roi(roiType, Roi::kImageArea) {

    if (roiType != "ai_polygon")
        throw std::runtime_error("roiType not supported");

    roiInitTable(this);
}

RoiPolygon::RoiPolygon(const RoiPolygon &right)
    : Roi(right) {
    roiInitTable(this);
    this->points_ = right.points_;
}

cv::RotatedRect RoiPolygon::minAreaRect() const {
    std::vector<cv::Point2f> points{ points_.begin(), points_.end() };
    if (points.size() == 0)
        return cv::RotatedRect();
    cv::RotatedRect rotateRect = cv::minAreaRect(points);
    return rotateRect;
}

// 获取roi区域的图像（超出图像范围的部分，使用patchBgra填充）
std::pair<cv::Mat, cv::RotatedRect> RoiPolygon::roiImage(const cv::Mat &inputImage, const cv::Scalar &patchBgra, bool tryInPlace, bool isMarginTransparent) const {
    std::vector<cv::Point2f> points{ points_.begin(), points_.end() };

    float xmin = points[0].x;
    float xmax = points[0].x;
    float ymin = points[0].y;
    float ymax = points[0].y;

    for (size_t i = 0; i < points.size(); ++i) {
        const auto &point = points[i];
        xmin = std::min(point.x, xmin);
        xmax = std::max(point.x, xmax);
        ymin = std::min(point.y, ymin);
        ymax = std::max(point.y, ymax);
    }

    cv::Rect2f rect(cv::Point2f(xmin, ymin), cv::Point2f(xmax, ymax));
    cv::RotatedRect rotatedRect = { (rect.tl() + rect.br()) / 2.0f, rect.size(), 0 };

    if (points.size() < 3) return { cv::Mat(), rotatedRect };

    cv::Point2f tl = rect.tl();
    for (auto &point : points) {
        point -= tl;
    }

    cv::Mat cropped = cvGetCroppedImage(
        inputImage,
        rect,
        true,
        patchBgra,
        (tryInPlace || isMarginTransparent) // 补充边界透明时，一定可以先在原图上构建而不影响原图
    );

    if (cropped.empty() || !isMarginTransparent)
        return { cv::Mat(), rotatedRect };

    if (cropped.channels() == 1) {
        cv::cvtColor(cropped, cropped, cv::COLOR_GRAY2BGRA);
    }
    else if (cropped.channels() == 3) {
        cv::cvtColor(cropped, cropped, cv::COLOR_BGR2BGRA);
    }

    cv::Mat transparentImage(cropped.size(), CV_8UC4, cv::Scalar(0, 0, 0, 0));

    // 在透明图像上绘制圆
    cv::Scalar color(255, 255, 255, 255); // 填充颜色，白色
    std::vector<cv::Point> ipoints{ points.begin(), points.end() };
    cv::fillPoly(transparentImage, ipoints, color);

    // 将原始图像与透明图像进行按位与操作，获取圆区域内的图像
    cv::Mat bitWised;
    cv::bitwise_and(cropped, transparentImage, bitWised);

    // 将圆区域内的图像与透明图像进行混合，将圆区域外的部分用透明色填充
    //cv::Mat resultImage;
    //cv::addWeighted(bitWised, 1.0, transparentImage, 0.0, 0, resultImage);

    return { bitWised, rotatedRect };
}

// 获取姿态
Attitude RoiPolygon::getAttitude() const {
    Attitude ret;
    ret.score_ = 0;
    ret.radian_ = 0;
    return ret;
}

// 判断点是否在Roi区域之内
bool RoiPolygon::contains(const cv::Point2f &point) const {
    //LOG_INFO("points_.size = {}", points_.size());
    std::vector<cv::Point2f> points{ points_.begin(), points_.end() };
    double result = cv::pointPolygonTest(points, point, false);
    return (result >= 0);
}

// 从绑定的姿态位置的区域（从this Roi对象），转成相对于图像的区域
std::shared_ptr<Roi> RoiPolygon::attitudeToImage(const Attitude *attitude) const {
    if (attitude == nullptr) {
        // 变量不存在
        return std::dynamic_pointer_cast<Roi>(const_cast<RoiPolygon *>(this)->shared_from_this());
    }
    else {
        std::shared_ptr<RoiPolygon> ret = std::make_shared<RoiPolygon>(*this);
        ret->detachAttitude(attitude);
        return ret;
    }
}

// 从相对于图像的区域，转到绑定的姿态位置的区域
std::shared_ptr<Roi> RoiPolygon::imageToAttitude(const Attitude *attitude) const {
    if (attitude == nullptr) {
        // 变量不存在
        return std::dynamic_pointer_cast<Roi>(const_cast<RoiPolygon *>(this)->shared_from_this());
    }
    else {
        std::shared_ptr<RoiPolygon> ret = std::make_shared<RoiPolygon>(*this);
        ret->attachAttitude(attitude);
        return ret;
    }
}

// 解绑姿态变量（从相对于姿态的坐标，转成图像坐标）
void RoiPolygon::detachAttitude(const Attitude *attitude) {
    if (!attitude) return;
    // 计算各个点旋转后的坐标
    for (cv::Point2f &point : this->points_) {
        point = cvAddAttitude(point, *attitude);
    }
}

// 绑定姿态变量（从图像坐标，转成相对于姿态的坐标）
void RoiPolygon::attachAttitude(const Attitude *attitude) {
    if (!attitude) return;
    // 计算各个点旋转后的坐标
    for (cv::Point2f &point : this->points_) {
        point = cvRemoveAttitude(point, *attitude);
    }
}

void RoiPolygon::paint(UiRoi &uiRoi, QPainter &qp, bool isFirstInGroup, bool isDrawHotSpot) {
    this->paint(uiRoi, qp, isFirstInGroup, isDrawHotSpot, nullptr);
}

void RoiPolygon::paint(UiRoi &uiRoi, QPainter &qp, bool isFirstInGroup, bool isDrawHotSpot, const std::shared_ptr<QPointF> &movingPoint) {
    //QSizeF widgetSize = qp.clipBoundingRect().size();
    qp.save();
    qp.setRenderHints(QPainter::Antialiasing);

    const QColor &color = uiRoi.allocateColor(color_);

    // 画自由图像
    qreal lineWidth = ROI_LINE_WIDTH;
    QPen pen;
    uiRoi.setPenStyle(&pen, color, lineWidth);
    pen.setCapStyle(Qt::PenCapStyle::RoundCap);
    pen.setJoinStyle(Qt::PenJoinStyle::RoundJoin);
    qp.setPen(pen);

    std::vector<QPointF> points;
    if (points_.size() > 0) {
        for(const auto &point: points_) {
            points.push_back(uiRoi.imageToWidget(cv2qt(point)));
        }
        if (movingPoint)
            points.push_back(uiRoi.imageToWidget(*movingPoint));
        //else
        //    points.push_back(uiRoi.imageToWidget(cv2qt(points_.front())));

        bool isAllPointNormal = true;
        for (const QPointF &point : points) {
            if (!qIsFinite(point))
                isAllPointNormal = false;
        }

        if (isAllPointNormal) {
            if(!movingPoint && !static_cast<const UiRoiPolygon &>(uiRoi).impl_->isPolyline_)
                qp.drawPolygon(points.data(), (int)points.size());
            else
                qp.drawPolyline(points.data(), (int)points.size());
            //qp.drawPolyline(points.data(), (int)points.size(), Qt::WindingFill);
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
        for (const cv::Point2f &point : points_) {
            const QPointF &qPoint = uiRoi.imageToWidget(cv2qt(point));
            if (qIsFinite(qPoint))
                qp.drawEllipse(qPoint, radius, radius);
        }
    }


    // 画标签文字
    if (!this->text_->empty() && points.size() >= 1) {
        float radian = 0;
        bool drawOnTop = true;
        bool drawOnLeft = false;
        std::vector<QPointF> rotatedPoints;
        rotatedPoints.reserve(points.size());

        QPointF start = points[0];
        if (points.size() >= 2) {
            radian = ::atan2(points[1].y() - points[0].y(), points[1].x() - points[0].x());
            for(const auto &point: points)
                rotatedPoints.push_back(qRotatedCoordinate(QPointF(0, 0), point, -radian));
            start = rotatedPoints[0];
        }
        else {
            for (const auto &point : points)
                rotatedPoints.push_back(point);
        }

        // 根据第二个点的位置，决定画左侧或右侧
        if (rotatedPoints.size() >= 2) {
            drawOnLeft = (start.x()> rotatedPoints[1].x());
        }
        // 根据第三个点的位置，决定画上侧或下侧
        if (rotatedPoints.size() >= 3) {
            drawOnTop = (rotatedPoints[2].y() >= 0);
        }

        // 获取最大长度
        int maxLength = 0;
        if (drawOnLeft) {
            for (size_t i = 1; i < rotatedPoints.size(); ++i) {
                const auto &point = rotatedPoints[i];
                if (point.x() < start.x()) {
                    int length = start.x() - point.x();
                    if (length > maxLength) maxLength = length;
                }
            }
        }
        else {
            for (size_t i = 1; i < rotatedPoints.size(); ++i) {
                const auto &point = rotatedPoints[i];
                if (point.x() > start.x()) {
                    int length = point.x() - start.x();
                    if (length > maxLength) maxLength = length;
                }
            }
        }


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
            if (textRect.width() + 18 < maxLength) {
                minFontSize = testFontSize;
            }
            else {
                maxFontSize = testFontSize;
            }
        }
        LOG_DEBUG("textRect = {}", textRect);

        QPointF point = start;
        if (drawOnTop) {
            point.setY(point.y() - textRect.height() - ROI_LABEL_MARGIN);
        }
        else {
            point.setY(point.y() + ROI_LABEL_MARGIN);
        }
        if (drawOnLeft) {
            point.setX(point.x() - textRect.width() - ROI_LABEL_MARGIN - lineWidth);
        }
        else {
            point.setX(point.x() + ROI_LABEL_MARGIN + lineWidth);
        }

        QSize textSize = { textRect.width() + 8, textRect.height() };
        textRect = QRect{ point.toPoint(), textSize };
        // 位置保存到roi对象
        UiRoiPolygon *roiFreeDraw = dynamic_cast<UiRoiPolygon *>(&uiRoi);
        if (roiFreeDraw != nullptr && isFirstInGroup) {
            if (roiFreeDraw->impl_->textRect_)
                *roiFreeDraw->impl_->textRect_ = textRect;
            else
                roiFreeDraw->impl_->textRect_ = std::make_shared<QRectF>(textRect);
        }

        LOG_DEBUG("textRect = {}", textRect);
        //qp.drawRect(textRect);
        qp.save();
        qp.rotate(radian * 180 / M_PI);
        uiRoi.drawHollowFont(qp, font, text, textRect, color);
        qp.restore();
        //qp.setClipRect(textRect, Qt::ClipOperation::IntersectClip);
    }

    qp.restore();
}

bool RoiPolygon::equals(const Roi& other) const {
    RoiPolygon* otherPolygon = dynamic_cast<RoiPolygon*>(const_cast<Roi*>(&other));
    if (otherPolygon == nullptr) {
        return false;
    }

    return this->points_ == otherPolygon->points_;
}

static std::shared_ptr<Roi> insureRoi(const std::string &roiType, const std::shared_ptr<Roi> &roi) {
    if (roi) {
        if (roiType != roi->roiType_)
            throw std::invalid_argument(fmt::format("roiType not match  = {}, {}", roiType, roi->roiType_));
        return roi;
    }

    auto data = std::make_shared<RoiPolygon>(roiType);
    return data;
}


UiRoiPolygon::UiRoiPolygon(const std::string &roiType, const std::shared_ptr<Roi> &roi)
    : UiRoi(insureRoi(roiType, roi))
    , impl_(new Impl(this)) {
    // 自由绘制格式不需要中心点和旋转

    auto updateWidget = [=](BindingData::EventType eventType, const boost::any &eventValue, const boost::any &src) {
        // 不管 points_ 为何种事件，都转弯编辑事件上报
        eventType = BindingData::kEdit;
        (void)eventValue;
        impl_->data_->triggerEvent(eventType, boost::any(), (src.empty() ? impl_->data_->triggerSource() : src));
        this->update();
    };

    impl_->data_ = std::dynamic_pointer_cast<RoiPolygon>(data_);
    impl_->isPolyline_ = (impl_->data_->roiType_ == "polyline");

    //impl_->eventHolder_ += bindDataToObject(static_cast<BindingData *>(this), static_cast<BindingData *>(&impl_->data_->boundVars_), [=]() {}, updateWidget);
    impl_->eventHolder_ += bindDataToObject(static_cast<BindingData *>(this), static_cast<BindingData *>(&impl_->data_->points_), [=]() {}, updateWidget);
    impl_->eventHolder_ += bindDataToObject(static_cast<BindingData *>(this), static_cast<BindingData *>(&impl_->data_->color_), [=]() {}, updateWidget);
    impl_->eventHolder_ += bindDataToObject(static_cast<BindingData *>(this), static_cast<BindingData *>(&impl_->data_->text_), [=]() {}, updateWidget);
}


UiRoiPolygon::~UiRoiPolygon() {
    impl_->eventHolder_.clear();
}


bool UiRoiPolygon::isOnObject(const QPointF &pos, Qt::CursorShape &mouseDirection) {
    std::shared_ptr<LayerManager> layerManager = layerManager_.lock();
    if (!layerManager || layerManager->isLayerToolDisabled(this))
        return false;

    const auto &data = impl_->data_;
    if (data->hasEmptyAttitudes())
        return false;

    // 判断是否落在热点区域
    if (impl_->movingPoint_ || data->points_.size() == 0) {
        mouseDirection = Qt::CrossCursor;
    }
    else if (impl_->isOnCorner(pos, nullptr)) {
        mouseDirection = Qt::OpenHandCursor;
    }
    // 判断是否落在边长区域
    else if (impl_->isOnBorder(pos, nullptr, nullptr)) {
        mouseDirection = Qt::SizeAllCursor;
    }
    else if (impl_->isOnHotSpot_text(pos)) {
        mouseDirection = Qt::IBeamCursor;
    }
    else {
        mouseDirection = Qt::ArrowCursor;
        return false;
    }
    return true;
}

void UiRoiPolygon::paint(QPainter &qp, bool isDrawHotSpot) {
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
        std::dynamic_pointer_cast<RoiPolygon>(roi)->paint(*this, qp,
            isFirst,
            isDrawHotSpot,
            impl_->movingPoint_);
        isFirst = false;
    }
}

bool UiRoiPolygon::Impl::isOnHotSpot_text(const QPointF &pos) const {

    if (!this->textRect_)
        return false;

    const auto &data = this->data_;
    const auto &points = data->points_;
    if (points.size() <= 1)
        return false;

    double radian = 0;
    const auto &start = points[0];
    if (data->points_.size() >= 2) {
        radian = ::atan2(points[1].y - points[0].y, points[1].x - points[0].x);
    }


    // 注意 textRect_ 相对于窗口的旋转后的坐标

    // 相对于窗口摆正后的坐标
    QPointF textPos = qRotatedCoordinate(QPointF(0, 0), this->textRect_->topLeft(), radian);
    // 相对于原图摆正后的坐标
    textPos = thiz_->widgetToImage(textPos);

    // 相对于roi摆正后的坐标
    textPos = qRotatedCoordinate(cv2qt(start), textPos, -radian);

    QPointF pos2 = qRotatedCoordinate(cv2qt(start), pos, -radian);
    QRectF rextRect = { textPos, thiz_->widgetToImage(this->textRect_->size()) };

    LOG_DEBUG("textRect_ = {}, {}, {}", rextRect, pos, pos2);
    LOG_DEBUG("rextRect.isOnObject(pos) = {}", rextRect.contains(pos2));

    if (rextRect.contains(pos2))
        return true;

    return false;
}

bool UiRoiPolygon::Impl::isOnCorner(const QPointF &pos, size_t *cornerIndex) const {
    // 计算鼠标所在何处边沿
    double r = thiz_->widgetToImage(ROI_MASK_MARGIN); // 角度热点区域的半径的平方
    double r2 = r * r;
    const auto &data = this->data_;
    for (size_t i = 0; i < data->points_.size(); ++i) {
        const cv::Point2f &point = data->points_[i];
        if (qDistance2(pos, cv2qt(point)) <= r2) {
            if(cornerIndex != nullptr) *cornerIndex = i;
            return true;
        }
    }
    return false;
}

bool UiRoiPolygon::Impl::isOnBorder(const QPointF & pos, size_t *startIndex, size_t *endIndex) const {
    const auto &data = this->data_;

    int size = (int)data->points_.size();
    if (this->isPolyline_) --size; // 不封闭区间，最后一个点不检测

    double halfBorder = thiz_->widgetToImage(ROI_MASK_MARGIN + ROI_LINE_WIDTH / 2);
    for (int i = 0; i < size; ++i) {
        // 由两个点构造一个线段区域
        const cv::Point2f &start = data->points_[i];
        const cv::Point2f &end = data->points_[(i + 1) % data->points_.size()];
        double radian = ::atan2(end.y - start.y, end.x - start.x);

        // 让线段转圈到躺平
        QPointF end_ = qRotatedCoordinate(cv2qt(start), cv2qt(end), -radian);
        QPointF pos_ = qRotatedCoordinate(cv2qt(start), pos, -radian);

        LOG_DEBUG("ttt ppp = {}, {}, {}, {}, {}\n", start, end, end_, pos, pos_);

        QRectF rect = QRectF(start.x,
                             start.y - halfBorder,
                             end_.x() - start.x,
                             halfBorder * 2);

        if (rect.contains(pos_)) {
            if (startIndex != nullptr) *startIndex = i;
            if (endIndex != nullptr) *endIndex = (i + 1) % data->points_.size();
            return true;
        }
    }

    return false;
}

void UiRoiPolygon::Impl::showPosition() {
    std::string text;
    bool first = true;
    for (const auto &point : data_->points_) {
        if(first)
            text += fmt::format("{:.3f}", point);
        else
            text += fmt::format(",{:.3f}", point);
        first = false;
    }

    thiz_->showStatusMessage(QString::fromStdString(text), 3000);
}

// 初始状态，自由绘制
promise::Promise UiRoiPolygon::runInitialize() {
    std::shared_ptr<LayerManager> layerManager = layerManager_.lock();
    this->widget()->grabMouse(Qt::CrossCursor);
    layerManager->setFocusedLayerTool(this);

    // 等待左键点击
    return pendingTask_ = doWhile(PM_LOC, [=](const DeferLoop &loop) {
        // 鼠标移动，根据位置设置鼠标形状
        auto onMoving = [=](QEvent *event) {
            // 设定移动中的顶点
            this->widget()->setCursor(Qt::CrossCursor);
            return loop.doContinue(PM_LOC); // will goto doWhile
        };
        auto waitHoverMove = layerManager->waitEvent(this, QEvent::HoverMove).then(PM_LOC, onMoving);
        auto waitMouseMove = layerManager->waitEvent(this, QEvent::MouseMove).then(PM_LOC, onMoving);

        auto waitMouseButtonPress = layerManager->waitEvent(this, QEvent::MouseButtonPress).then(PM_LOC, [=](QEvent *event) {
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
        if (mouseEvent->buttons().testFlag(Qt::LeftButton)) {
            impl_->data_->isInitialized_ = true;
            insertPoint(impl_->data_->points_.size(), widgetToImage(mouseEvent->pos()));
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
        // 设定鼠标独占状态
        layerManager->setFocusedLayerTool(this);

        // 鼠标双击事件
        std::shared_ptr<std::shared_ptr<std::chrono::steady_clock::time_point>> singleClickTime =
            std::make_shared<std::shared_ptr<std::chrono::steady_clock::time_point>>();
        std::shared_ptr<std::shared_ptr<std::chrono::steady_clock::time_point>> doubleClickTime =
            std::make_shared<std::shared_ptr<std::chrono::steady_clock::time_point>>();
        const int DOUBLE_CLICK_TIME_MS = 500;

        return doWhile(PM_LOC, [=](const DeferLoop &loop) {
            // 鼠标移动，根据位置设置鼠标形状
            auto onMoving = [=](QEvent *event) {
                const QPointF &pos = widgetToImage(getMousePos(event));

                // 设定移动中的顶点
                this->widget()->setCursor(Qt::CrossCursor);
                this->impl_->movingPoint_ = std::make_shared<QPointF>(pos);
                impl_->showPosition();
                this->update();
                return loop.doContinue(PM_LOC); // will goto doWhile
            };
            auto waitHoverMove = layerManager->waitEvent(this, QEvent::HoverMove).then(PM_LOC, onMoving);
            auto waitMouseMove = layerManager->waitEvent(this, QEvent::MouseMove).then(PM_LOC, onMoving);

            // 达到三个点以上，允许绘制结束
            auto onFinished = [=](QEvent *event) {
                QMouseEvent *mouseEvent = dynamic_cast<QMouseEvent *>(event);
                if (!mouseEvent) return loop.doContinue(PM_LOC); // will goto doWhile
                //const QPointF &pos = widgetToImage(mouseEvent->pos());

                if (impl_->data_->points_.size() < 3) {
                    loop.doContinue(PM_LOC);
                }
                else {
                    PTI;
                    // 清除移动中的顶点
                    this->impl_->movingPoint_.reset();
                    impl_->showPosition();
                    this->update();
                    loop.doBreak(PM_LOC);
                }
            };

            // 鼠标单击事件处理
            auto waitMouseButtonPress = layerManager->waitEvent(this, QEvent::MouseButtonPress).then(PM_LOC, [=](QEvent *event) {
                QMouseEvent *mouseEvent = dynamic_cast<QMouseEvent *>(event);
                if (!mouseEvent) return loop.doContinue(PM_LOC); // will goto doWhile
                const QPointF &pos = widgetToImage(mouseEvent->pos());

                if (mouseEvent->buttons().testFlag(Qt::LeftButton)) {
                    if (*doubleClickTime
                        && std::chrono::steady_clock::now() - **doubleClickTime < std::chrono::milliseconds(DOUBLE_CLICK_TIME_MS)) {
                        // 双击时间间隔内，不做处理
                    }
                    else {
                        // 增加一个顶点
                        *singleClickTime = std::make_shared<std::chrono::steady_clock::time_point>(std::chrono::steady_clock::now());
                        insertPoint(impl_->data_->points_.size(), pos);
                        impl_->showPosition();
                        this->update();
                        loop.doContinue(PM_LOC);
                    }
                }
                else {
                    onFinished(event);
                }
            });

            // 鼠标双机事件处理
            auto waitMouseButtonDblClick = layerManager->waitEvent(this, QEvent::MouseButtonDblClick).then(PM_LOC, [=](QEvent *event) {
                *doubleClickTime = std::make_shared<std::chrono::steady_clock::time_point>(std::chrono::steady_clock::now());

                if (*singleClickTime &&
                    std::chrono::steady_clock::now() - **singleClickTime < std::chrono::milliseconds(DOUBLE_CLICK_TIME_MS)) {
                    // 双击时间间隔内，去除最后的单击处理
                    size_t size = impl_->data_->points_.size();
                    if (size > 0) {
                        erasePoint(size - 1);
                        impl_->showPosition();
                        this->update();
                    }
                }
                return event;
            }).then(PM_LOC, onFinished);

            // 三个事件只处理其中任一个
            raceAndReject2nd(loop, { waitMouseMove, waitHoverMove, waitMouseButtonPress, waitMouseButtonDblClick });
        });
    }).

    // 取消事件独占状态
    finally(PM_LOC, [=]() {
        // 如果正在初始绘制移动中被打断，设为绘制失败，清除所有点
        if (this->impl_->movingPoint_) {
            if (impl_->data_->points_.size() < 3) {
                clearPoints();
            }
            this->impl_->movingPoint_.reset();
            this->widget();
        }

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

// 绘制完可编辑状态
Promise UiRoiPolygon::runEdit() {
    std::shared_ptr<LayerManager> layerManager = layerManager_.lock();
    return pendingTask_ = (data_->isRef_ || this->isReadonly_)
        ? promise::newPromise(PM_LOC)
        : doWhile(PM_LOC, [=](const DeferLoop &loop) {
        // 鼠标移动，根据位置设置鼠标形状
        auto onMoving = [=](QEvent *event) {
            const QPointF &pos = widgetToImage(getMousePos(event));

            // 判断是否落在热点区域
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
            QPointF pos = widgetToImage(mouseEvent->pos());

            // 判断是否落在热点区域
            size_t cornerIndex = 0;
            size_t startIndex = 0;
            size_t endIndex = 0;
            if (impl_->isOnCorner(pos, &cornerIndex)) {
                if (mouseEvent->buttons().testFlag(Qt::RightButton)) {
                    runCornerContextMenu(mouseEvent->pos(), cornerIndex).then(PM_LOC, loop);
                    return;
                }
                else if (mouseEvent->buttons().testFlag(Qt::LeftButton)) {
                    layerManager->setFocusedLayerTool(this);
                    runResize(pos, cornerIndex).finally(PM_LOC, [=]() {layerManager->setFocusedLayerTool(nullptr); }).then(PM_LOC, loop);
                    return;
                }
            }
            // 判断是否落在边长区域
            else if (impl_->isOnBorder(pos, &startIndex, &endIndex)) {
                if (mouseEvent->buttons().testFlag(Qt::RightButton)) {
                    runBorderContextMenu(mouseEvent->pos(), startIndex, endIndex).then(PM_LOC, loop);
                    return;
                }
                else if (mouseEvent->buttons().testFlag(Qt::LeftButton)) {
                    layerManager->setFocusedLayerTool(this);
                    runMove(pos).finally(PM_LOC, [=]() {layerManager->setFocusedLayerTool(nullptr); }).then(PM_LOC, loop);
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
    });;

}


Promise UiRoiPolygon::runCornerContextMenu(const QPointF &mousePos, size_t cornerIndex) {
    (void)mousePos;
    return runContextMenu(mousePos, [=](QMenu *menu, const FnAddMenu &addMenu) {
        menu->addSeparator();
        QAction *actionDelete = menu->addAction(QIcon(QPixmap(":/icons/remove_point.png")), QObject::tr("Delete the point"));
        addMenu(actionDelete, [=] {
            return newPromise(PM_LOC, [=](Defer defer) {
                erasePoint(cornerIndex);
                std::shared_ptr<LayerManager> layerManager = layerManager_.lock();
                if(layerManager)
                    layerManager->notifyMove(this);
                defer.resolve(PM_LOC);
            });
        });
    });
}


Promise UiRoiPolygon::runBorderContextMenu(const QPointF &mousePos, size_t startIndex, size_t endIndex) {
    (void)startIndex;
    return runContextMenu(mousePos, [=](QMenu *menu, const FnAddMenu &addMenu) {
        menu->addSeparator();
        QAction *actionAdd = menu->addAction(QIcon(QPixmap(":/icons/add_point.png")), QObject::tr("Add a point"));
        addMenu(actionAdd, [=] {
            return newPromise(PM_LOC, [=](Defer defer) {
                QPointF pos = widgetToImage(mousePos);
                insertPoint(endIndex, pos);
                std::shared_ptr<LayerManager> layerManager = layerManager_.lock();
                if (layerManager)
                    layerManager->notifyMove(this);
                defer.resolve(PM_LOC);
            });
        });
    });
}

Promise UiRoiPolygon::runMove(const QPointF &mousePos) {
    // 旋转状态
    std::shared_ptr<LayerManager> layerManager = layerManager_.lock();

    //const std::shared_ptr<Attitude> &attitude = impl_->data_->getAttitude0();
    VList<cv::Point2f> oldPoints = impl_->data_->points_;

    return doWhile(PM_LOC, [=](const DeferLoop &loop) {
        // 抓住鼠标
        this->widget()->grabMouse(Qt::SizeAllCursor);

        // 鼠标移动，旋转本组件
        auto waitMouseMove = layerManager->waitEvent(this, QEvent::MouseMove).then(PM_LOC, [=](QEvent *event) {
            QMouseEvent *mouseEvent = dynamic_cast<QMouseEvent *>(event);
            if (!mouseEvent) return loop.doContinue(PM_LOC); // will goto doWhile

            const QPointF &pos = widgetToImage(mouseEvent->pos());
            auto diff = (pos - mousePos);

            {
                auto onScopeExit = this->data_->scopedTriggerSource(Widget::kFromConfigRois);
                for (size_t i = 0; i < impl_->data_->points_.size() && i < oldPoints.size(); ++i) {
                    impl_->setPoint(i, cv2qt(oldPoints[i]) + diff, false);
                }
                impl_->data_->points_.triggerEvent(BindingData::kEdit);
            }

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


Promise UiRoiPolygon::runResize(const QPointF &mousePos, size_t cornerIndex) {
    // 尺寸拉伸状态
    std::shared_ptr<LayerManager> layerManager = layerManager_.lock();

    //const std::shared_ptr<Attitude> &attitude = impl_->data_->getAttitude0();
    //cv::Point2f oldCorner = cvAddAttitude(impl_->data_->points_[cornerIndex], *attitude);
    cv::Point2f oldCorner = impl_->data_->points_[cornerIndex];
    return doWhile(PM_LOC, [=](const DeferLoop &loop) {
        // 抓住鼠标
        this->widget()->grabMouse(Qt::ClosedHandCursor);

        // 鼠标移动，拉伸
        auto waitMouseMove = layerManager->waitEvent(this, QEvent::MouseMove).then(PM_LOC, [=](QEvent *event) {
            QMouseEvent *mouseEvent = dynamic_cast<QMouseEvent *>(event);
            if (!mouseEvent) return loop.doContinue(PM_LOC); // will goto doWhile

            const QPointF &pos = widgetToImage(mouseEvent->pos());
            auto diff = (pos - mousePos);
            setPoint(cornerIndex, cv2qt(oldCorner) + diff);

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

void UiRoiPolygon::insertPoint(size_t offset, const QPointF &point) {

    auto onScopeExit = this->data_->scopedTriggerSource(Widget::kFromConfigRois);

    // 转换为不含姿态调整的位置
    //const std::shared_ptr<Attitude> &attitude = impl_->data_->getAttitude0();
    //const cv::Point2f &pt = cvRemoveAttitude(qt2cv(point), *attitude);
    const cv::Point2f &pt = qt2cv(point);

    if (offset == impl_->data_->points_.size())
        impl_->data_->points_.push_back(pt);
    else
        impl_->data_->points_.insert(impl_->data_->points_.begin() + offset, pt);

    //data_->set("height", roiToWidget(height_));
}

void UiRoiPolygon::erasePoint(size_t offset) {
    auto onScopeExit = this->data_->scopedTriggerSource(Widget::kFromConfigRois);
    impl_->data_->points_.erase(impl_->data_->points_.begin() + offset);
}

void UiRoiPolygon::clearPoints() {
    auto onScopeExit = this->data_->scopedTriggerSource(Widget::kFromConfigRois);
    impl_->data_->points_.clear();
}


void UiRoiPolygon::Impl::setPoint(size_t offset, const QPointF &point, bool isTriggerEvent) {
    // 转换为不含姿态调整的位置
    //const std::shared_ptr<Attitude> &attitude = impl_->data_->getAttitude0();
    //const cv::Point2f &pt = cvRemoveAttitude(qt2cv(point), *attitude);
    const cv::Point2f &pt = qt2cv(point);

    if (this->data_->points_[offset] != pt) {
        this->data_->points_[offset] = pt;

        if(isTriggerEvent)
            this->data_->points_.triggerEvent(BindingData::kEdit);
        //data_->set("height", roiToWidget(height_));
    }
}

void UiRoiPolygon::setPoint(size_t offset, const QPointF &point) {
    auto onScopeExit = this->data_->scopedTriggerSource(Widget::kFromConfigRois);
    impl_->setPoint(offset, point, true);
}
