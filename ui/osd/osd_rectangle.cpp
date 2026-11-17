#include "ui/osd/osd_rectangle.h"
#include "utils/qt_utils.h"
#include "utils/log.h"
#include "utils/event_holder.h"
#include "utils/any_pro.h"
#include <QPainter>

AP_REGISTRATION{
    anypro::registration::class_<OsdRectangle>()
        .base<Osd>()
        .property("radian", &OsdRectangle::radian_)
        .property("center", &OsdRectangle::center_)
        .property("size", &OsdRectangle::size_)
        .property("width", &OsdRectangle::width_)
        .property("isShowGrid", &OsdRectangle::isShowGrid_);

    // 注册OSD变量
    registerOsd<OsdRectangle, UiOsdRectangle>("rectangle", UiOsdRectangle::tr("Rectangle").toStdString());
}

static void osdInitTable(OsdRectangle *osd) {
    auto disabled = osd->cfgTable_->scopedDisableChangedEvent();
    osd->cfgTable_->push_back({ "radian",     &osd->radian_ });
    osd->cfgTable_->push_back({ "center",     &osd->center_ });
    osd->cfgTable_->push_back({ "size",       &osd->size_ });
    osd->cfgTable_->push_back({ "width",      &osd->width_ });
    osd->cfgTable_->push_back({ "isShowGrid", &osd->isShowGrid_ });
}

OsdRectangle::OsdRectangle()
    : Osd("rectangle")
    , radian_(0)
    , center_(cv::Point2f(0, 0))
    , size_(qt2cv(OSD_DEFAULT_VISIABLE_RECT).size())
    , width_(OSD_DEFAULT_LINE_WIDTH)
    , isShowGrid_(true) {
    osdInitTable(this);
}

OsdRectangle::OsdRectangle(const OsdRectangle &right)
    : Osd(right) {
    osdInitTable(this);
    this->radian_ = right.radian_;
    this->center_ = right.center_;
    this->size_ = right.size_;
    this->width_ = right.width_;
    this->isShowGrid_ = right.isShowGrid_;
}


class UiOsdRectangle::Impl {
public:
    std::shared_ptr<OsdRectangle> data_;        // 数据对象
    std::shared_ptr<QRectF>       textRect_;    // 文本显示的位置
    EventHolder                   eventHolder_; // 数据绑定句柄
};


UiOsdRectangle::UiOsdRectangle(const std::shared_ptr<Osd> &osd)
    : UiOsd(osd)
    , impl_(new Impl) {

    auto updateWidget = [=](BindingData::EventType eventType, const boost::any &eventValue) {
        impl_->data_->triggerEvent(eventType, eventValue);
        this->update();
    };

    impl_->data_ = std::dynamic_pointer_cast<OsdRectangle>(data_);
    impl_->eventHolder_ += bindDataToObject(static_cast<BindingData *>(this), static_cast<BindingData *>(&impl_->data_->radian_), [=]() {}, updateWidget);
    impl_->eventHolder_ += bindDataToObject(static_cast<BindingData *>(this), static_cast<BindingData *>(&impl_->data_->center_), [=]() {}, updateWidget);
    impl_->eventHolder_ += bindDataToObject(static_cast<BindingData *>(this), static_cast<BindingData *>(&impl_->data_->size_), [=]() {}, updateWidget);
    impl_->eventHolder_ += bindDataToObject(static_cast<BindingData *>(this), static_cast<BindingData *>(&impl_->data_->width_), [=]() {}, updateWidget);
    impl_->eventHolder_ += bindDataToObject(static_cast<BindingData *>(this), static_cast<BindingData *>(&impl_->data_->isShowGrid_), [=]() {}, updateWidget);
    impl_->eventHolder_ += bindDataToObject(static_cast<BindingData *>(this), static_cast<BindingData *>(&impl_->data_->color_), [=]() {}, updateWidget);
    impl_->eventHolder_ += bindDataToObject(static_cast<BindingData *>(this), static_cast<BindingData *>(&impl_->data_->text_), [=]() {}, updateWidget);
    impl_->eventHolder_ += bindDataToObject(static_cast<BindingData *>(this), static_cast<BindingData *>(&impl_->data_->labels_), [=]() {}, updateWidget);
}


UiOsdRectangle::~UiOsdRectangle() {
    impl_->eventHolder_.clear();
}

