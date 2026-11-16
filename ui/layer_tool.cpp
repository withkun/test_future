#include "ui/layer_tool.h"
#include "ui/layer_manager.h"
#include "utils/log.h"
#include "utils/qt_utils.h"
#include "ui/image_controller.h"
#include <QMenu>
#include <QColorDialog>
using namespace promise;

Q_DECLARE_METATYPE(QExTimerHandler)

LayerTool::LayerTool() 
    : BindingData(LOC)
    , zIndex_(0)
    , isFirstInArray_(true)
    , isInitialized_(false)
    , isRef_(false) 
    , color_(QColor()) {
}

LayerTool::LayerTools LayerTool::listByVarName(const VStrSet &varNames) {
    LayerTools layerTools;
    for (const std::string &varName : varNames) {
        // 获取变量对应的显示数据
        LOG_DEBUG("varName = {}", varName);

        varSharedLock(varName, [&](const anypro::any &varValue) {

            // 检测数组变量
            const std::string &varArrayName = varIsArray(varName);
            if (!varArrayName.empty()) {
                // 数组变量
                LOG_DEBUG("found array = {}", varArrayName);
                //const anypro::any &varSelectedArray = varGet(varArrayName);
                const anypro::any &varSelectedArray = varValue;
                VList<std::shared_ptr<Roi>> *rois = anypro::rawCastPointer<VList<std::shared_ptr<Roi>>>(varSelectedArray);
                if (rois != nullptr) {
                    auto lock = hi_shared_lock(*rois);
                    for (const auto &roi : *rois) {
                        if (roi) layerTools.push_back(roi);
                    }
                }

                VList<std::shared_ptr<Osd>> *osds = anypro::rawCastPointer<VList<std::shared_ptr<Osd>>>(varSelectedArray);
                if (osds != nullptr) {
                    auto lock = hi_shared_lock(*osds);
                    for (const auto &osd : *osds) {
                        if (osd) layerTools.push_back(osd);
                    }
                }

                LOG_DEBUG("rois = {}, osds = {}", rois, osds);
            }
            else {
                // 非数组变量
                //const anypro::any &varSelected = varGet(varName);
                const anypro::any &varSelected = varValue;
                LayerTool *layerTool = nullptr;
                try {
                    const anypro::any &aLayerTool = anypro::parameter_cast<LayerTool *>(varSelected);
                    layerTool = anypro::rawCastPointer<LayerTool>(aLayerTool);
                }
                catch (const anypro::bad_any_cast &ex) {
                    (void)ex;
                }

                if (layerTool != nullptr) {
                    std::shared_ptr<LayerTool> sLayerTool = layerTool->shared_from_this();
                    layerTools.push_back(sLayerTool);
                }
            }
        });
    }

    // 去重
    //LOG_DEBUG("layerTools.size() = {}", layerTools.size());
    layerTools.sort();
    layerTools.unique();
    //LOG_DEBUG("layerTools.size() = {}", layerTools.size());

    return layerTools;
}

// 获取颜色属性
DataProperty<QColor> &LayerTool::getColorProperty(int index) {
    (void)index;
    return color_;
}

// 获取本对象的颜色组的名字，默认为空，则使用唯一的颜色组
std::vector<std::pair<std::string, int>> UiLayerTool::getColorNames() const {
    return {};
}


UiLayerTool::UiLayerTool(std::shared_ptr<LayerTool> data)
    : BindingData(LOC)
    , layerManager_()
    , data_(data)
    , isFocused_(false)
    , isReadonly_(false) {
}

UiLayerTool::~UiLayerTool() {
    LOG_DEBUG("delete UiLayerTool = {}", this);
    close();

    std::shared_ptr<LayerManager> layerManager = layerManager_.lock();
    if (layerManager) {
        QEvent destroyEvent(QEvent::Destroy);
        layerManager->eventFilter(this, &destroyEvent);
    }
}

std::shared_ptr<LayerManager> UiLayerTool::layerManager() const {
    return layerManager_.lock();
}

void UiLayerTool::setLayerManager(std::weak_ptr<LayerManager> layerManager) {
    this->remove();
    layerManager_ = layerManager;
    std::shared_ptr<LayerManager> newLayerManager = layerManager_.lock();

    if (newLayerManager != nullptr) {
        //size_t s0 = newLayerManager->layerTools().size();
        newLayerManager->layerTools().push_back({ this->layerName_, this->shared_from_this() });
        //size_t s1 = newLayerManager->layerTools().size();
    }
}

