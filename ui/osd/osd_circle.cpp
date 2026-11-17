#include "ui/osd/osd_circle.h"
#include "utils/qt_utils.h"
#include "utils/log.h"
#include "utils/event_holder.h"
#include "utils/any_pro.h"
#include <QPainter>

AP_REGISTRATION {
    anypro::registration::class_<OsdCircle>()
        .base<Osd>()
        .property("center", &OsdCircle::center_)
        .property("radian", &OsdCircle::radian_)
        .property("radius", &OsdCircle::radius_)
        .property("width", &OsdCircle::width_)
        .property("cs", &OsdCircle::cs_);

    // 注册OSD变量
    registerOsd<OsdCircle, UiOsdCircle>("circle", UiOsdCircle::tr("Circle").toStdString());
}

static void osdInitTable(OsdCircle *osd) {
    auto disabled = osd->cfgTable_->scopedDisableChangedEvent();
    osd->cfgTable_->push_back({ "center", &osd->center_ });
    osd->cfgTable_->push_back({ "radian", &osd->radian_ });
    osd->cfgTable_->push_back({ "radius", &osd->radius_ });
    osd->cfgTable_->push_back({ "width",  &osd->width_ });
    osd->cfgTable_->push_back({ "cs",     &osd->cs_ });
}

OsdCircle::OsdCircle()
    : Osd("circle")
    , center_(cv::Point2f(0, 0))
    , radian_(0)
    , radius_(OSD_DEFAULT_VISIABLE_RECT.width() / 2)
    , width_(OSD_DEFAULT_LINE_WIDTH)
    , cs_(OsdCircle::kUseImageCs) {
    osdInitTable(this);
}

OsdCircle::OsdCircle(const OsdCircle &right)
    : Osd(right) {
    osdInitTable(this);
    this->center_ = right.center_;
    this->radian_ = right.radian_;
    this->radius_ = right.radius_;
    this->width_ = right.width_;
    this->cs_ = right.cs_;
}


class UiOsdCircle::Impl {
public:
    std::shared_ptr<OsdCircle> data_;        // 数据对象
    std::shared_ptr<QRectF>    textRect_;    // 文本显示的位置
    EventHolder                eventHolder_; // 数据绑定句柄
};

UiOsdCircle::UiOsdCircle(const std::shared_ptr<Osd> &osd)
    : UiOsd(osd)
    , impl_(new Impl) {

    auto updateWidget = [=](BindingData::EventType eventType, const boost::any &eventValue) {
        impl_->data_->triggerEvent(eventType, eventValue);
        this->update();
    };

    impl_->data_ = std::dynamic_pointer_cast<OsdCircle>(data_);
    impl_->eventHolder_ += bindDataToObject(static_cast<BindingData *>(this), static_cast<BindingData *>(&impl_->data_->center_), [=]() {}, updateWidget);
    impl_->eventHolder_ += bindDataToObject(static_cast<BindingData *>(this), static_cast<BindingData *>(&impl_->data_->radian_), [=]() {}, updateWidget);
    impl_->eventHolder_ += bindDataToObject(static_cast<BindingData *>(this), static_cast<BindingData *>(&impl_->data_->radius_), [=]() {}, updateWidget);
    impl_->eventHolder_ += bindDataToObject(static_cast<BindingData *>(this), static_cast<BindingData *>(&impl_->data_->width_), [=]() {}, updateWidget);
    impl_->eventHolder_ += bindDataToObject(static_cast<BindingData *>(this), static_cast<BindingData *>(&impl_->data_->color_), [=]() {}, updateWidget);
    impl_->eventHolder_ += bindDataToObject(static_cast<BindingData *>(this), static_cast<BindingData *>(&impl_->data_->text_), [=]() {}, updateWidget);
    impl_->eventHolder_ += bindDataToObject(static_cast<BindingData *>(this), static_cast<BindingData *>(&impl_->data_->labels_), [=]() {}, updateWidget);
}

UiOsdCircle::~UiOsdCircle() {
    impl_->eventHolder_.clear();
}

