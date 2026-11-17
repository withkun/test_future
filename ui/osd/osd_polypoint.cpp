#include "ui/osd/osd_polypoint.h"
#include "utils/qt_utils.h"
#include "utils/cv_utils.h"
#include "utils/log.h"
#include "utils/event_holder.h"
#include "utils/any_pro.h"
#include <QPainter>

AP_REGISTRATION{
    anypro::registration::class_<OsdPolyPoint>()
        .base<Osd>()
        .property_readonly("points", [](OsdPolyPoint *thiz) -> VList<cv::Point2f> * {
            return &thiz->points_;
        })
        .property("radian", &OsdPolyPoint::radian_)
        .property("width", &OsdPolyPoint::width_)
        .property("showAngle", &OsdPolyPoint::showAngle_);

    anypro::registration::class_<OsdPolyGon>()
        .base<OsdPolyPoint>();

    anypro::registration::class_<OsdPolyLine>()
        .base<OsdPolyPoint>();

    // OsdPolyPoint也是可视化变量
    tcRegister<OsdPolyPoint>({ TypeCategory::kVisiable });

    // 注册OSD变量
    registerOsd<OsdPolyGon, UiOsdPolyPoint>("polygon", UiOsdPolyPoint::tr("PolyGon").toStdString());
    registerOsd<OsdPolyLine, UiOsdPolyPoint>("polyline", UiOsdPolyPoint::tr("PolyLine").toStdString());
}

static void osdInitTable(OsdPolyPoint *osd) {
    auto disabled = osd->cfgTable_->scopedDisableChangedEvent();
    osd->cfgTable_->push_back({ "points", &osd->points_ });
    osd->cfgTable_->push_back({ "radian", &osd->radian_ });
    osd->cfgTable_->push_back({ "width", &osd->width_ });
    osd->cfgTable_->push_back({ "showAngle", &osd->showAngle_ });
}

OsdPolyPoint::OsdPolyPoint()
    : Osd("polypoint")
    , points_()
    , radian_(0)
    , width_(OSD_DEFAULT_LINE_WIDTH)
    , showAngle_(false) {
    osdInitTable(this);
}

OsdPolyPoint::OsdPolyPoint(const std::string &osdType)
    : Osd(osdType)
    , points_()
     , radian_(0)
    , width_(OSD_DEFAULT_LINE_WIDTH)
    , showAngle_(false) {
    osdInitTable(this);
}

OsdPolyPoint::OsdPolyPoint(const OsdPolyPoint &right)
    : Osd(right) {
    osdInitTable(this);
    this->points_ = right.points_;
    this->radian_ = right.radian_;
    this->width_ = right.width_;
    this->showAngle_ = right.showAngle_;
}

OsdPolyGon::OsdPolyGon()
    : OsdPolyPoint("polygon") {
}

OsdPolyGon::OsdPolyGon(const OsdPolyGon &right)
    : OsdPolyPoint(right) {
}

OsdPolyLine::OsdPolyLine()
    : OsdPolyPoint("polyline") {
}

OsdPolyLine::OsdPolyLine(const OsdPolyLine &right)
    : OsdPolyPoint(right) {
}


class UiOsdPolyPoint::Impl {
public:
    std::shared_ptr<OsdPolyPoint> data_;        // 数据对象
    bool                          isPolyline_;  // 是polygon还是polyline
    std::shared_ptr<QRectF>       textRect_;    // 文本显示的位置
    EventHolder                   eventHolder_; // 数据绑定句柄
};



UiOsdPolyPoint::UiOsdPolyPoint(const std::shared_ptr<Osd> &osd)
    : UiOsd(osd)
    , impl_(new Impl) {

    auto updateWidget = [=](BindingData::EventType eventType, const boost::any &eventValue) {
        impl_->data_->triggerEvent(eventType, eventValue);
        this->update();
    };

    impl_->data_ = std::dynamic_pointer_cast<OsdPolyPoint>(data_);
    impl_->isPolyline_ = (impl_->data_->osdType_ == "polyline");

    impl_->eventHolder_ += bindDataToObject(static_cast<BindingData *>(this), static_cast<BindingData *>(&impl_->data_->radian_), [=]() {}, updateWidget);
    impl_->eventHolder_ += bindDataToObject(static_cast<BindingData *>(this), static_cast<BindingData *>(&impl_->data_->width_), [=]() {}, updateWidget);
    impl_->eventHolder_ += bindDataToObject(static_cast<BindingData *>(this), static_cast<BindingData *>(&impl_->data_->color_), [=]() {}, updateWidget);
    impl_->eventHolder_ += bindDataToObject(static_cast<BindingData *>(this), static_cast<BindingData *>(&impl_->data_->points_), [=]() {}, updateWidget);
    impl_->eventHolder_ += bindDataToObject(static_cast<BindingData *>(this), static_cast<BindingData *>(&impl_->data_->showAngle_), [=]() {}, updateWidget);
    impl_->eventHolder_ += bindDataToObject(static_cast<BindingData *>(this), static_cast<BindingData *>(&impl_->data_->text_), [=]() {}, updateWidget);
    impl_->eventHolder_ += bindDataToObject(static_cast<BindingData *>(this), static_cast<BindingData *>(&impl_->data_->labels_), [=]() {}, updateWidget);

}


UiOsdPolyPoint::~UiOsdPolyPoint() {
    impl_->eventHolder_.clear();
}