void UiLayerTool::close() {
    if (pendingTask_) {
        pendingTask_ = pendingTask_.fail(PM_LOC, [] {});
        pendingTask_.reject(PM_LOC);
        pendingTask_.clear();
    }
}

void UiLayerTool::remove() {
    std::shared_ptr<LayerManager> layerManager = layerManager_.lock();
    if (layerManager)
        layerManager->remove(this);
}

bool UiLayerTool::isValid() const {
    std::shared_ptr<LayerManager> layerManager = layerManager_.lock();
    return layerManager != nullptr && layerManager->isValid(this);
}

void UiLayerTool::update() {
    std::shared_ptr<LayerManager> layerManager = layerManager_.lock();
    if (layerManager)
        layerManager->update();
}


void UiLayerTool::drawRotateHandler(QPainter &qp,
                                    const QPointF &rotateCenter,
                                    const QColor &color,
                                    bool isSetPen,
                                    double radian) {
    if (isSetPen) {
        QPen pen;
        this->setPenStyle(&pen, color, ROI_LINE_WIDTH);
        pen.setCapStyle(Qt::PenCapStyle::RoundCap);
        pen.setJoinStyle(Qt::PenJoinStyle::MiterJoin);
        qp.setBrush(Qt::NoBrush);
        qp.setPen(pen);
    }

    LOG_DEBUG("radian = {}", radian);
    qp.save();
    qp.translate(rotateCenter);
    qp.rotate(radian * 180 / M_PI + 45);

    double rotateOffset = ROI_HOTSPOT_DISTANCE + ROI_LINE_WIDTH;
    QRectF rotateRect = { - rotateOffset,
                          - rotateOffset,
                          rotateOffset * 2,
                          rotateOffset * 2 };
    qp.drawArc(rotateRect, 0 * 16, 90 * 16);

    double arrowOffset = ROI_HOTSPOT_DISTANCE + ROI_LINE_WIDTH + ROI_LINE_WIDTH / 2;
    QPainterPath path0;
    path0.moveTo(+ 3.5, - arrowOffset - 1.5);
    path0.lineTo(0,     - arrowOffset);
    path0.lineTo(0,     - arrowOffset + 3.5);
    qp.drawPath(path0);

    QPainterPath path1;
    path1.moveTo(arrowOffset + 1.5, - 3.5);
    path1.lineTo(arrowOffset, 0);
    path1.lineTo(arrowOffset - 3.5, 0);
    qp.drawPath(path1);

    qp.restore();
}


void UiLayerTool::drawArrow(QPainter &qp,
                            const QPointF &start,
                            const QPointF &end,
                            const QColor &color,
                            float arrowSize,
                            float lineWidth,
                            bool isSetPan) {
    if (!qIsFinite(start)
        || !qIsFinite(end)
        || !std::isfinite(arrowSize)
        || !std::isnormal(lineWidth))
        return;

    if (isSetPan) {
        QPen pen;
        this->setPenStyle(&pen, color, lineWidth);
        pen.setCapStyle(Qt::PenCapStyle::RoundCap);
        pen.setJoinStyle(Qt::PenJoinStyle::MiterJoin);
        qp.setBrush(Qt::NoBrush);
        qp.setPen(pen);
    }

    QPainterPath linePath;

    float angle = ::atan2(end.y() - start.y(), end.x() - start.x());
    float angles[2] = { angle + (float)M_PI + (float)M_PI / 6, angle + (float)M_PI - (float)M_PI / 6 };
    QPointF arrows[2];
    for (size_t i = 0; i < sizeof(angles) / sizeof(angles[0]); ++i) {
        arrows[i] = QPointF(end.x() + arrowSize * ::cos(angles[i]),
            end.y() + arrowSize * ::sin(angles[i]));
    }
    linePath.moveTo(arrows[0]);
    linePath.lineTo(end);
    linePath.lineTo(arrows[1]);

    linePath.moveTo(end.x(), end.y());
    linePath.lineTo(start.x(), start.y());
    qp.drawPath(linePath);


}


