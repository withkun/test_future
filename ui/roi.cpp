#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES
#endif
#include "ui/roi.h"
#include "ui/roi/roi_freedraw.h"
#include "ui/roi/roi_rectangle.h"
#include "ui/roi/roi_ellipse.h"
#include "ui/roi/roi_circle.h"
#include "ui/roi/roi_line.h"
#include "ui/roi/roi_fullimage.h"
#include "ui/roi/roi_label_area.h"
#include "ui/layer_manager.h"
#include "ui/image_controller.h"
#include "ui/var_tree_browser.h"
#include "utils/var_type.h"
#include "utils/log.h"
#include "utils/any_pro.h"
#include "utils/qt_utils.h"
#include "types/vlist.h"
#include "types/task.h"
#include "app/task_runtime.h"
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


UiRoi::UiRoi(std::shared_ptr<Roi> data)
    : UiLayerTool(data)
    , roiType_((!data) ? "" : data->roiType_) {
}


UiRoi::UiRoi(const std::string &roiType, std::shared_ptr<Roi> data) 
    : UiLayerTool(data)
    , roiType_((!roiType.empty() || !data) ? roiType : data->roiType_) {
}

UiRoi::~UiRoi() {
}

#define kStraightRectangle kLine + 1
const std::vector<RoiFactory> &UiRoi::getRoiFactory() {
    static const std::vector<RoiFactory> s_roiFactorys = {
        { "full_image", QObject::tr("FullImage"), ":/icons/select_full.png",
            []() { return std::make_shared<RoiFullImage>(); },
            [](const std::shared_ptr<Roi> &roi) { return std::dynamic_pointer_cast<UiRoi>(std::make_shared<UiRoiFullImage>(roi)); }
        },
        { "rectangle", QObject::tr("Rectangle"), ":/icons/roi_rotate_rect.png",
            []() { return std::make_shared<RoiRectangle>("rectangle"); },
            [](const std::shared_ptr<Roi> &roi) { return std::dynamic_pointer_cast<UiRoi>(std::make_shared<UiRoiRectangle>("rectangle", roi)); }
        },
        { "straight_rectangle", QObject::tr("StraightRectangle"), ":/icons/roi_rectangle.png",
            []() { return std::make_shared<RoiRectangle>("straight_rectangle"); },
            [](const std::shared_ptr<Roi> &roi) { return std::dynamic_pointer_cast<UiRoi>(std::make_shared<UiRoiRectangle>("straight_rectangle", roi)); }
        },
        { "circle", QObject::tr("Circle"), ":/icons/roi_circle.png",
            []() { return std::make_shared<RoiCircle>(); },
            [](const std::shared_ptr<Roi> &roi) { return std::dynamic_pointer_cast<UiRoi>(std::make_shared<UiRoiCircle>(roi)); }
        },
        { "ellipse", QObject::tr("Ellipse"), ":/icons/roi_ellipse.png",
            []() { return std::make_shared<RoiEllipse>(); },
            [](const std::shared_ptr<Roi> &roi) { return std::dynamic_pointer_cast<UiRoi>(std::make_shared<UiRoiEllipse>(roi)); }
        },
        { // freedraw 封闭连接线（同polygon，兼容性保留）
            "freedraw", QObject::tr("FreeDraw"), ":/icons/roi_polygon.png",
            []() { return std::make_shared<RoiFreeDraw>("polygon"); },
            [](const std::shared_ptr<Roi> &roi) { return std::dynamic_pointer_cast<UiRoi>(std::make_shared<UiRoiFreeDraw>("polygon", roi)); }
        },
        { "polygon", QObject::tr("Polygon"), ":/icons/roi_polygon.png",
            []() { return std::make_shared<RoiFreeDraw>("polygon"); },
            [](const std::shared_ptr<Roi> &roi) { return std::dynamic_pointer_cast<UiRoi>(std::make_shared<UiRoiFreeDraw>("polygon", roi)); }
        },
        { "polyline", QObject::tr("Polyline"), ":/icons/roi_polyline.png",
            []() { return std::make_shared<RoiFreeDraw>("polyline"); },
            [](const std::shared_ptr<Roi> &roi) { return std::dynamic_pointer_cast<UiRoi>(std::make_shared<UiRoiFreeDraw>("polyline", roi)); }
        },
        { "line", QObject::tr("Line"), ":/icons/roi_line.png",
            []() { return std::make_shared<RoiLine>(); },
            [](const std::shared_ptr<Roi> &roi) { return std::dynamic_pointer_cast<UiRoiLine>(std::make_shared<UiRoiLine>(roi)); }
        },
        { "label_area", QObject::tr("LabelArea"), ":/icons/roi_labelarea.png",
            []() { return std::make_shared<RoiLabelArea>(); },
            [](const std::shared_ptr<Roi> &roi) { return std::dynamic_pointer_cast<UiRoi>(std::make_shared<UiRoiLabelArea>(roi)); }
        }
    };

    return s_roiFactorys;
}