bool UiOsdPolyPoint::isOnObject(const QPointF &pos, Qt::CursorShape &mouseDirection) {
    auto isOnObject = [this, &pos]() {
        const auto &data = impl_->data_;

        int size = (int)data->points_.size();
        if (impl_->isPolyline_) --size; // 不封闭区间，最后一个点不检测

        
        cv::Point2f pos2 = qt2cv(pos);
        if (data->points_.size() != 0) {
            cv::Point2f point0 = data->points_[0];
            pos2 = cvRotatedCoordinate(point0, pos2, -data->radian_);
        }

        double borderWidth = this->widgetToImage(std::max<double>(ROI_MASK_MARGIN + ROI_LINE_WIDTH, data->width_));
        for (int i = 0; i < size; ++i) {
            // 由两个点构造一个线段区域
            const cv::Point2f &start = data->points_[i];
            const cv::Point2f &end = data->points_[(i + 1) % data->points_.size()];
            double radian = ::atan2(end.y - start.y, end.x - start.x);

            // 让线段转圈到躺平
            const cv::Point2f end_ = cvRotatedCoordinate(start, end, -radian);
            const cv::Point2f pos_ = cvRotatedCoordinate(start, pos2, -radian);

            cv::Rect2d rect = {
                start.x,
                start.y - borderWidth / 2,
                end_.x - start.x,
                borderWidth };

            if (rect.contains(pos_)) {
                return true;
            }
        }

        return false;
    };

    bool ret = isOnObject();
    if (ret)
        mouseDirection = Qt::PointingHandCursor;
    else
        mouseDirection = Qt::ArrowCursor;
    return ret;
}

void UiOsdPolyPoint::paint(QPainter &qp, bool isDrawHotSpot) {
    //QSizeF widgetSize = qp.clipBoundingRect().size();
    const std::shared_ptr<OsdPolyPoint> &data = impl_->data_;
    qp.save();
    qp.setRenderHints(QPainter::Antialiasing);

    const QColor &color = this->allocateColor(data->color_);
    //LOG_DEBUG("color = {}, {}", color.name(), data->color_->name());

    // 画自由图像
    qreal lineWidth = data->width_;
    QPen pen;
    this->setPenStyle(&pen, color, lineWidth);
    pen.setCapStyle(Qt::PenCapStyle::RoundCap);
    pen.setJoinStyle(Qt::PenJoinStyle::RoundJoin);
    qp.setPen(pen);

    std::vector<QPointF> points;
    for (const auto &point : data->points_) {
        points.push_back(this->imageToWidget(cv2qt(point)));
    }
    if (points.size() > 0) {
        QPointF point0 = points[0];

        qp.translate(point0);
        qp.rotate(data->radian_ * 180 / M_PI);

        if (points.size() > 0) {

            bool isAllPointNormal = true;
            for (QPointF &point : points) {
                point -= point0;
                if (!qIsFinite(point))
                    isAllPointNormal = false;
            }

            if (isAllPointNormal) {
                if (impl_->isPolyline_)
                    qp.drawPolyline(points.data(), (int)points.size());
                else
                    qp.drawPolygon(points.data(), (int)points.size());
            }
        }

        // 画热点
        if (data->showAngle_) {
            QColor color2;
            qreal lightness = color.lightnessF();
            lightness = lightness * 0.9;
            color2.setHslF(color.hslHueF(), color.hslSaturationF(), lightness, color.alphaF());

            QBrush brush2(color2);
            QPen pen2;
            this->setPenStyle(&pen2, color2, lineWidth);
            qp.setPen(pen2);
            qp.setBrush(brush2);
            qreal radius = data->width_ + 1;
            for (const QPointF &point : points) {
                if (qIsFinite(point))
                    qp.drawEllipse(point, radius, radius);
            }
        }
    }

    // 画标签文字  
    bool isOsdLabelVisible = this->layerManager()->isOsdLabelVisible_;
    const std::string &textEx = isOsdLabelVisible
        ? strJoin({ impl_->data_->labels_, impl_->data_->text_ }, ",", true)
        : (std::string)impl_->data_->text_;
    if (!textEx.empty() && points.size() >= 1) {
        float radian = 0;
        bool drawOnTop = true;
        bool drawOnLeft = false;
        std::vector<QPointF> rotatedPoints;
        rotatedPoints.reserve(points.size());

        QPointF start = points[0];
        if (points.size() >= 2) {
            radian = ::atan2(points[1].y() - points[0].y(), points[1].x() - points[0].x());
            for (const auto &point : points)
                rotatedPoints.push_back(qRotatedCoordinate(QPointF(0, 0), point, -radian));
            start = rotatedPoints[0];
        }
        else {
            for (const auto &point : points)
                rotatedPoints.push_back(point);
        }

        // 根据第二个点的位置，决定画左侧或右侧
        if (rotatedPoints.size() >= 2) {
            drawOnLeft = (start.x() > rotatedPoints[1].x());
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
        QString text = QString::fromStdString(textEx);
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
        if (impl_->textRect_) *impl_->textRect_ = textRect;
        else impl_->textRect_ = std::make_shared<QRectF>(textRect);

        LOG_DEBUG("textRect = {}", textRect);
        //qp.drawRect(textRect);
        qp.save();
        qp.rotate(radian * 180 / M_PI);
        this->drawHollowFont(qp, font, text, textRect, color);
        qp.restore();
        //qp.setClipRect(textRect, Qt::ClipOperation::IntersectClip);
    }

    qp.restore();
}