// 画空心文字
void UiLayerTool::drawHollowFont(QPainter &qp,
    const QFont &font,
    const QString &text,
    const QRect &textRect,
    const QColor &textColor,
    const QColor &borderColor) {

#if 0
    QColor borderColor2 = borderColor;
    if (!borderColor.isValid()) {
        qreal lightness = textColor.lightnessF();
        lightness = 1 - (1 - lightness) * 0.5;
        borderColor2 = QColor::fromHslF(textColor.hslHueF(), textColor.hslSaturationF(), lightness, textColor.alphaF());
    }

    QFontMetrics metrics(font);
    QPainterPath path;
    QPen pen(borderColor2);
    qreal penwidth = font.pointSize() * 0.2;
    if (penwidth > 6) {
        penwidth = 6;
    }
    else if (penwidth < 1) {
        penwidth = 1;
    }

    penwidth = 3;
    pen.setWidthF(penwidth);
    path.addText(textRect.x(), textRect.y() + textRect.height(), font, text);
    qp.setPen(pen);
    qp.strokePath(path, pen);
    qp.drawPath(path);
    qp.fillPath(path, QBrush(borderColor));

    penwidth = 0.5;
    pen.setWidthF(penwidth);
    qp.setPen(pen);
    qp.fillPath(path, QBrush(textColor));

#else
    QPen penText(textColor);
    //penText.setStyle(Qt::PenStyle::SolidLine);
    qp.setPen(penText);
    qp.drawText(textRect, Qt::AlignLeft | Qt::AlignBottom, text);
#endif
}

// 统一样式：设定在主线宽比例下，根据不同条件的显示修改（isFirstInArray_，isRef_ ）
float UiLayerTool::getLineWidth(double width) {
    if (this->data_->isFirstInArray_)
        return width;
    else if (this->isFocused_)
        return width * 1.2;
    else
        return std::max(width * 0.7, std::min(width, 1.0));
}

void UiLayerTool::setPenStyle(QPen *pen, const QColor &color, double width) {
    QBrush brush(color);
    brush.setStyle(Qt::BrushStyle::SolidPattern);
    pen->setBrush(brush);
    pen->setColor(color);

    auto layerManager = this->layerManager_.lock();
    if (layerManager && !layerManager->isEditable()) {
        // 不可编辑的
        if (this->data_->isFirstInArray_) {
            pen->setStyle(Qt::SolidLine);
            pen->setWidthF(width);
        }
        else {
            // 其他（例如：同组其他）
            pen->setStyle(Qt::SolidLine);
            pen->setWidthF(std::max(width * 0.7, std::min(width, 1.0)));
        }
    }
    else {
        if (this->isFocused_) {
            // 获得焦点
            pen->setStyle(Qt::SolidLine);
            pen->setWidthF(width * 1.2);
        }
        else if (this->data_->isFirstInArray_) {
            // 同组第一个
            pen->setStyle(Qt::SolidLine);
            pen->setWidthF(width);
        }
        else if (this->data_->isRef_) {
            // 引用的
            pen->setStyle(Qt::CustomDashLine);
            pen->setDashPattern({ width / 2, width });
            pen->setWidthF(width);
        }
        else {
            // 其他（例如：同组其他）
            pen->setStyle(Qt::CustomDashLine);
            pen->setDashPattern({ width / 2, width });
            pen->setWidthF(std::max(width * 0.7, std::min(width, 1.0)));
        }
    }
}

// 分配颜色（color无效时，自动分配；color有效时，返回color）
QColor UiLayerTool::allocateColor(const QColor &color, int index) {
    if (color.isValid()) return color;

    auto layerManager = layerManager_.lock();
    if (layerManager)
        return layerManager->allocateColor(this, color, index);
    else
        return color;
}