const RoiFactory *UiRoi::getRoiFactory(Roi::RoiType roiType) {
    return getRoiFactory(Roi::roiTypeName(roiType));
}

const RoiFactory *UiRoi::getRoiFactory(const std::string &roiType) {
    const std::vector<RoiFactory> &roiFactories = UiRoi::getRoiFactory();
    for (const RoiFactory &roiFactory : roiFactories) {
        if (roiFactory.roiType_ == roiType)
            return &roiFactory;
    }
    return nullptr;
}


// 获取圈定范围区域的构建器
const std::vector<RoiFactory> &UiRoi::getRoiRangeFactory() {
    static const std::vector<RoiFactory> s_roiFactorys = {
        *getRoiFactory("rectangle"),
        *getRoiFactory("straight_rectangle"),
        *getRoiFactory("circle"),
        *getRoiFactory("ellipse"),
        *getRoiFactory("polygon"),
        *getRoiFactory("polyline"),
        *getRoiFactory("line"),
    };
    return s_roiFactorys;
}

// 获取标签区域的构建器
const std::vector<RoiFactory> &UiRoi::getRoiLabelFactory() {
    static const std::vector<RoiFactory> s_roiFactorys = {
        *getRoiFactory("label_area"),
    };
    return s_roiFactorys;
}

std::shared_ptr<UiRoi> UiRoi::newInstanceBy(const std::shared_ptr<LayerManager> &layerManager,
                                        const RoiFactory &roiFactory,
                                        const std::shared_ptr<LayerTool> &roi,
                                        const std::string &layerName,
                                        bool isRefRoi,
                                        bool isReadonly) {
    std::shared_ptr<UiRoi> uiRoi = roiFactory.createRoi_(std::dynamic_pointer_cast<Roi>(roi));
    uiRoi->layerName_ = layerName;
    uiRoi->data_->isRef_ = isRefRoi;
    uiRoi->isReadonly_ = isReadonly;
    uiRoi->setLayerManager(layerManager);

    // 强制层设为显示（即使该层已经隐藏）
    //layerManager->hiddenLayers_.remove(layerName); // 移到用户界面添加时才显示

    return uiRoi;
}

std::shared_ptr<UiRoi> UiRoi::newInstance(const std::shared_ptr<LayerManager> &layerManager,
                                          const std::string &roiType,
                                          const std::shared_ptr<LayerTool> &roi,
                                          const std::string &layerName,
                                          bool isRefRoi,
                                          bool isReadonly) {
    const RoiFactory *roiFactory = UiRoi::getRoiFactory(roiType);
    if (roiFactory != nullptr)
        return newInstanceBy(layerManager, *roiFactory, roi, layerName, isRefRoi, isReadonly);
    return nullptr;
}