bool UiOsdCircle::isOnObject(const QPointF &pos, Qt::CursorShape &mouseDirection) {
    auto isOnObject = [this, &pos]() {
        const auto &data = impl_->data_;
        //  落入大圆不落入小圆内
        double halfBorder = this->widgetToImage(std::max<double>(ROI_MASK_MARGIN + ROI_LINE_WIDTH, data->width_)) / 2;
        double r = data->radius_;
        if (data->cs_ == LayerTool::Cs::kUseGuiCs) {
            r = this->widgetToImage(r);
        }

        bool inBigEllipse = qPointInEllipse(pos, cv2qt(data->center_), r + halfBorder, r + halfBorder);
        bool inSmallEllipse = qPointInEllipse(pos, cv2qt(data->center_), std::max(r - halfBorder, 0.), std::max(r - halfBorder, 0.));
        bool matched = (inBigEllipse && !inSmallEllipse);
        return matched;
    };

    bool ret = isOnObject();
    if (ret)
        mouseDirection = Qt::PointingHandCursor;
    else
        mouseDirection = Qt::ArrowCursor;
    return ret;
}

void UiOsdCircle::paint(QPainter &qp, bool isDrawHotSpot) {
    QPointF center = imageToWidget(cv2qt(impl_->data_->center_));
    double radius;
    if (impl_->data_->cs_ == OsdCircle::kUseImageCs) {
        // 随着图片缩放
        radius = imageToWidget(impl_->data_->radius_);
    }
    else /* if (data->cs_ == OsdCircle::kUseGuiCs) */ {
        // 中心点随着图片缩放，半径相对窗口和起始点固定
        radius = impl_->data_->radius_;
    }

    if (!qIsFinite(center)
        || !qIsNormal(radius))
        return;
    qp.save();
    qp.rotate(impl_->data_->radian_ * 180 / M_PI);
    center = qRotatedCoordinate(QPointF(0, 0), center, -impl_->data_->radian_);

    QSizeF widgetSize = qp.clipBoundingRect().size();
    LOG_DEBUG("osd paint rc = {}\n", widgetSize);

    // 设置画笔
    const QColor &color = this->allocateColor(impl_->data_->color_);

    QPen pen;
    this->setPenStyle(&pen, color, impl_->data_->width_);
    pen.setCapStyle(Qt::PenCapStyle::RoundCap);
    pen.setJoinStyle(Qt::PenJoinStyle::RoundJoin);
    qp.setBrush(Qt::NoBrush);
    qp.setPen(pen);

    // 画圆
    qp.drawEllipse(center, radius, radius);

    // 画标签文字  
    bool isOsdLabelVisible = this->layerManager()->isOsdLabelVisible_;
    const std::string &textEx = isOsdLabelVisible
        ? strJoin({ impl_->data_->labels_, impl_->data_->text_ }, ",", true)
        : (std::string)impl_->data_->text_;
    if (!textEx.empty()) {
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
            if (textRect.width() + 18 < radius * 2
                && textRect.height() + 4 < radius * 2) {
                minFontSize = testFontSize;
            }
            else {
                maxFontSize = testFontSize;
            }
        }
        LOG_DEBUG("textRect = {}", textRect);

        QPointF point = { center.x() + radius - ROI_LABEL_MARGIN * 2 - textRect.width(), center.y() - textRect.height() / 2 };
        QSize textSize = { textRect.width() + 8, textRect.height() };
        textRect = QRect{ point.toPoint(), textSize };
        if (impl_->textRect_) *impl_->textRect_ = textRect;
        else impl_->textRect_ = std::make_shared<QRectF>(textRect);

        LOG_DEBUG("textRect = {}", textRect);
        //qp.drawRect(textRect);
        this->drawHollowFont(qp, font, text, textRect, color);
        //qp.setClipRect(textRect, Qt::ClipOperation::IntersectClip);
    }

    qp.restore();
}

#if 0
// 创建直线osd函数
// center: 中心点
// radius: 半径
// color: 颜色值，和 QColor::value() 的返回值相同
std::shared_ptr<UiOsd> UiOsd::newCircle(std::weak_ptr<LayerManager> layerManager, 
    const cv::Point2f &center,
    double radius,
    int color) {
    std::shared_ptr<UiOsd> osd = std::make_shared<UiOsdCircle>(center, radius, color);
    osd->setLayerManager(layerManager);
    return osd;
}
#endif