Promise UiLayerTool::runSetColor(const QPointF &mousePos, int index) {
    // 新建颜色选择对话框
    QColorDialog *colorDialog = new QColorDialog(this->widget());

    QColor oldColor = this->data_->getColorProperty(index);

    const QColor &allocatedColor = this->allocateColor(oldColor, index);
    colorDialog->setCurrentColor(allocatedColor);

    std::weak_ptr<UiLayerTool> wThiz = this->shared_from_this();
    auto applyColor = [wThiz, index](const QColor &color) {
        // 颜色已修改
        std::shared_ptr<UiLayerTool> thiz = wThiz.lock();
        if (!thiz) return;

        //设定到层分配器
        auto layerManager = thiz->layerManager_.lock();
        if (!layerManager) return;
        layerManager->colorManager().reserveColor(color, thiz->layerName_, thiz->data_->toolClassName(), index);

        if (index == 0) {
            if (std::shared_ptr<Roi> roi = std::dynamic_pointer_cast<Roi>(thiz->data_); roi != nullptr) {
                try {
                    // 同时修改原本设定的roi
                    std::shared_ptr<Roi> roi0 = roi->roi0_.lock();
                    if (roi0 != nullptr) {
                        auto onScopeExit = roi0->color_.scopedTriggerSource(Widget::kFromConfigRois);
                        roi0->color_ = color;
                    }
                }
                catch (const anypro::bad_any_cast &) {}
                catch (const std::out_of_range &) {}
            }
        }

        //设定到层分配器
        DataProperty<QColor> &colorProperty = thiz->data_->getColorProperty(index);
        colorProperty = color;
    };

    QColorDialog::connect(colorDialog, &QColorDialog::currentColorChanged, [applyColor, colorDialog] {
        applyColor(colorDialog->currentColor());
    });

    QPointF pos = mousePos;
    int res = colorDialog->exec();
    if (res == QColorDialog::Accepted) {
        applyColor(colorDialog->currentColor());
    }
    else {
        applyColor(allocatedColor);
    }
    colorDialog->deleteLater();
    return promise::resolve(PM_LOC);
}

Promise UiLayerTool::runDispText(const QStringList& texts) {
    QMessageBox *messageBox = new QMessageBox(this->widget());
    messageBox->setText(texts.join("\n"));
    messageBox->setStandardButtons(QMessageBox::Ok);
    messageBox->exec();
    messageBox->deleteLater();
    return promise::resolve(PM_LOC);
}

void UiLayerTool::setAutoColor(int index) {
    //设定到层分配器
    auto layerManager = this->layerManager_.lock();
    if (!layerManager) return;
    layerManager->colorManager().reserveColor(QColor(), this->layerName_, this->data_->toolClassName(), index);

    // 颜色已修改
    DataProperty<QColor> &colorProperty = this->data_->getColorProperty(index);
    colorProperty = QColor();
}

Promise UiLayerTool::runWorkaroundMaskCursor(const QCursor &cursor) {
    // 这是一个workaround，不明原因导致图像区域的鼠标没法设定
    // 经验证需要鼠标移动到其他QWidget，再移回来，才能重新设定鼠标形状
    // 因此，创建一个覆盖层透明窗口，接收移动事件
    QWidget *maskWidget = new QWidget(this->widget());
    maskWidget->setCursor(cursor);
    maskWidget->resize(this->widget()->size());
    maskWidget->move(0, 0);
    maskWidget->show();

    std::shared_ptr<bool> isSuccess = std::make_shared<bool>(false);
    std::shared_ptr<bool> isDeleted = std::make_shared<bool>(false);
    auto onFinish = [=]() {
        *isSuccess = true;
        if (!*isDeleted) {
            *isDeleted = true;
            maskWidget->deleteLater();
        }
    };
    auto onFail = [=](const pm_any &arg) {
        if (!*isDeleted) {
            *isDeleted = true;
            maskWidget->deleteLater();
        }
        if (!*isSuccess)
            throw arg;
    };
    auto waitMouseMove = waitEvent(maskWidget, QEvent::MouseMove).then(PM_LOC, onFinish).fail(PM_LOC, onFail);
    auto waitHoverMove = waitEvent(maskWidget, QEvent::HoverMove).then(PM_LOC, onFinish).fail(PM_LOC, onFail);
    return raceAndReject(PM_LOC, { waitMouseMove, waitMouseMove });
}

QWidget *UiLayerTool::widget() const {
    std::shared_ptr<LayerManager> layerManager = layerManager_.lock();
    return (layerManager != nullptr ? layerManager->widget() : nullptr);
}

// 获取所属窗口当前的图片
std::shared_ptr<Image> UiLayerTool::getImage() const {
    std::shared_ptr<LayerManager> layerManager = layerManager_.lock();
    return (layerManager != nullptr ? layerManager->getImage() : nullptr);
}

// 图像分辨率
QSizeF UiLayerTool::imageSize() const {
    std::shared_ptr<LayerManager> layerManager = layerManager_.lock();
    if (layerManager != nullptr)
        return layerManager->imageSize();
    return QSizeF(0, 0);
}

// 图像放大倍数
double UiLayerTool::imageZoom() const {
    std::shared_ptr<LayerManager> layerManager = layerManager_.lock();
    if (layerManager != nullptr)
        return layerManager->imageZoom();
    return 1;
}