Promise UiRoi::runContextMenu(const QPointF &mousePos, const std::function<void(QMenu *, const FnAddMenu &)> &onMenuCreated) {
    (void)mousePos;

    std::weak_ptr<Roi> wData = std::dynamic_pointer_cast<Roi>(this->data_);
    return promise::delay(0).then(PM_LOC, [=] {

        //std::shared_ptr<LayerManager> layerManager = layerManager_.lock();
        QMenu *menu = new QMenu(this->widget());
        menu->setLayoutDirection(Qt::LeftToRight);
        std::shared_ptr<std::vector<std::function<promise::Promise()>>> getPromises = std::make_shared<std::vector<std::function<promise::Promise()>>>();

        auto ret = newPromise(PM_LOC, [=](Defer defer) {

            FnAddMenu addMenu = [=](QAction *action, const std::function<promise::Promise()> &getPromise) {
                // 编辑菜单处理
                int index = (int)getPromises->size();
                getPromises->push_back(getPromise);
                QObject::connect(action, &QAction::triggered, [=]() {
                    defer.resolve(PM_LOC, index);
                });
            };

            // 关联变量菜单(不做下一步状态转换)
            if (false) {
                QMenu *menuRelativeTo = menu->addMenu(QIcon(QPixmap(":/icons/variable2.png")), QObject::tr("Relative to ..."));
                menuRelativeTo->setObjectName("menuRelativeTo");
                newRelativeVarBrowser(menuRelativeTo);
            }

            // 删除菜单处理
            QAction *actionRemove = menu->addAction(QIcon(QPixmap(":/icons/remove.png")), QObject::tr("Remove the ROI"));
            actionRemove->setObjectName("actionRemove");
            addMenu(actionRemove, [=] {
                return newPromise(PM_LOC, [=](Defer defer) {
                    auto data = wData.lock();
                    if (data) {
                        int res = QMessageBox::question(this->widget(), "", QObject::tr("Remove the ROI (%1)?")
                                                        .arg(QString::fromStdString(data->text_)));
                        if (res == QMessageBox::Yes) {
                            std::shared_ptr<LayerManager> layerManager = layerManager_.lock();
                            if (layerManager) {
                                auto onScopeExit = layerManager->layerTools().scopedTriggerSource(Widget::kFromConfigRois);
                                layerManager->remove(this);
                            }
                        }
                    }
                    defer.resolve(PM_LOC);
                });
            });

            // 编辑文本菜单处理
            QAction *actionEditRoiText = menu->addAction(QIcon(QPixmap(":/icons/label.png")), QObject::tr("Edit ROI text"));
            actionEditRoiText->setObjectName("actionEditRoiText");
            addMenu(actionEditRoiText, [=] {
                // 进入文字编辑模式
                std::shared_ptr<LayerManager> layerManager = layerManager_.lock();
                if (layerManager) {
                    layerManager->setFocusedLayerTool(this);
                    return runModifyText(mousePos).finally(PM_LOC, [=]() {layerManager->setFocusedLayerTool(nullptr); });
                }
                else {
                    return promise::resolve(PM_LOC);
                }
            });

            // 修改颜色
            QAction *actionSetColor = menu->addAction(QIcon(QPixmap(":/icons/color.png")), QObject::tr("Set color"));
            actionSetColor->setObjectName("actionSetColor");
            addMenu(actionSetColor, [=] {
                // 进入文字编辑模式
                return runSetColor(mousePos);
            });

            // 修改颜色
            QAction *actionSetAutoColor = menu->addAction(QIcon(QPixmap(":/icons/color.png")), QObject::tr("Auto allocate color"));
            actionSetAutoColor->setObjectName("actionSetAutoColor");
            addMenu(actionSetAutoColor, [=] {
                // 设置自动分配颜色  
                this->setAutoColor();
                return promise::resolve(PM_LOC);
            });

            // 显示隐藏区域信息窗口  
            QAction *actionToggleLayerInfo = menu->addAction(QIcon(QPixmap(":/icons/layer_info.png")), QObject::tr("show / hide layer information window"));
            actionToggleLayerInfo->setObjectName("actionToggleLayerInfo");
            addMenu(actionToggleLayerInfo, [=] {
                std::shared_ptr<LayerManager> layerManager = layerManager_.lock();
                if (layerManager) {
                    ImageController *imageController = layerManager->imageController();
                    if (imageController) {
                        QToolButton *layerInfoButton = imageController->findChild<QToolButton *>("layerInfoButton");
                        if (layerInfoButton) {
                            layerInfoButton->toggle();
                            emit layerManager->moveLayer(this);
                        }
                    }
                }
                return promise::resolve(PM_LOC);
            });

            // 允许继承者定制菜单
            if (onMenuCreated)
                onMenuCreated(menu, addMenu);

            // 其他
            auto waitMenuHide = waitEvent(menu, QEvent::Hide).then(PM_LOC, [=]() {
                return promise::yield().then(PM_LOC, [=]() {
                    defer.resolve(PM_LOC, (int)-1);
                });
            });
            raceAndReject(PM_LOC, defer.getPromise(), waitMenuHide);

        }).then(PM_LOC, [=](int index) {
            if (index >= 0 && index < getPromises->size()) {
                return (*getPromises)[index]();
            }
            else {
                PTD;
                return promise::resolve(PM_LOC);
            }
        }).always(PM_LOC, [=]() {
            return runWorkaroundMaskCursor(Qt::ArrowCursor);
        });

        menu->exec(QCursor::pos());
        menu->deleteLater();

        return ret;

    });
}