bool UiOsdRectangle::isOnObject(const QPointF &pos, Qt::CursorShape &mouseDirection) {
    auto isOnObject = [this, &pos]() {
        //  落入大框不落入小框内
        const auto &data = impl_->data_;
        const cv::Point2f &pos_ = cvRotatedCoordinate(data->center_, qt2cv(pos), -data->radian_);

        double halfBorder = this->widgetToImage(std::max<double>(ROI_MASK_MARGIN + ROI_LINE_WIDTH, data->width_)) / 2;
        cv::Rect2d bigRect = { data->center_->x - data->size_->width / 2 - halfBorder,
                               data->center_->y - data->size_->height / 2 - halfBorder,
                               data->size_->width + halfBorder * 2,
                               data->size_->height + halfBorder * 2 };
        cv::Rect2d smallRect = { data->center_->x - data->size_->width / 2 + halfBorder,
                                 data->center_->y - data->size_->height / 2 + halfBorder,
                                 std::max(data->size_->width - halfBorder * 2, 0.),
                                 std::max(data->size_->height - halfBorder * 2, 0.) };

        bool inBigRect = bigRect.contains(pos_);
        bool inSmallRect = smallRect.contains(pos_);
        bool matched = (inBigRect && !inSmallRect);
        return matched;
    };

    bool ret = isOnObject();
    if (ret)
        mouseDirection = Qt::PointingHandCursor;
    else
        mouseDirection = Qt::ArrowCursor;
    return ret;
}

void UiOsdRectangle::paint(QPainter &qp, bool isDrawHotSpot) {

    QSizeF widgetSize = qp.clipBoundingRect().size();
    LOG_DEBUG("UiOsdRectangle paint rc = {}\n", widgetSize);

    QPointF center = imageToWidget(cv2qt(impl_->data_->center_));
    double width = imageToWidget(impl_->data_->size_->width);
    double height = imageToWidget(impl_->data_->size_->height);
    if (!qIsFinite(center)
        || !qIsNormal(width)
        || !qIsNormal(height))
        return;

    qp.save();
    qp.setRenderHints(QPainter::Antialiasing);
    qp.rotate(impl_->data_->radian_ * 180 / M_PI);

    center = qRotatedCoordinate(QPointF(0, 0), center, -impl_->data_->radian_);

    const QColor &color = this->allocateColor(impl_->data_->color_);

    // 画矩形
    qreal lineWidth = impl_->data_->width_;

    QPen pen;
    this->setPenStyle(&pen, color, impl_->data_->width_);
    pen.setCapStyle(Qt::PenCapStyle::RoundCap);
    pen.setJoinStyle(Qt::PenJoinStyle::RoundJoin);
    qp.setPen(pen);

    QRectF rect = { center.x() - width / 2, center.y() - height / 2, width, height };
    qp.drawRect(rect);

    if (impl_->data_->isShowGrid_) {
        // 画中间十字
        QColor color1;
        qreal lightness = color.lightnessF();
        lightness = lightness + (1 - lightness) * 0.4;
        color1.setHslF(color.hslHueF(), color.hslSaturationF(), lightness, color.alphaF());

        QPen pen1;
        this->setPenStyle(&pen1, color1, impl_->data_->width_);
        pen1.setStyle(Qt::PenStyle::DotLine);
        pen1.setCapStyle(Qt::PenCapStyle::RoundCap);
        pen1.setJoinStyle(Qt::PenJoinStyle::RoundJoin);
        qp.setPen(pen1);

        qp.drawLine(rect.x(), center.y(),
            rect.x() + rect.width(), center.y());

        QPen pen3;
        this->setPenStyle(&pen1, color1, impl_->data_->width_ / 2.0f); //细线
        pen3.setStyle(Qt::PenStyle::DotLine);
        pen3.setCapStyle(Qt::PenCapStyle::RoundCap);
        pen3.setJoinStyle(Qt::PenJoinStyle::RoundJoin);
        qp.setPen(pen3);

        qp.drawLine(center.x(), rect.y(),
            center.x(), rect.y() + rect.height());

        // 画箭头方向
        this->drawArrow(qp,
            QPointF{ rect.x() + rect.width() - UiOsd::DEFAULT_ARROW_SIZE, center.y() },
            QPointF{ rect.x() + rect.width(), center.y() },
            color1);
    }


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
        if(impl_->textRect_) *impl_->textRect_ = textRect;
        else impl_->textRect_ = std::make_shared<QRectF>(textRect);
        LOG_DEBUG("textRect = {}", textRect);

        drawHollowFont(qp, font, text, textRect, color);
    }

    qp.restore();
}