// 图像放大区域中心在整体图像的位置（比例 0 - 1）
QPointF UiLayerTool::imageZoomCenter() const {
    std::shared_ptr<LayerManager> layerManager = layerManager_.lock();
    if (layerManager != nullptr)
        return layerManager->imageZoomCenter();
    return QPointF(0.5, 0.5);
}

QSizeF UiLayerTool::widgetSize() const {
    std::shared_ptr<LayerManager> layerManager = layerManager_.lock();
    if (layerManager != nullptr)
        return layerManager->widgetSize();
    return QSizeF(0, 0);
}


// 窗口坐标转成图像坐标
QPointF UiLayerTool::widgetToImage(const QPointF &pos) const {
    std::shared_ptr<LayerManager> layerManager = layerManager_.lock();
    if (layerManager) return layerManager->widgetToImage(pos);
    else return pos;
}

// 图像坐标转成窗口坐标
QPointF UiLayerTool::imageToWidget(const QPointF &pos) const {
    std::shared_ptr<LayerManager> layerManager = layerManager_.lock();
    if (layerManager) return layerManager->imageToWidget(pos);
    else return pos;
}

// 窗口坐标转成图像坐标
QLineF UiLayerTool::widgetToImage(const QLineF &pos) const {
    QPointF p1 = widgetToImage(pos.p1());
    QPointF p2 = widgetToImage(pos.p2());
    return QLineF(p1, p2);
}

// 图像坐标转成窗口坐标
QLineF UiLayerTool::imageToWidget(const QLineF &pos) const {
    QPointF p1 = imageToWidget(pos.p1());
    QPointF p2 = imageToWidget(pos.p2());
    return QLineF(p1, p2);
}

// 窗口坐标转成图像坐标
QSizeF UiLayerTool::widgetToImage(const QSizeF &size) const {
    return QSizeF(widgetToImage(size.width()), widgetToImage(size.height()));
}

// 图像坐标转成窗口坐标
QSizeF UiLayerTool::imageToWidget(const QSizeF &size) const {
    return QSizeF(imageToWidget(size.width()), imageToWidget(size.height()));
}

// 窗口坐标转成图像坐标
QRectF UiLayerTool::widgetToImage(const QRectF &rect) const {
    return QRectF(widgetToImage(rect.topLeft()), widgetToImage(rect.size()));
}
// 图像坐标转成窗口坐标
QRectF UiLayerTool::imageToWidget(const QRectF &rect) const {
    return QRectF(imageToWidget(rect.topLeft()), imageToWidget(rect.size()));
}


// 窗口距离转成图像距离
double UiLayerTool::widgetToImage(double size) const {
    std::shared_ptr<LayerManager> layerManager = layerManager_.lock();
    if (layerManager) return layerManager->widgetToImage(size);
    else return size;
}

// 图像距离转成窗口距离
double UiLayerTool::imageToWidget(double size) const {
    std::shared_ptr<LayerManager> layerManager = layerManager_.lock();
    if (layerManager) return layerManager->imageToWidget(size);
    else return size;
}

// 获取对应的状态显示栏
QStatusBar *UiLayerTool::statusBar() {
    auto layerManager = layerManager_.lock();
    if (!layerManager) return nullptr;
    return layerManager->statusBar();
}

// 状态栏位显示文字（支持富文本）
void UiLayerTool::showStatusMessage(const QString &message, int timeoutMs) {
    QStatusBar *statusBar = this->statusBar();
    if (statusBar) {
        QString name = "__left__";
        QLabel *label = statusBar->findChild<QLabel *>(name);
        if (label == nullptr) {
            label = new QLabel(statusBar);
            label->setObjectName(name);
            label->setTextFormat(Qt::RichText);
            //label->setStyleSheet("margin-right:5px;");
            statusBar->insertWidget(0, label);
        }

        label->setText(message + "  ");

        // 清除旧的timer
        const QVariant &vOldTimer = label->property("closeTimer");
        if(vOldTimer.isValid()) {
            QExTimerHandler oldTimer = vOldTimer.value<QExTimerHandler>();
            qClearTimeout(oldTimer);
        }

        // 设定新的timer
        if (timeoutMs > 0) {
            auto timer = qSetTimeout(LOC, label, [=](const Loc &) {
                label->setText("");
            }, timeoutMs);
            label->setProperty("closeTimer", QVariant::fromValue(timer));
        }
    }
}