// 文本编辑
Promise UiRoi::runModifyText(const QPointF &mousePos) {
    //QMessageBox::information(this->widget(), "edit", "edit");

    // 新建编辑框
    QLineEdit *edit = new QLineEdit(this->widget());
    std::shared_ptr<Roi> roi = std::dynamic_pointer_cast<Roi>(this->data_);
    
    edit->setText(QString::fromStdString(roi->text_));
    //QPointF center = imageToWidget(cv2qt(impl_->data_->center_));
    //double width = imageToWidget(impl_->data_->size_->width);
    //double height = imageToWidget(impl_->data_->size_->height);

    //QPointF point = { center.x() - width / 2 + 1, center.y() - height / 2 + 1 };
    //LOG_INFO("point = {}", point);

    edit->resize(150, 28);
    const QSize &size = edit->size();
    QPointF pos = { mousePos.x() - size.width() / 2,
                   mousePos.y() - size.height() / 2 };

    edit->move(pos.toPoint());
    edit->setFocus();
    edit->show();

    // 保存编辑结果
    auto saveText = [=]() {
        auto onScopeExit = roi->text_.scopedTriggerSource(Widget::kFromConfigRois);
        roi->text_ = edit->text().toStdString();
    };

    return listenEvents(PM_LOC,

        edit, 
        // 按键处理函数
        QEvent::KeyPress,
        [=](QEvent *event, const Defer &defer) {

            QKeyEvent *keyEvent = dynamic_cast<QKeyEvent *>(event);
            if (keyEvent == nullptr);
            else if (keyEvent->key() == Qt::Key_Escape) {
                defer.resolve(PM_LOC);
            }
            else if (keyEvent->key() == Qt::Key_Enter
                || keyEvent->key() == Qt::Key_Return) {
                saveText();
                defer.resolve(PM_LOC);
            }
        },

        // 失去焦点
        QEvent::FocusOut,
        [=](QEvent *event, const Defer &defer) {
            saveText();
            defer.resolve(PM_LOC);
        }
    ).then(PM_LOC, [=]() {
        edit->deleteLater();
    });
}

std::shared_ptr<Roi> UiRoi::getData() const {
    return std::dynamic_pointer_cast<Roi>(data_);
}


// 绑定到姿态变量
void UiRoi::newRelativeVarBrowser(QMenu *menuRelativeTo) {
    const std::shared_ptr<Widget> &widget = getRelativeWidget(this->widget());
    if (!widget) return;

    std::vector<std::string> excludeVariablePrefix;
    if (widget) excludeVariablePrefix.push_back(widget->varPrefix());

    VarTreeBrowser *varTreeBrowser = VarTreeBrowser::bindToVars(
        menuRelativeTo,
        *widget->roiAttitudes_,
        QObject::tr("Bind To Variables"),
        typeGetNames<Attitude>(),
        {},
        excludeVariablePrefix // 不显示本组件的变量
    );

#if 0  // 不修了，此函数已经不再使用  
    VarTreeBrowser::connect(varTreeBrowser, &VarTreeBrowser::clicked, [=] {
        // 菜单阻挡视线，把菜单藏起来，放到 VarTreeBrowser 下面
        QWidget *menu = menuRelativeTo->parentWidget();
        QPoint pos = varTreeBrowser->pos();
        pos = varTreeBrowser->mapToGlobal(pos);
        QPoint menuPos = { pos.x(), pos.y() };
        menu->move(menuPos);
    });
#endif
}
