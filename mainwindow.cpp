#include "mainwindow.h"
#include "ui_mainwindow.h"

#include "project/project_file.h"
#include "utils/log.h"
#include "app/host_console.h"
#include "types/flow.h"
#include "types/scene.h"
#include "types/task.h"
#include "types/project_db.h"
#include "jsengine/tl_module.h"
#include "utils/qt_utils.h"
#include "ui/roi.h"
#include "ui/var_browser.h"
#include "ui/var_tree_browser.h"
#include "ui/tl_application.h"
#include "utils/misc.h"
#include "jsengine/utils.h"
#include "utils/any_pro.h"
#include "promise-cpp/add_ons/qt/promise_qt.hpp"
#include "ui/alarm_browser.h"
#include "ui/widget_label.h"
#include "ui/flow_editor.h"
#include "ui/image_frame.h"
#include "ui/about_dialog.h"
#include "ui/image_bundle_viewer.h"
#include "ui/time_inspector_list.h"
#include "ui/time_reset_plan.h"
#include "ui/timedout_message_box.h"
#include "ui/project_parameters.h"
#include "ui/project_update_settings_dialog.h"
#include "ui/project_merge_open.h"
#include "ui/login_dialog.h"
#include "ui/user_group_dialog.h"
#include "ui/user_auth_item_dialog.h"
#include "ui/user_login_dialog.h"
#include "ui/ui_hmi_list.h"
#include "ui/ui_designer.h"
#include "ui/user_edit_dialog.h"
#include "ui/image_controller.h"
#include "ui/project_history_dialog.h"
#include "ui/dock_monitor.h"
#include "ui/alarm_list.h"
#include "ui/project_archive_dialog.h"
#include "ui/tls_mode_dialog.h"
#include "cmd_arg.h"
#include "alarm_manager.h"

#include <QApplication>
#include <QPushButton>
#include <QTextBrowser>
#include <QTextCursor>
#include <QQuickWidget>
#include <QListWidget>
#include <QDir>
#include <QVariant>
#include <QQmlEngine>
#include <QScrollBar>
#include <QFileDialog>
#include <QMessageBox>
#include <QInputDialog>
#include <QMdiSubWindow>
#include <QSizeGrip>
#include <QQuickItem>
#include <QSpacerItem>
#include <QTabBar>
#include <QToolTip>
#include <QDesktopServices>
#include <QToolButton>
#include <QProgressDialog>
#include <QRadioButton>
#include <QWidgetAction>
#include "ui/connect_line.h"
#include "ui/property_testdata.h"
#include "configdialog/configdialog.h"
#include "../splash_screen/proc_notifier.h"
#include "utils/log.h"
#include "utils/var_type.h"
#include "utils/event_holder.h"
#include "DockManager.h"


#define DEFAULT_SPLITTER_1 1.5
#define DEFAULT_SPLITTER_2 0.2

static void adjustQTabBarWidth(QTabBar *tabBar) {
    return;
    int index = tabBar->count();
    tabBar->insertTab(index, "----------------");
    tabBar->removeTab(index);
}

Q_DECLARE_METATYPE(QExTimerHandler)

struct QTreeWidgetPublic : public QTreeWidget {
    QModelIndex indexFromItem(const QTreeWidgetItem *item, int column = 0) const {
        return QTreeWidget::indexFromItem(item, column);
    }
    QTreeWidgetItem *itemFromIndex(const QModelIndex &index) const {
        return QTreeWidget::itemFromIndex(index);
    }
};


class MainWindow::Impl {
public:
    Impl(MainWindow *thiz)
        : thiz_(thiz) {
    }

    MainWindow                              *thiz_;

    EventHolder                              eventHolderOpLog_;  // 操作监听器

    bool                                     isLastRunOk_;       // 上次运行是否正常退出
    std::function<void()>                    mainClear_;         // 主程序清理函数，传到这里用于强制退出
    std::vector<FlowEditor *>                focusedFlowEditors_;// 当前配方下的所有流程的编辑器
    std::vector<FlowEditor *>                sharedFlowEditors_; // 共享配方下的所有流程的编辑器
    FlowEditor                              *globalFlowEditor_;
    FlowEditor                              *focusedFlowEditor_; // 当前选中的流程编辑器

    std::vector<ImageFrame *>                focusedImageFrames_;
    std::vector<ImageFrame *>                sharedImageFrames_;
    ImageFrame *                             globalImageFrame_;

    std::shared_ptr<ProjectFile>             projectFile_;  // 当前打开的项目

    bool                                     inUpdateReleaseMode_;
    bool                                     projectDockFlowsVisible_;
    bool                                     projectDockWidgetsVisible_;
    bool                                     projectDockFlowEditorVisible_;
#if 0
    QList<int>                               projectSplitSizes_;
#endif

    QQmlEngine                               jsEngine_;          // js引擎
    //std::shared_ptr<TlModule>                tlModule_;        // 模块维护类
    //std::vector<std::shared_ptr<Widget>>     widgets_;         // 模块实例
    QWidget                                 *configDialog_;      // 配置对话框
    QWidget                                 *varBrowser_;        // 变量浏览器
    QWidget                                 *alarmBrowser_;      // 告警信息浏览器  
    QWidget                                 *projectParameters_; // 项目参数
    QWidget                                 *projectUpdateSettingsDialog_; // 项目升级参数
    QWidget                                 *uiList_;            // 设计界面列表
    QWidget                                 *timeInspector_;     // 时间检视器
    QWidget                                 *alarmTriggers_;     // 告警触发器设置  
    QWidget                                 *alarmHistory_;      // 告警历史  
    std::atomic<bool>                        isClosing_;         // 处于关闭状态中
    std::atomic<bool>                        isQuiteClosing_;    // 处于无对话框提示的关闭状态中
    std::atomic<bool>                        isAutoClosing_;     // 处于自动关闭状态中
    QExTimerHandler                          eventHolderCloser_; // 用于关闭条件
    EventHolder                              eventHolderUser_;   // 绑定用户登录句柄  
    EventHolder                              eventHolder_;       // 数据绑定句柄

    QTabBar                                 *flowEditorBar_;        // 流程管理器
    QTabBar                                 *globalEditorBar_;      // 全局流程编辑器
    std::atomic<bool>                        disableNewFlowEditor_; // 禁止产生新的流程管理器

    QWidget                                 *ngToolTip_;            // NG消息展示器
    QExTimerHandler                          ngToolTipTimer_;       // NG消息展示器消失句柄
    std::chrono::steady_clock::time_point    ngToolTipShowTime_;    // NG消息展示器开始展示的事件
    int                                      ngToolTipShowCount_;   // NG消息展示器展示的次数

    std::map<std::weak_ptr<Flow>, int>               flowTabMaps_;  // 流程和tab关系（用于快速查找, not used）
    std::map<std::weak_ptr<Flow>, QTreeWidgetItem *> flowTreeMaps_; // 流程和树关系（用于快速查找）
    std::map<std::weak_ptr<Task>, QTreeWidgetItem *> taskTreeMaps_; // 任务和树关系（用于快速查找）

    std::optional<QTime>                     lastTimePlannerTime_; // 上一次时间计划器时间
    AlarmManager                             alarmManager_;        // 报警管理器
    QWidget*                                 projectArchive_;      // 项目档案对话框                                    

    // 状态栏  
    std::map<QString, QWidget *> statusbars_;

    // 高级dock管理
    std::string dockStateTemp_;
    ads::CDockManager *dockManager_{ nullptr };
    DockWidgetMonitor *dockWidgetMonitor_{ nullptr };
    ads::CDockWidget *dockWidget_flows_;
    ads::CDockWidget *dockWidget_widgets_;
    ads::CDockWidget *dockWidget_widgetAttribute_;
    ads::CDockWidget *dockWidget_console_;
    ads::CDockWidget *dockWidget_alarm_;
    ads::CDockWidget *dockWidget_mainUi_;
    ads::CDockWidget *dockWidget_flowEditor_;

    // 显示版本提示
    void checkModuleVersions();

    // 加载最近一个项目
    void loadLatestProject(const QString &path,
        const std::string &startupSceneName,
        const std::vector<std::string> &startupFlowNames,
        const std::optional<int>& recipeId,
        FlowRunner::RunCmd runCmd);

    // 监听项目结构变化
    void bindProjectFile(bool refreshGuiAfterBind, const Project::ShowProgress &showProgress);

    // 初始化操作监听器
    void setupUserOpLog();

    // 初始化状态栏
    void setupStatusbar();
    // 初始化toolbar
    void setupMainToolbar();
    // 初始化流程编辑器
    void setupFlowEditors();
    // 初始化图像托盘容器
    void setupImageFrames();
    // 初始化HMI项目菜单
    void setupHmiMenu();

    // 初始化授权绑定
    void setupAuthItems();
    // 运行启动项目  
    void setupStartupProject(__qsync__);

    // 设定清理时钟  
    void setupTimePlan();

    // 获取启动项目所需的dll  
    std::set<std::string> getStartupProjectDlls();
    // 获取启动项目信息  
    void getStartupInfo(
        FlowRunner::RunCmd &runCmd,
        int &loopCount,
        QString &startupProject,
        std::string &startupSceneName,
        std::vector<std::string> &startupFlowNames,
        std::optional<int>& recipeId);

    void loadPluginDlls(const std::set<std::string> &dlls);

    // 更新登录的用户信息  
    void updateLoginUser();
    // 更新最近项目菜单
    void updateRecentProjectMenu();
    // 更新项目档案Dialog
    void updateProjectArchive();
    // 更新发布模式状态
    void updateReleaseMode();
    // 更新主工具条
    void updateMainToolbar();
    // 更新项目流程树
    void updateFlowTree();
    // 更新流程编辑器
    void updateFlowEditors();
    // 更新图片托盘
    void updateImageFrames();
    // 更新组件树
    void updateWidgetTree();
    // 更新流程状态
    void updateFlowStatus(const std::weak_ptr<Flow> &flow);
    // 更新任务状态
    void updateTaskStatus(const std::weak_ptr<Task> &task);

    FlowEditor *newFlowEditor(const std::shared_ptr<Flow> &flow, QTabBar *flowEditorBar, bool addCloseButton, int index, const QString &editorName, const QString &tabName);
    void deleteFlowEditor(QTabBar *flowEditorBar, int index);
    void renameFlowEditor(QTabBar *flowEditorBar, int index);
    void forceDeleteFlowEditor(QTabBar *flowEditorBar, int index);
    FlowEditor *focusedFlowEditor();
    void setFocusFlowEditor(FlowEditor *flowEditor);
    FlowEditor *getFlowEditor(const std::shared_ptr<Flow> &flow);

    // 项目树被点击
    void clickFlowFree(const QPoint &point);
    // 组件区开始拖动
    void startDragWidget(QTreeWidgetItem *item, int column);

    // 设定docker窗口和菜单关联
    void connectDockerToMenu();

    // 设置属性页名称
    void setProperyPageName(const QString &name);

    // 运行项目
    void runCurrentProject(
        FlowRunner::RunCmd runCmd,
        const char *startupSceneName,
        const std::vector<std::string> &startupFlowNames);

    // 判断是否在运行
    bool warnProjectRunning();

    // 启动显示 NgToolTip
    void startNgToolTipTimer(int timeoutMs, int totalShowMs);

    // 关闭变量浏览器窗口
    void closeVarBrowser();
    // 关闭告警浏览器窗口  
    void closeAlarmBrowser();
    // 关闭参数窗口
    void closeProjectParameters();
    // 关闭项目升级参数窗口  
    void closeProjectUpdateSettingsDialog();
    // 关闭界面设计器
    void closeUiDesigners();
    // 关闭时间监视器
    void closeTimeInspector();
    // 关闭所有HMI窗口
    void closeHmiWindows();
    // 关闭所有当前项目相关辅助窗口
    void closeAllProjectWindows();



    // 检查当前项目是否需要保存
    Result checkUnsavedProject();
    // 将窗口对象转为文档对象
    void guiToDoc();
    // 将文档对象转为窗口对象
    void docToGui(__qsync__, bool restoreFullGui);
    // 保存窗口对象位置
    nlohmann::json saveGuiPoistion();
    
    // 恢复窗口对象位置(仅恢复Dock)  
    void restoreDockOnly(const nlohmann::json &json);
    // 恢复窗口对象位置
    void restoreGuiPosition(const nlohmann::json &json, bool restoreFullGui);

    // 创建显示进度条函数，函数析构时，进度条对象也析构
    Project::ShowProgress newLoadingProgressBar(const QString &prompt);

    // 运行配方关联的hmi  
    bool runSceneHmi(const std::shared_ptr<Scene> &scene);

    // 新建文件
    Result newProject(__qsync__);
    // 打开文件
    Result openProject(__qsync__, QString path, bool refreshGuiAfterBind, const std::optional<int>& projectId = {}, const std::optional<int>& recipeId = {});
    // 打开项目档案
    ResultEx<QString> openArchive(__qsync__);

    // 导入json
    Result importJson(__qsync__,
        const nlohmann::json &json,
        const std::string &type,
        Result(ProjectFile:: *importScene)(const nlohmann::json &json, const Project::ShowProgress &showProgressFunc));

    // 获取当前取得焦点的配方、流程或工具
    // 注意：根据界面焦点状况，返回项目均可能为空
    void getFocusedItem(
        std::shared_ptr<Scene> &scene,
        std::shared_ptr<Flow> &flow,
        std::vector<std::shared_ptr<Task>> &tasks);

    // 加入维护线程，避免加载dll时出现messagebox挡住去路...
    std::thread antiMsgBoxThread_;
    std::atomic<bool> antiMsgBoxThreadRunning_;
    void antiMsgBoxStart();
    void antiMsgBoxStop();

    void onTextBrowserWrite(QTextBrowser *browser, const QDateTime &now, int level, const QString &msg);

    void useLanguage(const std::string &localeName, QAction *action);

    // MainWindow上的监听界面事件
    using GlobalListenerItr = std::weak_ptr<promise::PromiseEventListener>; 
    GlobalListenerItr listenerConsoleKeyPress_;
    void setupConsoleEventFilter();
};

void MainWindow::cppReceive(QString textIn) {
    LOG_INFO("mainwindow receive: {}\n", textIn);
}


MainWindow::MainWindow(uint32_t autoClose_ms,
    bool isLastRunOk,
    const std::function<void()> &mainClear,
    QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , impl_(new Impl(this)) {
    __qsync__;

    LOG_INFO("{} = {}", tr("main window created"), this);

    impl_->inUpdateReleaseMode_ = false;
    impl_->projectDockFlowsVisible_ = true;
    impl_->projectDockWidgetsVisible_ = true;
    impl_->projectDockFlowEditorVisible_ = true;
    impl_->isLastRunOk_ = isLastRunOk;
    impl_->mainClear_ = mainClear;
    impl_->configDialog_ = nullptr;
    impl_->varBrowser_ = nullptr;
    impl_->alarmBrowser_ = nullptr;
    impl_->projectParameters_ = nullptr;
    impl_->projectUpdateSettingsDialog_ = nullptr;
    impl_->uiList_ = nullptr;
    impl_->timeInspector_ = nullptr;
    impl_->alarmTriggers_ = nullptr;
    impl_->alarmHistory_ = nullptr;
    impl_->isClosing_ = false;
    impl_->isQuiteClosing_ = false;
    impl_->isAutoClosing_ = false;
    impl_->disableNewFlowEditor_ = false;
    impl_->ngToolTip_ = nullptr;
    impl_->ngToolTipShowCount_ = 0;
    impl_->projectArchive_ = nullptr;

    // 注册变量表
    varAddTranslation("projectFile", tr("projectFile"));
    varSet("projectFile", &impl_->projectFile_);

    ui->setupUi(this);

    //ui->actionProjectUpgradeConfigurations->setVisible(false);

    // 设定docker窗口
    impl_->dockManager_ = new ads::CDockManager();
    QLayout *layout = ui->centralWidget->layout();
    layout->addWidget(impl_->dockManager_);
    //impl_->dockManager_->setCentralWidget(ui->centralWidget);

    impl_->dockWidgetMonitor_ = new DockWidgetMonitor(impl_->dockManager_, this);

    // Add the dock widget to the top dock widget area
    auto makeDocker = [this](ads::DockWidgetArea area, ads::CDockWidget *&adsDockWidget, QWidget *oldDockWidget) {
        adsDockWidget = new ads::CDockWidget(oldDockWidget->windowTitle(), this);

        // 替换名字
        QString objectName = oldDockWidget->objectName();
        oldDockWidget->setObjectName(objectName + "_bak");
        adsDockWidget->setObjectName(objectName);

        QDockWidget *qDockWidget = dynamic_cast<QDockWidget *>(oldDockWidget);
        if (qDockWidget)
            qDockWidget->setTitleBarWidget(new QWidget());
        adsDockWidget->setWidget(oldDockWidget);
        adsDockWidget->setMinimumSizeHintMode(ads::CDockWidget::MinimumSizeHintFromDockWidget);
        impl_->dockManager_->addDockWidget(area, adsDockWidget);
    };

    makeDocker(ads::CenterDockWidgetArea, impl_->dockWidget_flowEditor_, ui->dockWidget_flowEditor);
    makeDocker(ads::BottomDockWidgetArea, impl_->dockWidget_mainUi_, ui->dockWidget_mainUi);
    makeDocker(ads::LeftDockWidgetArea, impl_->dockWidget_flows_, ui->dockWidget_flows);
    makeDocker(ads::RightDockWidgetArea, impl_->dockWidget_widgets_, ui->dockWidget_widgets);
    makeDocker(ads::RightDockWidgetArea, impl_->dockWidget_widgetAttribute_, ui->dockWidget_widgetAttribute);
    makeDocker(ads::BottomDockWidgetArea, impl_->dockWidget_console_, ui->dockWidget_console);
    makeDocker(ads::BottomDockWidgetArea, impl_->dockWidget_alarm_, ui->dockWidget_alarm);


    // 合并dock默认style到 style.css，并以style.css的设定优先。
    QString styleStr = impl_->dockManager_->styleSheet();
    auto style = readTextFile(":/style.css");
    if (style.result_) {
        styleStr = styleStr + style.object_;
    }
    impl_->dockManager_->setStyleSheet("");
    this->setStyleSheet(styleStr);


    this->setContextMenuPolicy(Qt::ContextMenuPolicy::NoContextMenu);
    this->setAcceptDrops(true);

    impl_->setupUserOpLog();
    impl_->setupStatusbar();
    impl_->setupMainToolbar();
    impl_->setupFlowEditors();
    impl_->setupImageFrames();
    impl_->setupHmiMenu();

    AppConfig &appConfig = Singleton<AppConfig>::getInstance();
    impl_->updateRecentProjectMenu();
    appConfig.recentProjects_.onChanged([=] {
        impl_->updateRecentProjectMenu();
    });

    // 初始化项目档案对话框
    impl_->updateProjectArchive();
    ArchiveConfig &arConfig = Singleton<ArchiveConfig>::getInstance();
    // 打开项目更新档案对话框
    arConfig.onChanged([=] {
        impl_->updateProjectArchive();
    });

    //ui->buttonTest->hide();
    //ui->topTab->hide();
    //ui->actionTimeInspector->setVisible(false);
    ui->actionProjectUpgrade->setVisible(false);
    ui->actionProjectUpgradeConfigurations->setVisible(false);
    impl_->dockWidget_widgetAttribute_->toggleView(false);

    // 隐藏参数设计器 
    //ui->actionUiDesigner->deleteLater();
    
    // 禁止控制台窗口反编辑  
    ui->console->setUndoRedoEnabled(false);
    ui->console->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->console, &QTextBrowser::customContextMenuRequested, this, &MainWindow::onConsoleContextMenu);
    ui->alarm->setUndoRedoEnabled(false);

    // 双击不改变展开方式
    ui->flowTree->setExpandsOnDoubleClick(false);
    // 左侧流程列表不需要表头
    ui->flowTree->setHeaderHidden(true);
    // 设置流程右键
    ui->flowTree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->flowTree, &QTreeWidget::customContextMenuRequested, [=](const QPoint &point) {
        impl_->clickFlowFree(point);
    });

    auto setFlowVisible = [](const std::shared_ptr<Flow> &flow, bool isVisible) {
        if (flow) {
            if (isVisible) {
                if (flow->isCollpased_)
                    flow->isCollpased_ = false;
                FlowEditor::setFocusedFlow(nullptr, flow);
            }
            else {
                if (!flow->isCollpased_)
                    flow->isCollpased_ = true;
            }
        }
    };

    connect(ui->flowTree, &QTreeWidget::itemDoubleClicked, [=](QTreeWidgetItem *item, int column) {
        if (item != nullptr) {
            const QVariant &userRole = item->data(0, Qt::UserRole);
            if (!(userRole.isValid())) {
            }
            else if (userRole.canConvert<std::weak_ptr<Flow>>()) {
                auto flow = userRole.value<std::weak_ptr<Flow>>().lock();
                LOG_INFO("flow = {}", flow);
                setFlowVisible(flow, true);
            }
        }
    });
    anypro::detail::set_var("setFlowVisible", setFlowVisible);

    // 双击不改变展开方式
    ui->widgetTree->setExpandsOnDoubleClick(false);
    // 设定工具列表可以拖动
    ui->widgetTree->setSelectionMode(QAbstractItemView::SingleSelection);
    // 自定义拖拽样式
    ui->widgetTree->setDragEnabled(false);
    connect(ui->widgetTree, &QTreeWidget::itemPressed, [=](QTreeWidgetItem *item, int column) {
        impl_->startDragWidget(item, column);
    });

    // 显示区设最大
    //ui->flowViewer->showMaximized();

    //QQuickItem *item = this->ui->quickWidget->rootObject();
    //item->window();
    //connect(this, SIGNAL(cppSend(QString)), this->ui->quickWidget->rootObject(), SIGNAL(qmlReceive(QString)));
    //connect(this->ui->quickWidget->rootObject(), SIGNAL(qmlSend(QString)), this, SLOT(cppReceive(QString)));

    connect(this, &MainWindow::quiteClose, this, [=]() {
        impl_->isQuiteClosing_ = true;
        this->close();
    });

    // 设定输出
    connect(this, &MainWindow::consoleWrite, this, &MainWindow::onConsoleWrite);
    connect(this, &MainWindow::alarmWrite, this, &MainWindow::onAlarmWrite);
    // 设定最大行数，防止无限输出
    ui->console->document()->setMaximumBlockCount(200);
    ui->alarm->document()->setMaximumBlockCount(200);

#if 1
    impl_->alarmManager_.onChanged(BindingData::kInsert, [this] {
        if (impl_->alarmBrowser_ != nullptr) {
            static_cast<AlarmBrowser *>(impl_->alarmBrowser_)->delayUpdate();
        }
    });

    // 消息输出
    if (true) {
        // 只输出notify消息
        spdLogSetNotifier([=](const spdlog::source_loc &sourceLoc, int level, const std::string_view &msg) {
            QString message = QString::fromUtf8(msg.data(), msg.size());
            // 防止太长造成窗口假死
            if (message.length() > 512)
                message = message.left(512) + "...";
            if (!message.endsWith('\n'))
                message += "\n";

            const LogLocalData &logLocalData = LogLocalData::getData();
            logLocalData.moduleName_;
            logLocalData.tags_;

            // 消息输出到运行日志面板和运行告警面板
            const QDateTime &now = QDateTime::currentDateTime();
            if (level <= spdlog::level::info) {
                emit this->consoleWrite(now, level, message);
            }
            else {
                emit this->alarmWrite(now, level, message);
                impl_->alarmManager_.onAlarm(now, sourceLoc, level, msg);
            }
        });
    }
    else if (!((TlApplication *)qApp)->useConsole_) {
        impl_->dockWidget_console_->toggleView(false);
        impl_->dockWidget_alarm_->toggleView(false);
    }
    else {
        std::string messageHandlerName = fmt::format("main_window_console{}", this);
        // spdlog输出到本窗口
        Singleton<HostConsole>::getInstance().enableMainWindowOutput(true);
        auto messageOutputToConsole = [this](int level, const spdlog::source_loc &sourceLoc, const void *ptr, size_t size) {
            if (!Singleton<HostConsole>::getInstance().isMainWindowOutputEnabled())
                return;

            QString message = QString::fromUtf8((const char *)ptr, (int)(size));
            // 防止太长造成窗口假死
            if (message.length() > 512)
                message = message.left(512) + "...";

            if (!Singleton<HostConsole>::getInstance().isMatchGlobalConfig(sourceLoc, message))
                return;
            const QDateTime &now = QDateTime::currentDateTime();
            if (level <= spdlog::level::info)
                emit this->consoleWrite(now, level, message);
            else
                emit this->alarmWrite(now, level, message);
        };
        Singleton<HostConsoleSink>::getInstance().installHandler(messageHandlerName, messageOutputToConsole);
    }
#else
    // fmtlib输出到窗口
    auto messageOutputToConsole = [this](const void *ptr, size_t size, size_t count, FILE *stream) {
        (void)stream;

        QString message = QString::fromUtf8((const char *)ptr, (int)(size * count));
        if (!Singleton<HostConsole>::getInstance().isMatchGlobalConfig(message))
            return;

        std::string prefix = fmt::format("[{}] [{}] ", HostConsole::time(), HostConsole::threadId());
        QString msg = QString::fromStdString(prefix) + message;
        emit this->consoleWrite(msg);
    };
    fmt::output_filter::install_handler(messageHandlerName, messageOutputToConsole);
#endif

    // 设定docker窗口和菜单关联
    impl_->connectDockerToMenu();

    // 加载js引擎
#if 1
    // 先加载个对话框，否则在第三方dll的初始化中调用 QMessageBox 会卡死，why？qt的bug？
    qApp->setApplicationDisplayName("");
    QMessageBox *openbox = new QMessageBox(
        QMessageBox::Icon::Information,
        "Loading ...",
        tr("Loading ..."),
        QMessageBox::StandardButton::No,
        this);
    QTimer::singleShot(0, openbox, [=] {
        openbox->accept();
    });
    openbox->setWindowFlag(Qt::Dialog, false);
    openbox->setWindowFlag(Qt::Window, true);
    openbox->setWindowFlag(Qt::FramelessWindowHint, true);
    openbox->resize(3, 3);
    openbox->exec();
    delete openbox;

    // 显示在标题栏上的应用程序名后缀,会在所有窗口SetWindowsTitle的后面
    qApp->setApplicationDisplayName("TL Vision 2");
#endif

    TlApplication::setMainWindow(this);

    ResMonitor &resMonitor = singleton<ResMonitor>();
    if (!resMonitor.isStarted()) {
        QString info = tr("<font color='red'>ResMonitor Error</font>");
        resMonitor.showStatusBarInfo(info);
        QWidget *processInfo = impl_->statusbars_["99_ProcessInfo"];
        promise::addEventListener(processInfo, QEvent::MouseButtonPress,
            [this, processInfo](QObject *, QEvent *event) {
            QMouseEvent *mevent = dynamic_cast<QMouseEvent *>(event);
            if (mevent && mevent->button() == Qt::LeftButton) {
                // 左键点击事件处理  
                QString text = tr("ResMonitor initialization error!<br><br>"
                    "Please try run \"lodctr / R\" in administrator mode to reset the performance counter, <br>"
                    "and then to restart this application again.");

                QMessageBox::warning(this, tr("Warning"), text);
            }
            return false;
        });
    }

    ui->actionLoadAllPlugins->setVisible(false);
    const auto &dlls = impl_->getStartupProjectDlls();
    impl_->loadPluginDlls(dlls);
    TlApplication::initJsEngineRuntime();

    // 加载完dll，再监控主界面线程僵死  
    watchDogStart();

    std::shared_ptr<VariantMap> configItems_ = VariantMap::newInstance(LOC);
    //configItems_->set("v", 1);
    //configItems_->set("v2", QString("xdd"));
    //configItems_->set("a", QString("xxx"));
    configItems_->set("color", QColor(1, 2, 3, 4));
    //configItems_->set("color2", QColor());
    //configItems_->set("font", QFont());

    // 默认图像区域（这个可能会删除）
    //connect(ui->imageBundleViewer, &ImageBundleViewer::focusIn, this, &MainWindow::onImageViewerFocusIn);
    // 打开配置
    connect(this, &MainWindow::openSettings, this, &MainWindow::onOpenSettings);

    connect(this, &MainWindow::showLogin, this, &MainWindow::onShowLogin);

    // 延迟任务
    promise::delay(0).then(PM_LOC, [this]() {
        // 滚动条到最后
        QScrollBar *sb = ui->console->verticalScrollBar();
        if (sb != nullptr)
            sb->setValue(sb->maximum());
        PTI;
        sb = ui->alarm->verticalScrollBar();
        if (sb != nullptr)
            sb->setValue(sb->maximum());
        return promise::delay(100);
    }).then(PM_LOC, []() {
        // 通知父进程程序启动成功
        boost::interprocess::named_mutex mutex(boost::interprocess::open_or_create, PROC_STATUS_INTERPROCESS_NAME);
        writeProcStatus(ProcStatus::kSuccess, mutex);
    });

    if (autoClose_ms != 0) {
        promise::delay(autoClose_ms).then(PM_LOC, [this]() {
            this->close();
        });
    }

    // 更新工具列表
    qSetTimeout(LOC, [=](const Loc &loc) {
        loc.trace();
        ui->widgetTree->setHeaderHidden(true);
        // Widget创建时机
        // 仅仅用于生成WidgetTree控件界面内容
        // updateWidgetTree退出时Widget就析构了,不附着在task上
        // xxxWidget构造此时task为nullptr
        // MainWindow::updateWidgetTree -> TlModule::newWidget -> 
        // WidgetTypeCache.newWidgetFuncs_['widget_name'] -> 
        // Widget::registerWidget注册的NewWidget函数实例化一个XXXWidget对象
        impl_->updateWidgetTree();
    }, 0).wait(__qwait__);

    //初始化授权绑定  
    impl_->setupAuthItems();

    TlApplication::setMainWindowReady(true);

    // 登录
    User::tryAutoLogin();
    if(0) {
        LoginDialog loginDialog(this);
        int ret = loginDialog.exec();
        LOG_INFO("ret of loginDialog = {}", ret);
        if (!ret) {
            qSetTimeout(LOC, [=](const Loc &loc) {
                emit this->quiteClose();
            }, 0);
            return;
        }
    }

    // 启动项目  
    impl_->setupStartupProject(__qwait__);

    // 设定清理时钟  
    impl_->setupTimePlan();

    // 安装console界面事件响应者
    impl_->setupConsoleEventFilter();
}

MainWindow::~MainWindow() {
    TlApplication::setMainWindowReady(false);
    TlApplication::setMainWindow(nullptr);

    varUnregisterAll("projectFile");

    std::string messageHandlerName = fmt::format("main_window_console{}", this);
#if 1
    Singleton<HostConsoleSink>::getInstance().removeHandler(messageHandlerName);
#else
    fmt::output_filter::remove_handler(messageHandlerName);
#endif

    /* TODO: do with QQuickWidget memory leak ? 
    if(ui->quickWidget != nullptr) {
        ui->verticalLayout_7->removeWidget(quickWidget);
        delete quickWidget;
    }
	*/

    impl_->eventHolderCloser_.cancel();

    {
        auto imageFrames = impl_->focusedImageFrames_;
        impl_->focusedImageFrames_.clear();
        for (ImageFrame *imageFrame : imageFrames)
            delete imageFrame;
    }
    {
        auto imageFrames = impl_->sharedImageFrames_;
        impl_->sharedImageFrames_.clear();
        for (ImageFrame *imageFrame : imageFrames)
            delete imageFrame;
    }
    auto globalImageFrame = impl_->globalImageFrame_;
    impl_->globalImageFrame_ = nullptr;
    delete globalImageFrame;

    if (impl_->varBrowser_ != nullptr)
        delete impl_->varBrowser_;
    if (impl_->alarmBrowser_ != nullptr)
        delete impl_->alarmBrowser_;
    if (impl_->projectParameters_ != nullptr)
        delete impl_->projectParameters_;
    if (impl_->projectUpdateSettingsDialog_ != nullptr)
        delete impl_->projectUpdateSettingsDialog_;
    if (impl_->uiList_ != nullptr)
        delete impl_->uiList_;
    if (impl_->timeInspector_ != nullptr)
        delete impl_->timeInspector_;
    if (impl_->alarmTriggers_ != nullptr)
        delete impl_->alarmTriggers_;
    if (impl_->alarmHistory_ != nullptr)
        delete impl_->alarmHistory_;

    if(impl_->configDialog_ != nullptr)
        delete impl_->configDialog_;
    impl_->closeTimeInspector();

    if (impl_->projectArchive_ != nullptr)
        delete impl_->projectArchive_;
    promise::PromiseEventFilter& filter = promise::PromiseEventFilter::getSingleInstance();
    filter.removeEventListener(impl_->listenerConsoleKeyPress_);
    watchDogStop();
    delete ui;  // 最后删除.
    LOG_INFO("MainWindow destructor finish");
}

Project::NewWidget MainWindow::newWidgetFunc() {

    // 获取最大的同类型widget编号
    auto getMaxWidgetNumber = [](
        const std::shared_ptr<Task> &theTask,
        const std::string &widgetName,
        const std::string &widgetLocalName) -> std::pair<int, int> {

        // 获取task关联的场景
        std::vector<std::shared_ptr<Scene>> scenes;
        std::shared_ptr<Project> project = theTask->getProject();
        if (project) {
            scenes.push_back(project->getFocusedScene());
            scenes.push_back(project->getSharedScene());
        }
        
        scenes.push_back(theTask->getScene());

        // 编号用instanceId
        // 遍历组件对象
        int maxId = -1;
        int maxCustomNum = -1;
        auto checkMaxNumber = [&](const std::shared_ptr<Flow> &flow) {
            if (!flow) return;
            for (auto &task : flow->tasks_) {
                if (task == theTask) continue; // 不计入自身  
                const std::shared_ptr<Widget> &widget = task->widget_;
                if (!widget) continue;

                // 获取widget名字的“名字+编号”
                if (strStartWith(widget->customName(), widgetLocalName)) {
                    const std::string &strNumber = widget->customName().substr(widgetLocalName.size());
                    try {
                        int iNumber = std::stoi(strNumber);
                        if (maxCustomNum < iNumber)
                            maxCustomNum = iNumber;
                    }
                    catch (const std::invalid_argument &ex) {
                        (void)ex;
                    }
                    catch (const std::out_of_range &ex) {
                        (void)ex;
                    }
                }

                if (widget->name_ == widgetName && maxId < widget->instanceId())
                    maxId = widget->instanceId();
            }
        };

        for (const auto &scene : scenes) {
            if (scene) {
                checkMaxNumber(scene->globalFlow_);
                for (const auto &flow : scene->flows_) {
                    checkMaxNumber(flow);
                }
            }
        }
        // 检查自身所在的flow（可能scene没有包含）   
        checkMaxNumber(theTask->getFlow());
        return { maxId, maxCustomNum };
    };

    return [=](const Widget::WidgetType &widgetType,
        const std::shared_ptr<Task> &task,
        const std::string &customName,
        bool allowModifyCustomName,
        int instanceId) -> std::shared_ptr<Widget> {

        // 创建一个对象用于模仿
        std::string newCustomName = customName;
        if (instanceId < 0 || newCustomName.empty()) {
            std::shared_ptr<Widget> widget = this->tlModule_->newWidget(this, widgetType, nullptr, "", -1);

            if (instanceId < 0) {
                auto ids = getMaxWidgetNumber(task, widget->name_, widget->localName_);
                instanceId = ids.first + 1;
                int maxCustomNum = ids.second + 1;

                if (allowModifyCustomName) {
                    bool ok;
                    std::string oldCustomName = customName;
                    if (oldCustomName.empty()) {
                        oldCustomName = fmt::format("{}{}", (widget->localName_->empty() ? widget->name_ : widget->localName_), maxCustomNum);
                    }

                    QString newName = QInputDialog::getText(this, MainWindow::tr("Create new task"),
                        MainWindow::tr("Task Name<br/><font color='red'>Task name may not be changed later</font>"),
                        QLineEdit::Normal,
                        QString::fromStdString(oldCustomName),
                        &ok).trimmed();
                    if (!ok) {
                        return nullptr;
                    }
                    if (newName.isEmpty()) {
                        QMessageBox::critical(this, MainWindow::tr("Create task"), MainWindow::tr("Task name is empty!"));
                        return nullptr;
                    }

                    newCustomName = newName.toStdString();
                }
            }
            else if (newCustomName.empty()) {
                auto ids = getMaxWidgetNumber(task, widget->name_, widget->localName_);
                int maxCustomNum = ids.second + 1;
                newCustomName = fmt::format("{}{}", (widget->localName_->empty() ? widget->name_ : widget->localName_), maxCustomNum);
            }
        }

        // 检查重名
        auto project = task->getProject();
        auto sceneId = project->objIdCreator_.getIdStr(task->getScene().get());
        const void *objAddr = project->objIdCreator_.getObjAddr(sceneId, "task", newCustomName).first;
        if (objAddr != nullptr) {
            QMessageBox::critical(this, tr("Error"), tr("Duplicated widget name!"));
            return nullptr;
        }

        // 创建实际对象
        auto start = std::chrono::steady_clock::now();
        std::shared_ptr<Widget> widget = this->tlModule_->newWidget(this, widgetType, task, newCustomName, instanceId);
        auto now = std::chrono::steady_clock::now();
        long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
        LOG_INFO("widget {}, load time = {} ms", newCustomName, ms);

        if (widget) {
            task->widget_ = widget;

            // 注册到全局变量表
            varRegisterTable(widget->varPrefix() + ".data", *task->datTable_);
            varRegisterTable(widget->varPrefix() + ".config", *widget->cfgTable_);
        }

        return widget;
    };
}

// 显示版本提示
void MainWindow::Impl::checkModuleVersions() {
    AppConfig &appConfig = Singleton<AppConfig>::getInstance();
    if (appConfig.showUnmatchedDllVersion_) {
        promise::delay(0).then(PM_LOC, [=] {
            // dll版本信息
            const std::map<std::string, std::string> &moduleVersions = anypro::get_module_versions();
            std::shared_ptr<std::map<std::string, std::string>> unmatchedModuleVersions = std::make_shared<std::map<std::string, std::string>>();
            const char *sdkVersion = getSdkVersion();
            for (const auto &item : moduleVersions) {
                const std::string &modulePath = item.first;
                const std::string &moduleVersions = item.second;
                LOG_INFO("version path = {}, module versions={}", modulePath, moduleVersions);

                std::vector<std::string> arrModuleVersions = strSplit(moduleVersions, ",");
                std::string moduleSdkVersion = arrModuleVersions[0];

                if (strcmp(sdkVersion, moduleSdkVersion.c_str()) != 0) {
                    unmatchedModuleVersions->insert(item);
                }
            }

            if (!unmatchedModuleVersions->empty()) {
                QString unmatched;
                size_t count = 0;
                for (const auto &item : *unmatchedModuleVersions) {
                    const std::string &modulePath = item.first;
                    const std::string &moduleVersions = item.second;

                    unmatched += tr("<nobr>%1: %2</nobr><br>")
                        .arg(QString::fromStdString(baseName(modulePath)))
                        .arg(QString::fromStdString(moduleVersions));
                    if (++count >= 20 && count != unmatchedModuleVersions->size()) {
                        unmatched += tr("(%1 lines not shown ...)<br>").arg(unmatchedModuleVersions->size() - count);
                        break;
                    }
                }
                unmatchedModuleVersions->clear();

                QMessageBox *mbox = new QMessageBox(
                    QMessageBox::Icon::Critical,
                    qApp->applicationName(),
                    tr("version mot match<br><br>sdk version: %1<br>%2<br>Ignore and continue to load?")
                        .arg(QString::fromStdString(sdkVersion))
                        .arg(unmatched),
                    (QMessageBox::StandardButton::Yes | QMessageBox::StandardButton::No),
                    thiz_);
                mbox->setWindowFlag(Qt::WindowStaysOnTopHint, true);
                auto ret = mbox->exec();
                delete mbox;
                if (ret != QMessageBox::StandardButton::Yes) {
                    forceTerminateProcess(0);
                }
            }
        });
    }

}

void MainWindow::Impl::loadLatestProject(
    const QString &path,
    const std::string &startupSceneName,
    const std::vector<std::string> &startupFlowNames,
    const std::optional<int>& recipeId,
    FlowRunner::RunCmd runCmd) {
    qSetTimeout(LOC, [=](const Loc &loc) {
        __qsync__;
        loc.trace();

        auto loadAndRun = [&](__qsync__){
            if (runCmd == FlowRunner::RunCmd::kRunOneShot
                || runCmd == FlowRunner::RunCmd::kRunLooply)
                this->openProject(__qwait__, path, false, {}, recipeId); // 运行时再刷新界面
            else
                this->openProject(__qwait__, path, true, {}, recipeId);  // 加载后直接刷新界面

            while (!this->projectFile_->isAllStopped()) {
                QCoreApplication::processEvents();
            }
            LOG_NOTIFY("start to run looply");

            if (runCmd == FlowRunner::RunCmd::kRunOneShot
                || runCmd == FlowRunner::RunCmd::kRunLooply) {
                this->runCurrentProject(runCmd,
                    (startupSceneName.empty() ? nullptr : startupSceneName.c_str()),
                    startupFlowNames);
            }
        };

        const CmdArg &cmdArg = singleton<CmdArg>();
        bool useCommandLineStartup = !cmdArg.startupProject_.empty();

        if (!this->isLastRunOk_ && !useCommandLineStartup) {
            TimedoutMessageBox messageBox(thiz_);
            messageBox.setText(MainWindow::tr("Last run project may failed --\n\n  %1\n\nLoad it anyway?").arg(path));
            messageBox.setIcon(QMessageBox::Question);
            messageBox.setStandardButtons(QMessageBox::Yes|QMessageBox::No);
            messageBox.setAutoClose(tr("( auto close in %1 seconds )"), 5);
            int res = messageBox.exec();
            if (res == QMessageBox::Yes || res == 0) {
                loadAndRun(__qwait__);
            }
        }
        else {
            loadAndRun(__qwait__);
        }
    }, 0);
}


void MainWindow::Impl::bindProjectFile(bool refreshGuiAfterBind, const Project::ShowProgress &showProgress) {
    const std::shared_ptr<Project> &sProject = projectFile_->getProject();
    const std::weak_ptr<Project> &wProject = sProject;

    std::shared_ptr<Project::ShowProgress> showProgressPtr = std::make_shared<Project::ShowProgress>(showProgress);
    auto showProgressEnd = [=]() mutable {
        if (showProgressPtr) {
            auto showProgress = *showProgressPtr;
            showProgressPtr.reset();
            if (showProgress) {
                showProgress(1, {}, tr("Load finished").toStdString());
            }
        }
    };

    
    // （合并）更新发布模式状态
    auto oneShotUpdateReleaseMode = qOneShot(thiz_, [=](const Loc &loc) {
        loc.trace();
        this->updateReleaseMode();
    });

    // （合并）更新主工具条
    auto oneShotUpdateMainToolbar = qOneShot(thiz_, [=](const Loc &loc) {
        loc.trace();
        this->updateMainToolbar();
    });

    // （合并）更新项目流程树
    auto oneShotUpdateFlowTree = qOneShot(thiz_, [=](const Loc &loc) {
        loc.trace();
        this->updateFlowTree();
    });

    // （合并）更新流程编辑器
    auto oneShotUpdateFlowEditor = qOneShot(thiz_, [=](const Loc &loc) {
        loc.trace();
        this->updateFlowEditors();
    });

    //（合并）更新图像显示托盘
    auto oneShotUpdateImageFrame = qOneShot(thiz_, [=](const Loc &loc) {
        loc.trace();
        this->updateImageFrames();
    });

    // （合并）更新场景对象位置
    auto oneShotRestoreSceneGuiPosition = qOneShot(thiz_, [=](const Loc &loc) {
        loc.trace();
        // updateImageFrames() 调用 setFlow，通过再次触发事件绑定生成ImagePanel
        // 因此，需要用qSetTimeout延后到下一时钟周期，才有响应的ImagePanel，才可以恢复ImagePanel的位置。
        __qsync__;
        qSetTimeout(LOC, [=](const Loc &loc) {
            loc.trace();
            auto sProject = wProject.lock();
            if (sProject && sProject == Project::current()) {
                auto focusedScene = sProject->getFocusedScene();
                auto sharedScene = sProject->getSharedScene();
                thiz_->restoreSceneGuiPosition(focusedScene, sharedScene);
            }
        }, 0).wait(__qwait__);
    });

    auto setProjectChanged = [=] {
        auto sProject = wProject.lock();
        if (sProject)
            sProject->setUnsaved(true);
    };

    // 幂等事件支持

    auto bindTaskEvent = [=](const std::weak_ptr<Task> &wTask) {
        auto sTask = wTask.lock();
        if (!sTask) return;

        sTask->widget_->customName_.onChanged(UNI_EVT(&eventHolder_), [=]() {
            setProjectChanged();
            oneShotUpdateFlowTree.call(LOC);
        });

        sTask->widget_->comment_.onChanged(UNI_EVT(&eventHolder_), [=]() {
            setProjectChanged();
            oneShotUpdateFlowTree.call(LOC);
        });

        // （合并）更新流程状态
        auto oneShotUpdateTaskStatus = qOneShot(thiz_, [=](const Loc &loc) {
            loc.trace();
            this->updateTaskStatus(wTask);
        });
        sTask->onChanged(UNI_EVT(&eventHolder_), [=] {
            oneShotUpdateTaskStatus.call(LOC);
        });
    };

    auto bindFlowEvent = [=](const std::weak_ptr<Flow> &wFlow) {
        auto sFlow = wFlow.lock();
        if (!sFlow) return;

        sFlow->name_.onChanged(UNI_EVT(&eventHolder_), [=]() {
            setProjectChanged();
            oneShotUpdateFlowTree.call(LOC);
            oneShotUpdateFlowEditor.call(LOC);
        });

        sFlow->isDefaultRunning_.onChanged(UNI_EVT(&eventHolder_), [=]() {
            setProjectChanged();
            oneShotUpdateMainToolbar.call(LOC);
            oneShotUpdateFlowTree.call(LOC);
            //oneShotUpdateFlowEditor.call(LOC);
        });

        sFlow->isCollpased_.onChanged(UNI_EVT(&eventHolder_), [=]() {
            setProjectChanged();
            //oneShotUpdateFlowTree.call(LOC);
            oneShotUpdateFlowEditor.call(LOC);
        });

        sFlow->tasks_.onChanged(UNI_EVT(&eventHolder_), [=](BindingData::EventType eventType, int eventValue) {
            LOG_DEBUG("eventType = {}", eventType);
            auto sFlow = wFlow.lock();
            if (!sFlow) return;
            if (eventType == BindingData::kInsert || eventType == BindingData::kEdit) {
                int index = eventValue;
                bindTaskEvent(sFlow->tasks_[index]);
            }
            else if (eventType == BindingData::kRemove) {
            }
            else if (eventType == BindingData::kReset) {
                for (const auto &task : sFlow->tasks_) {
                    bindTaskEvent(task);
                }
            }
            setProjectChanged();
            oneShotUpdateFlowTree.call(LOC);
            oneShotUpdateImageFrame.call(LOC);
            oneShotUpdateMainToolbar.call(LOC);
        });

        // （合并）更新流程状态
        auto oneShotUpdateFlowStatus = qOneShot(thiz_, [=](const Loc &loc) {
            loc.trace();
            this->updateFlowStatus(wFlow);
        });
        sFlow->onChanged(UNI_EVT(&eventHolder_), BindingData::kStatusChanged, [=] {
            oneShotUpdateFlowStatus.call(LOC);
        });

        for (const auto &task : sFlow->tasks_) {
            bindTaskEvent(task);
        }
    };

    auto bindSceneEvent = [=](const std::weak_ptr<Scene> &wScene) {
        auto sScene = wScene.lock();
        if (!sScene) return;

        sScene->name_.onChanged(UNI_EVT(&eventHolder_), [=]() {
            setProjectChanged();
            oneShotUpdateFlowTree.call(LOC);
        });
        sScene->focusedFlow_.onChanged(UNI_EVT(&eventHolder_), [=]() {
            setProjectChanged();
            oneShotUpdateFlowEditor.call(LOC);
            oneShotUpdateImageFrame.call(LOC);
            oneShotUpdateMainToolbar.call(LOC);
        });

        sScene->flows_.onChanged(UNI_EVT(&eventHolder_), [=](BindingData::EventType eventType, int eventValue) {
            PTD;
            auto sScene = wScene.lock();
            if (!sScene) return;
            if (eventType == BindingData::kInsert || eventType == BindingData::kEdit) {
                int index = eventValue;
                bindFlowEvent(sScene->flows_[index]);
            }
            else if (eventType == BindingData::kRemove) {
            }
            else if (eventType == BindingData::kReset) {
                for (const auto &flow : sScene->flows_) {
                    bindFlowEvent(flow);
                }
            }
            setProjectChanged();
            oneShotUpdateFlowTree.call(LOC);
            oneShotUpdateFlowEditor.call(LOC);
            oneShotUpdateImageFrame.call(LOC);
            oneShotUpdateMainToolbar.call(LOC);
        });

        for (const auto &flow : sScene->flows_) {
            bindFlowEvent(flow);
        }
        bindFlowEvent(sScene->globalFlow_);
    };

    auto bindProjectEvent = [=](const std::weak_ptr<Project> &wProject) {
        auto sProject = wProject.lock();
        if (!sProject || sProject != Project::current()) return;

        //sProject->onChanged(UNI_EVT(&eventHolder_), BindingData::kError, [=](const std::string &msg) {
        //    LOG_NOTIFY("{}", msg);
        //});

        sProject->onChanged(UNI_EVT(&eventHolder_), [=](BindingData::EventType eventType, const boost::any &eventValue) mutable {
            if (eventType != BindingData::kError) {
                auto sProject = wProject.lock();
                if (!sProject || sProject->isAllStopped()) {
                    showProgressEnd();
                }

                oneShotUpdateMainToolbar.call(LOC);
            }
        });

        sProject->isReleaseMode_.onChanged(UNI_EVT(&eventHolder_), [=]() {
            setProjectChanged();
            oneShotUpdateReleaseMode.call(LOC);
        });

        sProject->isEmuReleaseMode_.onChanged(UNI_EVT(&eventHolder_), [=]() {
            //setProjectChanged();
            oneShotUpdateReleaseMode.call(LOC);
        });

        sProject->focusedScene_.onChanged(UNI_EVT(&eventHolder_), [=]() {
            PTW;
            auto sProject = wProject.lock();
            this->runSceneHmi(sProject->getFocusedScene());

            setProjectChanged();
            oneShotUpdateFlowTree.call(LOC);
            oneShotUpdateImageFrame.call(LOC);
            oneShotUpdateMainToolbar.call(LOC);
            oneShotUpdateFlowEditor.call(LOC);
            oneShotRestoreSceneGuiPosition.call(LOC);
        });

        sProject->scenes_.onChanged(UNI_EVT(&eventHolder_), [=](BindingData::EventType eventType, int eventValue) {
            PTD;
            auto sProject = wProject.lock();
            if (!sProject) return;
            setProjectChanged();
            if (eventType == BindingData::kInsert || eventType == BindingData::kEdit) {
                int index = eventValue;
                bindSceneEvent(sProject->scenes_[index]);
                oneShotUpdateFlowTree.call(LOC);
            }
            else if (eventType == BindingData::kRemove) {
                oneShotUpdateFlowTree.call(LOC);
            }
            else if (eventType == BindingData::kReset) {
                for (const auto &scene : sProject->scenes_) {
                    bindSceneEvent(scene);
                }
                oneShotUpdateFlowTree.call(LOC);
            }
        });

        for (const auto &scene : sProject->scenes_) {
            bindSceneEvent(scene);
        }
        if (sProject->sharedScene_) {
            bindSceneEvent(sProject->sharedScene_);
        }
    };

    bindProjectEvent(wProject);
    TlApplication *tlApp = TlApplication::instance();
    tlApp->triggerChanged("project_loaded");

    oneShotUpdateReleaseMode.call(LOC);
    oneShotUpdateMainToolbar.call(LOC);
    oneShotUpdateFlowTree.call(LOC);
    oneShotUpdateImageFrame.call(LOC);
    oneShotUpdateMainToolbar.call(LOC);
    oneShotUpdateFlowEditor.call(LOC);
    oneShotRestoreSceneGuiPosition.call(LOC);


    auto focusedScene = sProject->getFocusedScene();
    auto sharedScene = sProject->getSharedScene();
    std::vector<std::shared_ptr<Scene>> scenes;
    if (focusedScene) scenes.push_back(focusedScene);
    if (sharedScene) scenes.push_back(sharedScene);
    if (scenes.size() > 0 && refreshGuiAfterBind) {
        sProject->runScenes(FlowRunner::kRunRefreshUi, scenes, false);
    }
}

// 初始化操作监听器
void MainWindow::Impl::setupUserOpLog() {
    auto opLog = [](const std::string &eventName) {
        return [eventName](QObject *object, QEvent *event) {
            if (object == nullptr || event == nullptr)
                return false;
            const QMetaObject *metaObject = object->metaObject();
            if (metaObject == nullptr)
                return false;

            // 告诉流程有用户输入，以便可以正确处理 kRunRefreshUi 动作  
            // 如果不管用户输入，一律刷新界面，有可能导致刷新停不下来  
            FlowRunner::notifyUserInput();

            const QString &objectName = object->objectName();
            //QWidget *widget = dynamic_cast<QWidget *>(object);
            //widget->windowIconText();

            // 获取对象自定义的描述  
            QString objectDescription;
            const auto &vGetDescription = object->property("getDescription");
            if (vGetDescription.isValid() && vGetDescription.canConvert<FnGetDescription>()) {
                FnGetDescription fnGetDescription = vGetDescription.value<FnGetDescription>();
                if (fnGetDescription)
                    objectDescription = fnGetDescription(object);
            }
            if (!objectDescription.isEmpty()) {
                objectDescription = QString(":(%1)").arg(objectDescription);
            }

            bool isPassword = false; // 疑似密码的就不要展示了
            bool isTarget = false;
            bool objectEnabled = true;
            QString objectText;
            QString topLevelTitle;
            QString topLevelText;
            if (QAbstractButton *widget = dynamic_cast<QAbstractButton *>(object); widget != nullptr) {
                objectText = widget->text();
                isTarget = true;
            }
            else if (QLineEdit *widget = dynamic_cast<QLineEdit *>(object); widget != nullptr) {
                isPassword = objectName.contains("password", Qt::CaseInsensitive)
                    || objectName.contains("pwd", Qt::CaseInsensitive)
                    || objectName.contains("passwd", Qt::CaseInsensitive);
                if (!isPassword) {
                    objectText = widget->text().left(32);
                }
                isTarget = true;
            }
            else if (QComboBox *widget = dynamic_cast<QComboBox *>(object); widget != nullptr) {
                objectText = QString("%1:%2").arg(widget->currentIndex()).arg(widget->currentText());
                isTarget = true;
            }
            else if (QCheckBox *widget = dynamic_cast<QCheckBox *>(object); widget != nullptr) {
                objectText = QString("%1:%2").arg(widget->checkState()).arg(widget->text());
                isTarget = true;
            }
            else if (QLabel *widget = dynamic_cast<QLabel *>(object); widget != nullptr) {
                objectText = widget->text().left(32);
                isTarget = true;
            }
            else if (QSpinBox *widget = dynamic_cast<QSpinBox *>(object); widget != nullptr) {
                objectText = widget->text();
                isTarget = true;
            }
            else if (QDoubleSpinBox *widget = dynamic_cast<QDoubleSpinBox *>(object); widget != nullptr) {
                objectText = widget->text();
                isTarget = true;
            }
            else if (QMenu *widget = dynamic_cast<QMenu *>(object); widget != nullptr) {
                //widget->actionAt()
                QAction *action = widget->activeAction();
                if (action != nullptr) {
                    objectText = action->text();
                }
                isTarget = true;
            }
            else if (QAction *widget = dynamic_cast<QAction *>(object); widget != nullptr) {
                objectText = widget->text();
                objectEnabled = widget->isEnabled();
                isTarget = true;
                QWidget *parentWidget = widget->parentWidget();
                QWidget *topLevelWidget = (parentWidget ? parentWidget->topLevelWidget() : nullptr);
                if (topLevelWidget)
                    topLevelTitle = topLevelWidget->windowTitle();
            }
            //else if (QPlainTextEdit *widget = dynamic_cast<QPlainTextEdit *>(object); widget != nullptr) {
            //}
            //else if (QTextEdit *widget = dynamic_cast<QTextEdit *>(object); widget != nullptr) {
            //}
            //else if (QSlider *widget = dynamic_cast<QSlider *>(object); widget != nullptr) {
            //}

            if (!isTarget)
                return false;


            QString nameStr = QStringLiteral("%1:%2")
                .arg(metaObject->className())
                .arg(objectName);

            if (QWidget *widget = dynamic_cast<QWidget *>(object); widget != nullptr) {
                objectEnabled = widget->isEnabled();
                QWidget *topLevelWidget = widget->topLevelWidget();
                if(topLevelWidget && topLevelTitle.isEmpty())
                    topLevelTitle = topLevelWidget->windowTitle();

                QMessageBox *messageBox = dynamic_cast<QMessageBox *>(topLevelWidget);
                if (messageBox) {
                    topLevelText = messageBox->text();
                }

                for (; widget != nullptr && widget != topLevelWidget; widget = widget->parentWidget()) {
                    const QString &objectName = widget->objectName();
                    const QMetaObject *metaObject = widget->metaObject();
                    if (metaObject && objectName.startsWith("qt_") && objectName.indexOf("_", 3) != -1) {
                        // 包含qt_以及至少再一个_  
                    }
                    else {
                        nameStr = QStringLiteral("%1:%2")
                            .arg(metaObject->className())
                            .arg(objectName);
                        break;
                    }
                }
            }


            QStringList eventText;
            if (QMouseEvent *mouseEvent = dynamic_cast<QMouseEvent *>(event); mouseEvent != nullptr) {
                Qt::MouseButtons buttons = mouseEvent->buttons();
                if (buttons.testFlag(Qt::LeftButton))
                    eventText.push_back("Left");
                if (buttons.testFlag(Qt::RightButton))
                    eventText.push_back("Right");
                if (buttons.testFlag(Qt::MiddleButton))
                    eventText.push_back("Middle");
            }
            
            if (QKeyEvent *keyEvent = dynamic_cast<QKeyEvent *>(event); keyEvent != nullptr) {
                Qt::KeyboardModifiers modifiers = keyEvent->modifiers();
                if(modifiers.testFlag(Qt::KeyboardModifier::AltModifier))
                    eventText.push_back("Alt");
                if (modifiers.testFlag(Qt::KeyboardModifier::ControlModifier))
                    eventText.push_back("Ctrl");
                if (modifiers.testFlag(Qt::KeyboardModifier::ShiftModifier))
                    eventText.push_back("Shift");
                if (modifiers.testFlag(Qt::KeyboardModifier::MetaModifier))
                    eventText.push_back("Meta");

                if (!isPassword) {
                    eventText.push_back(QString("key_%1").arg(keyEvent->key()));
                    eventText.push_back(keyEvent->text());
                }
            }

            if (!topLevelText.isEmpty())
                topLevelText = ">" + topLevelText;

            LOG_INFO("UI: {}{}>{}{}:{}{}, {}:{}",
                topLevelTitle,
                topLevelText,
                nameStr,
                (objectEnabled ? "" : "[X]"),
                objectText,
                objectDescription,
                eventName,
                eventText.join("|")
                );

            NAMED_LOG_WARN("tlvision2_em",
                "UI: {}{}>{}{}:{}{}, {}:{}",
                topLevelTitle,
                topLevelText,
                nameStr,
                (objectEnabled ? "" : "[X]"),
                objectText,
                objectDescription,
                eventName,
                eventText.join("|")
            );

            return false;
        };
    };

    eventHolderOpLog_.clear();
    eventHolderOpLog_ += promise::addEventListener(nullptr, QEvent::MouseButtonPress, opLog("MouseButtonPress"));
    eventHolderOpLog_ += promise::addEventListener(nullptr, QEvent::MouseButtonRelease, opLog("MouseButtonRelease"));
    eventHolderOpLog_ += promise::addEventListener(nullptr, QEvent::MouseButtonDblClick, opLog("MouseButtonDblClick"));
    eventHolderOpLog_ += promise::addEventListener(nullptr, QEvent::KeyPress, opLog("KeyPress"));
    eventHolderOpLog_ += promise::addEventListener(nullptr, QEvent::KeyRelease, opLog("KeyRelease"));
    eventHolderOpLog_ += promise::addEventListener(nullptr, QEvent::Wheel, opLog("Wheel"));
    eventHolderOpLog_ += promise::addEventListener(nullptr, QEvent::DragEnter, opLog("DragEnter"));
    eventHolderOpLog_ += promise::addEventListener(nullptr, QEvent::DragLeave, opLog("DragLeave"));
    eventHolderOpLog_ += promise::addEventListener(nullptr, QEvent::Drop, opLog("Drop"));
    eventHolderOpLog_ += promise::addEventListener(nullptr, QEvent::ContextMenu, opLog("ContextMenu"));
    eventHolderOpLog_ += promise::addEventListener(nullptr, QEvent::GrabMouse, opLog("GrabMouse"));
    eventHolderOpLog_ += promise::addEventListener(nullptr, QEvent::UngrabMouse, opLog("UngrabMouse"));
    eventHolderOpLog_ += promise::addEventListener(nullptr, QEvent::UngrabMouse, opLog("UngrabMouse"));
    eventHolderOpLog_ += promise::addEventListener(nullptr, QEvent::GrabKeyboard, opLog("GrabKeyboard"));
}

// 初始化状态栏
void MainWindow::Impl::setupStatusbar() {
    // 创建标签组件  

    QString name = "__left__";
    QLabel *label = new QLabel(thiz_->ui->statusbar);
    label->setObjectName(name);
    label->setTextFormat(Qt::RichText);
    //label->setStyleSheet("margin-right:5px;");
    thiz_->ui->statusbar->addWidget(label);

    QLabel *expand = new QLabel("", thiz_);
    expand->setObjectName("__expand__");
    thiz_->ui->statusbar->addWidget(expand, 1);

    
}


// 初始化toolbar
void MainWindow::Impl::setupMainToolbar() {
    auto moveLanguageTips = [](QAction *action) {
        action->setProperty("langInfo", action->whatsThis());
        action->setWhatsThis("");
    };

    moveLanguageTips(thiz_->ui->actionLanguageEnglish);
    moveLanguageTips(thiz_->ui->actionLanguageSimplifiedChinese);


    auto appendLoginButtons = [this](QToolBar *toolbar) {
        // 添加可伸缩的空间控件到工具条中
        QWidget *spacer = new QWidget(toolbar);
        spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        toolbar->insertWidget(toolbar->actions().back(), spacer);

        // 添加警告显示区域
        QLabel *warning = new QLabel(toolbar);
        warning->setObjectName("warning");
        warning->setText("");
        toolbar->insertWidget(toolbar->actions().back(), warning);
        promise::addEventListener(warning, QEvent::MouseButtonPress,
            [this, warning](QObject *, QEvent *event) {
            QMouseEvent *mevent = dynamic_cast<QMouseEvent *>(event);
            if (mevent && mevent->button() == Qt::LeftButton) {
                // 左键点击事件处理  
                QString text = tr("%1<br><br>"
                    "Above settings may cause low perforcement. To fix this,<br>"
                    "change the settings in production environment if possible,<br><br>"
                    "But it's recommented to <font color='red'>keep</font> the settings in development envirionment.")
                    .arg(warning->text());

                QMessageBox::warning(thiz_, tr("Warning"), text);
            }
            return false;
        });

        // 添加当前用户名显示区域
        QLabel *username = new QLabel(toolbar);
        username->setObjectName("username");
        username->setText(tr("Welcome: %1").arg("Username"));
        toolbar->insertWidget(toolbar->actions().back(), username);
        promise::addEventListener(username, QEvent::MouseButtonPress,
            [this](QObject *, QEvent *event) {
            QMouseEvent *mevent = dynamic_cast<QMouseEvent *>(event);
            if (mevent && mevent->button() == Qt::LeftButton) {
                // 左键点击事件处理
                emit thiz_->ui->actionUserLogin->trigger();
            }
            return false;
        });
    };

    appendLoginButtons(thiz_->ui->toolBarNormal);
    appendLoginButtons(thiz_->ui->toolBarRelease);

    updateLoginUser();
    this->eventHolderUser_ = User::getCurrentUser().onChanged(BindingData::kCustomEvent, [this](const std::string &eventValue) {
        if(eventValue == "user" || eventValue == "group")
            updateLoginUser();
    });

    AppConfig &appConfig = singleton<AppConfig>();
    this->eventHolderUser_ += appConfig.useSynchronizedLog_.onChanged([this] {
        this->updateLoginUser();
    });
    this->eventHolderUser_ += appConfig.debugHiMutex_.onChanged([this] {
        this->updateLoginUser();
    });
    this->eventHolderUser_ += appConfig.flowWaitGui_.onChanged([this] {
        this->updateLoginUser();
    });


}


void MainWindow::Impl::setupFlowEditors() {
    focusedFlowEditor_ = nullptr;
    //flowEditorBar_ = new QTabBar;
    //globalEditorBar_ = new QTabBar;
    flowEditorBar_ = thiz_->ui->flowEditorBar;
    globalEditorBar_ = thiz_->ui->globalEditorBar;

    flowEditorBar_->setFocusPolicy(Qt::StrongFocus);
    globalEditorBar_->setFocusPolicy(Qt::StrongFocus);

    // 加入流程QTabTbar
    flowEditorBar_->setDrawBase(false);
    //flowEditorBar_->setUsesScrollButtons(false);
    flowEditorBar_->addTab("");
    //flowEditorBar_->setStyleSheet("QTabBar::tab:first { min-width: 6px; max-width: 6px; width: 6px;}");
    //flowEditorBar_->setStyleSheet("QTabBar::tab { min-width:86px; } QTabBar::tab:last { min-width:0px; } QTabBar::tab:last:disabled { min-width:0px; border:0; color:transparent;} QTabBar::tab:first { min-width:1px; margin-top: 2px; max-width: 1px; width: 1px; border-bottom: 0px; border-top: 0px; border-left: 0px; border-right: 1px solid #d0d0d0;}");
    flowEditorBar_->addTab("+");
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    flowEditorBar_->setTabVisible(0, false);
#endif

    globalEditorBar_->setDrawBase(false);
    globalEditorBar_->setUsesScrollButtons(false);
    globalEditorBar_->addTab("");
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    globalEditorBar_->setTabVisible(0, false);
#endif

    //globalEditorBar_->setStyleSheet("QTabBar::tab:first { max-width: 0px; border: 0px }");
    //globalEditorBar_->setStyleSheet("QTabBar::tab:first { margin-top: 2px; max-width: 1px; width: 1px; border-bottom: 0px; border-top: 0px; border-left: 0px; border-right: 1px solid #d0d0d0;}");

    //QHBoxLayout *layout = dynamic_cast<QHBoxLayout *>(thiz_->ui->flowEditorTab->layout());
    //layout->addWidget(flowEditorBar_);
    //layout->insertSpacerItem(1, new QSpacerItem(0, 0, QSizePolicy::Expanding, QSizePolicy::Fixed));
    //layout->addWidget(globalEditorBar_);

#if 1
    focusedFlowEditors_.push_back(newFlowEditor(nullptr, flowEditorBar_, true, -1, "flowEditor", MainWindow::tr("Flow Editor %1").arg(1)));
    //flowEditors_.push_back(newFlowEditor(nullptr, flowEditorBar_, true, -1, "flowEditor", MainWindow::tr("Flow Editor %1").arg(2)));
    globalFlowEditor_ = newFlowEditor(nullptr, globalEditorBar_, false, 0, "globalFlowEditor", MainWindow::tr("Global Editor"));

    adjustQTabBarWidth(flowEditorBar_);
    flowEditorBar_->setCurrentIndex(1);
    globalEditorBar_->setCurrentIndex(1);
#endif

    MainWindow::connect(flowEditorBar_, &QTabBar::tabBarClicked, thiz_, [=](int index) {
        auto scene = this->projectFile_->getProject()->getFocusedScene();

        int focusedFlow = scene->focusedFlow_;
        if (index == flowEditorBar_->count() - 1) {
            // 创建新的流程
            thiz_->on_actionNewFlow_triggered();
            if (focusedFlow == scene->focusedFlow_) {
                // 如果focusedFlow没变，还是重新触发下变化事件吧
                scene->focusedFlow_.triggerEvent(BindingData::kEdit);
            }
        }
    });
    MainWindow::connect(flowEditorBar_, &QTabBar::currentChanged, thiz_, [=](int index) {
        if (disableNewFlowEditor_) return;

        if (index == flowEditorBar_->count() - 1) {
            // 改用响应点击事件产生新流程
            //thiz_->on_actionNewFlow_triggered();
        }
        else if (index > 0 && index < flowEditorBar_->count() - 1) {
            auto scene = this->projectFile_->getProject()->getFocusedScene();
            scene->focusedFlow_ = index - 1;
        }
    });
    MainWindow::connect(globalEditorBar_, &QTabBar::currentChanged, thiz_, [=](int index) {
        if (index == 0) {
            auto scene = this->projectFile_->getProject()->getFocusedScene();
            scene->focusedFlow_ = -1;
        }
    });

    flowEditorBar_->setContextMenuPolicy(Qt::CustomContextMenu);
    MainWindow::connect(flowEditorBar_, &QWidget::customContextMenuRequested, [=](const QPoint &point) {
        if (point.isNull())
            return;

        int index = flowEditorBar_->tabAt(point) - 1;
        if (index >= 0) {
            auto flow = this->projectFile_->getProject()->getFlowByIndex(index);
            if (flow) {
                const QPoint &pos = flowEditorBar_->mapToGlobal(point);
                FlowEditor::showContextMenu(thiz_, pos, flow);
            }
        }
    });

    globalEditorBar_->setContextMenuPolicy(Qt::CustomContextMenu);
    MainWindow::connect(globalEditorBar_, &QWidget::customContextMenuRequested, [=](const QPoint &point) {
        if (point.isNull())
            return;

        int index = globalEditorBar_->tabAt(point);
        auto scene = this->projectFile_->getProject()->getFocusedScene();
        LOG_INFO("index = {}", index);
        if (index == 0) {
            const QPoint &pos = globalEditorBar_->mapToGlobal(point);
            FlowEditor::showContextMenu(thiz_, pos, scene->globalFlow_);
        }
    });
}

// 初始化图像托盘容器
void MainWindow::Impl::setupImageFrames() {
    this->focusedImageFrames_.clear();
    this->sharedImageFrames_.clear();
    this->globalImageFrame_ = thiz_->ui->globalImageFrame;
}


static void removeMenu(QMenu *menu) {
    auto actions = menu->actions();
    for (QAction *action : actions) {
        QMenu *subMenu = action->menu();
        if (subMenu) {
            removeMenu(subMenu);
            delete subMenu;
        }
        menu->removeAction(action);
        action->deleteLater();
    }
};

// 初始化HMI项目菜单  
void MainWindow::Impl::setupHmiMenu() {
    QMenu *menu = thiz_->ui->actionHmiRunner->menu();
    if (menu == nullptr) {
        menu = new QMenu(thiz_);

        thiz_->ui->actionHmiRunner->setMenu(menu);

        QToolButton *toolButton = dynamic_cast<QToolButton *>(thiz_->ui->toolBarNormal->widgetForAction(thiz_->ui->actionHmiRunner));
        if (toolButton) toolButton->setPopupMode(QToolButton::InstantPopup);

        toolButton = dynamic_cast<QToolButton *>(thiz_->ui->toolBarRelease->widgetForAction(thiz_->ui->actionHmiRunner));
        if (toolButton) toolButton->setPopupMode(QToolButton::InstantPopup);
    }

    // 更新HMI项目菜单  
    auto isUpdating_L1 = std::make_shared<bool>(false);
    auto updateHmiMenuAsync_L1 = [isUpdating_L1, menu, this]() -> co_async_t {
        if (*isUpdating_L1) co_return;
        *isUpdating_L1 = true;
        OnScopeExit onScopeExit([=] {
            *isUpdating_L1 = false;
        });

        removeMenu(menu);

        auto createWidget = [](const std::shared_ptr<HmiRunBoard> &hmiRunBoard, QMenu *menu) -> co_async<QWidget *> {
            if (hmiRunBoard->integrationMode_ == "shrink-as-level1-menu"
                || hmiRunBoard->integrationMode_ == "shrink-as-level2-menu"
                || hmiRunBoard->integrationMode_ == "run-as-level1-menu"
                || hmiRunBoard->integrationMode_ == "run-as-level2-menu") {

                QWidget *widget = newHmiWidget(hmiRunBoard, menu);
                if(!widget) co_return nullptr;
                UiRunStack *uiRunStack = dynamic_cast<UiRunStack *>(widget);
                if (!uiRunStack) {
                    widget->deleteLater();
                    co_return nullptr;
                }

                std::shared_ptr<UiRunBoard> uiRunBoard = uiRunStack->find(hmiRunBoard);
                std::weak_ptr<UiRunBoard> wUiRunBoard = uiRunBoard;

                co_await co_delay_ms(0);

                if (hmiRunBoard->integrationMode_ == "shrink-as-level1-menu"
                    || hmiRunBoard->integrationMode_ == "shrink-as-level2-menu") {
                    QRect rect = uiRunBoard->getShrinkedRect();

                    LOG_INFO("rect = {}", rect);
                    uiRunBoard->move(-rect.topLeft());
                    QSize size = rect.size();
                    uiRunStack->setMinimumSize(size);
                    uiRunStack->resize(size);
                }
                else {
                    QSize size = uiRunStack->size();
                    uiRunStack->setMinimumSize(size);
                }
                co_return widget;
            }
            co_return nullptr;
        };

        auto insertHmiMenu = [](const std::shared_ptr<HmiRunBoard> &hmiRunBoard,
            QMenu *menu,
            const std::map<std::shared_ptr<HmiRunBoard>, QWidget *> &widgetsMap) {

            auto found = widgetsMap.find(hmiRunBoard);
            if (found == widgetsMap.end()) return;
            QWidget *widget = found->second;

            if (widget) {
                QWidgetAction *widgetAction = new QWidgetAction(menu);
                //widget->QObject::setParent(widgetAction);
                widgetAction->setDefaultWidget(widget);
                //widgetAction->setIcon(QIcon(QPixmap(":/icons/visible.png")));
                menu->addAction(widgetAction);

                auto widgetActionAlive = std::make_shared<bool>(true);
                QObject::connect(widgetAction, &QObject::destroyed, [widgetActionAlive] {
                    *widgetActionAlive = false;
                    LOG_INFO("widgetAction destroyed");
                });

                QObject::connect(widget, &QObject::destroyed, [menu, widget, widgetAction, widgetActionAlive] {
                    LOG_INFO("hmi widget destroyed");
                    qSetTimeout(LOC, [=](const Loc &) {
                        if (*widgetActionAlive) {
                            menu->removeAction(widgetAction);
                        }
                    }, 0);
                });
            }
        };

        // 获取当前的界面列表  
        std::shared_ptr<HmiList> hmiList = getHmiList(Project::current());
        if (hmiList) {
            // 创建对象  
            std::map<std::shared_ptr<HmiRunBoard>, QWidget *> widgetsMap;
            for (const std::shared_ptr<HmiRunBoard> &hmiRunBoard : hmiList->hmiRunBoards_) {
                if (hmiRunBoard->integrationMode_ == "run-as-level1-menu"
                    || hmiRunBoard->integrationMode_ == "shrink-as-level1-menu"
                    ) {
                    QWidget *widget = co_await createWidget(hmiRunBoard, menu);
                    if(widget) widgetsMap.emplace(hmiRunBoard, widget);
                }
            }

            for (const std::shared_ptr<HmiRunBoard> &hmiRunBoard : hmiList->hmiRunBoards_) {
                if (hmiRunBoard->integrationMode_ == "run-as-level1-menu"
                    || hmiRunBoard->integrationMode_ == "shrink-as-level1-menu") {
                    // 插入一级菜单  
                    insertHmiMenu(hmiRunBoard, menu, widgetsMap);
                }
                else if (hmiRunBoard->integrationMode_ == "run-as-level2-menu"
                    || hmiRunBoard->integrationMode_ == "shrink-as-level2-menu") {
                    // 插入二级菜单  
                    std::string winTitle = hmiRunBoard->winTitle_;
                    QAction *hmiItem = menu->addAction(QString::fromStdString(winTitle));
                    QMenu *subMenu = new QMenu();
                    hmiItem->setMenu(subMenu);

                    auto isUpdating_L2 = std::make_shared<bool>(false);
                    auto isDestoryed_L2 = std::make_shared<bool>(false);
                    auto updateHmiMenuAsync_L2 = [=]() -> co_async_t {
                        if (*isUpdating_L2) co_return;
                        *isUpdating_L2 = true;
                        OnScopeExit onScopeExit([=] {
                            *isUpdating_L2 = false;
                        });

                        removeMenu(subMenu);
                        if(*isDestoryed_L2) co_return;
                        QWidget *widget = co_await createWidget(hmiRunBoard, subMenu);
                        if (*isDestoryed_L2) co_return;
                        if (widget) {
                            std::map<std::shared_ptr<HmiRunBoard>, QWidget *> widgetsMap;
                            widgetsMap.emplace(hmiRunBoard, widget);
                            insertHmiMenu(hmiRunBoard, subMenu , widgetsMap);
                        }
                    };
                    
                    auto updateHmiMenu_L2 = [=] {
                        co_call(updateHmiMenuAsync_L2);
                    };

                    QMenu::connect(subMenu, &QMenu::aboutToShow, updateHmiMenu_L2);
                    QMenu::connect(subMenu, &QMenu::destroyed, [=] {
                        PTI;
                        *isDestoryed_L2 = true;
                    });
                }
                else if (hmiRunBoard->integrationMode_ == "run-as-docker"
                    || hmiRunBoard->integrationMode_ == "run-as-window"
                    || hmiRunBoard->integrationMode_ == "run-as-dialog"
                    || hmiRunBoard->integrationMode_ == "run-as-root-window"
                    ) {
                    std::string winTitle = hmiRunBoard->winTitle_;
                    QAction *hmiItem = menu->addAction(QString::fromStdString(winTitle));

                    std::weak_ptr<HmiRunBoard> wHmiRunBoard = hmiRunBoard;
                    QObject::connect(hmiItem, &QAction::triggered, [winTitle, this, wHmiRunBoard]() {
                        auto hmiRunBoard = wHmiRunBoard.lock();
                        if (hmiRunBoard) {
                            showHmiWindow(hmiRunBoard->clone(), hmiRunBoard, thiz_);
                        }
                    });
                }
            }
        }

        if (menu->actions().isEmpty()) {
            QAction *hmiItem = menu->addAction(tr("No ui designed"));
            QObject::connect(hmiItem, &QAction::triggered, [this]() {
                QMessageBox::warning(thiz_, tr("Warning"), tr("Add hmi by ui designer first!"));
            });
        }
    };

    auto updateHmiMenu_L1 = [=] {
        co_call(updateHmiMenuAsync_L1);
    };

    QMenu::connect(menu, &QMenu::aboutToShow, updateHmiMenu_L1);
    QMenu::connect(menu, &QMenu::aboutToHide, [=] {
        //removeMenu(menu);
    });
}

void MainWindow::Impl::setupAuthItems() {
    AuthItem::bindToObject("save_project", thiz_->ui->actionSave, AuthItem::kValid);
    AuthItem::bindToObject("edit_flow", thiz_->ui->actionNewScene, AuthItem::kValid);
    AuthItem::bindToObject("edit_flow", thiz_->ui->actionNewFlow, AuthItem::kValid);
    AuthItem::bindToObject("edit_flow", thiz_->ui->actionNewImagePanel, AuthItem::kValid);

    AuthItem::bindToObject("edit_flow", thiz_->ui->actionUiDesigner, AuthItem::kValid);

    AuthItem::bindToObject("run_stop_project", thiz_->ui->actionUiRunner, AuthItem::kValid);
    AuthItem::bindToObject("run_stop_project", thiz_->ui->actionRunFlowLoop, AuthItem::kValid);
    AuthItem::bindToObject("run_stop_project", thiz_->ui->actionRunFlowOnce, AuthItem::kValid);
    AuthItem::bindToObject("step_project", thiz_->ui->actionRunFlowStep, AuthItem::kShow);
    AuthItem::bindToObject("run_stop_project", thiz_->ui->actionPauseFlow, AuthItem::kValid);
    AuthItem::bindToObject("run_stop_project", thiz_->ui->actionStopFlowTask, AuthItem::kValid);

    AuthItem::bindToObject("show_var_browser", thiz_->ui->actionVariableBrowser, AuthItem::kValid);
    AuthItem::bindToObject("show_alarm_browser", thiz_->ui->actionAlarmHistory, AuthItem::kValid);
    AuthItem::bindToObject("project_parameter", thiz_->ui->actionProjectParameters, AuthItem::kValid);

    AuthItem::bindToObject("edit_flow", thiz_->ui->actionProjectUpgradeConfigurations, AuthItem::kValid);
    AuthItem::bindToObject("edit_flow", thiz_->ui->actionProjectUpgrade, AuthItem::kValid);
    AuthItem::bindToObject("edit_flow", thiz_->ui->actionAlarmTriggers, AuthItem::kValid);

    AuthItem::bindToObject("user_management", thiz_->ui->actionUserGroup, AuthItem::kShow);
    AuthItem::bindToObject("user_management", thiz_->ui->actionPrivilege, AuthItem::kShow);
}

// 运行启动项目  
void MainWindow::Impl::setupStartupProject(__qsync__) {
    AppConfig &appConfig = Singleton<AppConfig>::getInstance();
    const CmdArg &cmdArg = singleton<CmdArg>();

    // 启动项目信息  
    FlowRunner::RunCmd runCmd;
    int loopCount = -1;
    QString startupProject;
    std::string startupSceneName;
    std::vector<std::string> startupFlowNames;
    std::optional<int> recipeId;
    getStartupInfo(runCmd, loopCount, startupProject, startupSceneName, startupFlowNames, recipeId);

    // 默认项目
    this->newProject(__qwait__);
    qSetTimeout(LOC, [=](const Loc &loc) {
        loc.trace();
        thiz_->on_actionResetDefaultLayout_triggered();
    }, 0).wait(__qwait__);

    // 启动项目  
    if (!startupProject.isEmpty()) {
        RunMonitor &runMonitor = singleton<RunMonitor>();
        runMonitor.enabled_ = true;
        runMonitor.sceneName_ = startupSceneName;
        runMonitor.flowName_ = cmdArg.loopFlowName_;
        runMonitor.loopCount_ = loopCount;
        runMonitor.exitAfterLoop_ = cmdArg.exitAfterLoop_;

        this->loadLatestProject(
            startupProject,
            startupSceneName,
            startupFlowNames,
            recipeId,
            runCmd);
    }

}

void MainWindow::Impl::setupTimePlan() {

    // 是否需要清零  
    QTime now = QTime::currentTime();
    if (lastTimePlannerTime_.has_value()
        && TimeResetPlan::isUpToResetTime(lastTimePlannerTime_.value(), now)) {
        // 时间监视器清零  
        auto project = Project::current();
        if (project) {
            auto timeInspectorDefs = hi_copy_locked(project->timeInspectorDefs_);
            for (auto &def : timeInspectorDefs) {
                project->timeInspector_.resetStat(def);
            }
        }
    }
    lastTimePlannerTime_ = now;
    qSetTimeout(LOC, thiz_, [=](const Loc &) {
        this->setupTimePlan();
    }, 1000 * 60);
}


// 获取启动项目所需的dll
std::set<std::string> MainWindow::Impl::getStartupProjectDlls() {
    AppConfig &appConfig = Singleton<AppConfig>::getInstance();
    if (!appConfig.loadRequiredDllOnly_)
        return {};  // 加载所有dll   

    // 启动项目信息  
    FlowRunner::RunCmd runCmd;
    int loopCount = -1;
    QString startupProject;
    std::string startupSceneName;
    std::vector<std::string> startupFlowNames;
    std::optional<int> recipeId;
    getStartupInfo(runCmd, loopCount, startupProject, startupSceneName, startupFlowNames, recipeId);

    if (runCmd != FlowRunner::RunCmd::kStop) {
        return ProjectFile::getPluginDlls(startupProject);

    }

    return {};
}

// 获取启动项目  
void MainWindow::Impl::getStartupInfo(
    FlowRunner::RunCmd &runCmd,
    int &loopCount,
    QString &startupProject,
    std::string &startupSceneName,
    std::vector<std::string> &startupFlowNames,
    std::optional<int>& recipeId) {
    AppConfig &appConfig = Singleton<AppConfig>::getInstance();
    QString latestProjectPath = QString::fromStdString(appConfig.latestProject_);

    // 命令行启动项目  
    const CmdArg &cmdArg = singleton<CmdArg>();
    if (!cmdArg.startupProject_.empty()) {
        startupProject = QString::fromStdString(cmdArg.startupProject_);
        startupSceneName = cmdArg.startupSceneName_;
        startupFlowNames = cmdArg.startupFlowNames_;
    }
    // 最近项目
    else if (!latestProjectPath.isEmpty()) {
        startupProject = latestProjectPath;
        recipeId = appConfig.latestRecipeId_;
    }

    runCmd = FlowRunner::RunCmd::kStop;
    loopCount = -1;
    if (!startupProject.isEmpty()) {
        if (cmdArg.loopCount_.has_value()) {
            loopCount = cmdArg.loopCount_.value();
            if (loopCount < 0)
                runCmd = FlowRunner::RunCmd::kRunLooply;
            else if (loopCount == 0)
                runCmd = FlowRunner::RunCmd::kStop;
            else if (loopCount == 1)
                runCmd = FlowRunner::RunCmd::kRunOneShot;
            else
                runCmd = FlowRunner::RunCmd::kRunLooply;
        }
        else if (!cmdArg.startupProject_.empty()) {
            runCmd = FlowRunner::RunCmd::kStop;
        }
        else if (appConfig.latestProjectLoadMode_ == "load_oneshot")
            runCmd = FlowRunner::RunCmd::kRunOneShot;
        else if (appConfig.latestProjectLoadMode_ == "load_looply")
            runCmd = FlowRunner::RunCmd::kRunLooply;
        else
            startupProject = "";
    }
}


void MainWindow::Impl::loadPluginDlls(const std::set<std::string> &dlls) {
    // 抑制dll加载错误对话框  
    AppConfig &appConfig = Singleton<AppConfig>::getInstance();
    bool suppressDllLoadingError = appConfig.suppressDllLoadingError_;
    if (suppressDllLoadingError)
        this->antiMsgBoxStart();

    // 加载dll 
    // 设定仅加载需要的dll  
    JSRttrStub::invalidCache();
    TlModule::setDllModuleFilter(dlls);
    thiz_->tlModule_ = jsInitEngine(&this->jsEngine_);

    // 停止抑制dll加载错误对话框  
    if (suppressDllLoadingError)
        this->antiMsgBoxStop();

    this->checkModuleVersions();
}

// 更新登录的用户信息  
void MainWindow::Impl::updateLoginUser() {
    UserGroup userRuntimeGroup = User::getCurrentRuntimeUserGroup();
    UserGroup userGroup;
    std::string username;

    const CurrentUser &currentUser = User::getCurrentUser();
    {
        auto lock = hi_shared_lock(currentUser);
        username = currentUser.user_.displayName();
        userGroup = currentUser.user_.group_;
    }

    auto updateLabel = [&](QToolBar *toolBar) {
        // 在用户名左侧加上性能提示  
        AppConfig &appConfig = singleton<AppConfig>();
        if (appConfig.useSynchronizedLog_
            || appConfig.debugHiMutex_
            || appConfig.flowWaitGui_) {
            QString performanceWarning;
            QStringList as;
            if (appConfig.useSynchronizedLog_) as.push_back(tr("sync log"));
            if (appConfig.debugHiMutex_) as.push_back(tr("dead lock detection"));
            if (appConfig.flowWaitGui_) as.push_back(tr("flow wait gui"));
            
            performanceWarning = tr("<b><font color='%1'>Warning: %2</font></b> ")
                .arg(appConfig.inDevEnv_ ? "gray" : "red")
                .arg(as.join(", "));
            QLabel *lWarning = toolBar->findChild<QLabel *>("warning");
            if (lWarning != nullptr) {
                lWarning->setText(performanceWarning);
            }
        }

        QLabel *lUsername = toolBar->findChild<QLabel *>("username");
        if (lUsername != nullptr) {
            if (username.empty()) {
                lUsername->setText("<font>" + tr("Not login") + "</font>");
            }
            else if (userGroup != userRuntimeGroup) {
                lUsername->setText("<font>" + tr("Welcome: %1, (%2 as %3)")
                    .arg(QString::fromStdString(username))
                    .arg(QString::fromStdString(User::groupToName(userGroup).second))
                    .arg(QString::fromStdString(User::groupToName(userRuntimeGroup).second))
                    + "</font>");
            }
            else {
                lUsername->setText("<font>" + tr("Welcome: %1 (%2)")
                    .arg(QString::fromStdString(username))
                    .arg(QString::fromStdString(User::groupToName(userRuntimeGroup).second))
                    + "</font>");
            }
        }
    };

    updateLabel(thiz_->ui->toolBarNormal);
    updateLabel(thiz_->ui->toolBarRelease);

}

// 更新最近项目菜单
void MainWindow::Impl::updateRecentProjectMenu() {
    QMenu *menu = thiz_->ui->actionRecentProject->menu();
    if (menu == nullptr) {
        menu = new QMenu(thiz_);
        thiz_->ui->actionRecentProject->setMenu(menu);
    }

    auto actions = menu->actions();
    for (QAction *action : actions) {
        menu->removeAction(action);
        action->deleteLater();
    }

    AppConfig &appConfig = Singleton<AppConfig>::getInstance();
    for (const std::string &path : appConfig.recentProjects_) {
        QString nativePath = QDir::toNativeSeparators(QString::fromStdString(path));
        QAction *actionRecentItem = menu->addAction(nativePath);
        QObject::connect(actionRecentItem, &QAction::triggered, [path, this]() {
            __qsync__;
            LOG_INFO("Recent path = {}", path);
            this->openProject(__qwait__, QString::fromStdString(path), true);
        });
    }

}

// 更新项目档案
void MainWindow::Impl::updateProjectArchive() {
    if (projectArchive_ == nullptr) {
        projectArchive_ = new ProjectArchiveDialog(thiz_);
    }

    dynamic_cast<ProjectArchiveDialog*>(projectArchive_)->updateRecentProjects();
}

// 更新发布模式状态
void MainWindow::Impl::updateReleaseMode() {
    this->inUpdateReleaseMode_ = true;
    OnScopeExit onScopeExit([&]{
        this->inUpdateReleaseMode_ = false;
    });

    bool isReleaseMode = this->projectFile_->getProject()->isReleaseMode_;
    bool isEmuReleaseMode = this->projectFile_->getProject()->isEmuReleaseMode_;

    QWidget *normalWidgets[] = {
        thiz_->ui->toolBarNormal,
        thiz_->ui->menuProject,
        thiz_->ui->menuAction,
        thiz_->ui->menuView,
        thiz_->ui->menuDevice,
        thiz_->ui->menuSetting,
    };
    QWidget *releaseWidgets[] = {
        thiz_->ui->toolBarRelease,
        thiz_->ui->menuProject2,
        thiz_->ui->menuAction2,
        thiz_->ui->menuView2,
        thiz_->ui->menuDevice2,
        thiz_->ui->menuSetting2,
    };

    //QWidget *onlyNormalWidgets[] = {
        //thiz_->ui->dockWidget_widgets,
    //};

    auto hide = [](QWidget *widget) {
        QMenu *menu = dynamic_cast<QMenu *>(widget);
        if (menu != nullptr) {
            if (menu->menuAction()->isVisible())
                menu->menuAction()->setVisible(false);
            return;
        }

        ads::CDockWidget *dockWidget = dynamic_cast<ads::CDockWidget *>(widget);
        if (dockWidget != nullptr) {
            dockWidget->toggleView(false);
            return;
        }

        if (!widget->isHidden())
            widget->hide();

    };
    auto show = [](QWidget *widget) {
        QMenu *menu = dynamic_cast<QMenu *>(widget);
        if (menu != nullptr) {
            if (!menu->menuAction()->isVisible())
                menu->menuAction()->setVisible(true);
            return;
        }

        ads::CDockWidget *dockWidget = dynamic_cast<ads::CDockWidget *>(widget);
        if (dockWidget != nullptr) {
            dockWidget->toggleView(true);
            return;
        }
        
        if (widget->isHidden())
            widget->show();
    };

    if (isEmuReleaseMode) {
        thiz_->ui->actionSwitchToReleaseMode->setToolTip(tr("Restore from release mode"));
    }
    else {
        thiz_->ui->actionSwitchToReleaseMode->setToolTip(tr("Switch to release mode"));
    }

    if (isReleaseMode || isEmuReleaseMode) {
        for (QWidget *widget : normalWidgets)
            hide(widget);
        for (QWidget *widget : releaseWidgets)
            show(widget);
        //for (QWidget *widget : onlyNormalWidgets)
        //    hide(widget);
        hide(this->dockWidget_widgets_);
        hide(this->dockWidget_flowEditor_);

        if (isReleaseMode) {
            thiz_->ui->actionSwitchToReleaseMode->setVisible(false);
        }
        else {
            thiz_->ui->actionSwitchToReleaseMode->setVisible(true);
        }

#if 0
        QList<int> sizes = thiz_->ui->splitter->sizes();
        const QSize &size = thiz_->ui->splitter->size();
        LOG_INFO("sizes[{}] = {}, {}, {}, spliiter = {}", sizes.size(), sizes[0], sizes[1], sizes[2], size);
        if (sizes.size() > 0 && sizes[0] > 0) {
            if (sizes.size() >= 3) {
                sizes[1] += sizes[0];
                sizes[0] = 0;
                thiz_->ui->splitter->setSizes(sizes);
            }
            else {
                QList<int> sizes{ 0, (int)(100*(1+ DEFAULT_SPLITTER_1)), (int)(100 * DEFAULT_SPLITTER_2) };
                thiz_->ui->splitter->setSizes(sizes);
            }
        }
#endif

        int addTab = (this->flowEditorBar_->count() - 1);
        if(this->flowEditorBar_->isTabEnabled(addTab)) {
            this->flowEditorBar_->setTabEnabled(addTab, false);
            //adjustQTabBarWidth(flowEditorBar_);
        }
    }
    else {
        for (QWidget *widget : releaseWidgets)
            hide(widget);
        for (QWidget *widget : normalWidgets)
            show(widget);
        //for (QWidget *widget : onlyNormalWidgets)
        //    show(widget);
        
        if(this->projectDockWidgetsVisible_)
            show(this->dockWidget_widgets_);
        else
            hide(this->dockWidget_widgets_);

        if (this->projectDockFlowEditorVisible_)
            show(this->dockWidget_flowEditor_);
        else
            hide(this->dockWidget_flowEditor_);

        thiz_->ui->actionSwitchToReleaseMode->setVisible(true);

#if 0
        QList<int> sizes = thiz_->ui->splitter->sizes();
        const QSize &size = thiz_->ui->splitter->size();
        LOG_INFO("sizes[{}] = {}, {}, {}, spliiter = {}", sizes.size(), sizes[0], sizes[1], sizes[2], size);
        if (sizes.size() > 0 && sizes[0] == 0
            && projectSplitSizes_.size() > 0 && projectSplitSizes_[0] > 0) {
            if (sizes.size() >= 3) {
                if (sizes[1] > 0) {
                    sizes[0] = (int)(sizes[1] / (1 + DEFAULT_SPLITTER_1));
                    sizes[1] -= sizes[0];
                }
                else {
                    sizes[0] = 100;
                }
                thiz_->ui->splitter->setSizes(sizes);
            }
            else {
                QList<int> sizes{ 100, (int)(100 * DEFAULT_SPLITTER_1), (int)(100 * DEFAULT_SPLITTER_2) };
                thiz_->ui->splitter->setSizes(sizes);
            }
        }
#endif

        int addTab = (this->flowEditorBar_->count() - 1);
        if (!this->flowEditorBar_->isTabEnabled(addTab)) {
            if (!disableNewFlowEditor_) {
                disableNewFlowEditor_ = true;
                OnScopeExit scoped([&] { disableNewFlowEditor_ = false; });
                this->flowEditorBar_->setTabEnabled(addTab, true);
            }
            else {
                this->flowEditorBar_->setTabEnabled(addTab, true);
            }
            this->setFocusFlowEditor(this->focusedFlowEditor_);
            adjustQTabBarWidth(flowEditorBar_);
        }
    }
    //adjustQTabBarWidth(flowEditorBar_);
}

// 更新主工具条
void MainWindow::Impl::updateMainToolbar() {
    bool isAllRunning = this->projectFile_->isAllRunning();
    bool isAllStopped = this->projectFile_->isAllStopped();
    LOG_DEBUG("isAllStopped = {}", isAllStopped);
    
#if 0
    std::vector<FlowRunner::RunMode> runModes = this->projectFile_->getRunModes();
    bool hasRunLooply = false;
    for (FlowRunner::RunMode runMode : runModes) {
        LOG_DEBUG("runMode = {}", enumStr(runMode));
        if (runMode == FlowRunner::RunMode::kNormalLooply)
            hasRunLooply = true;
    }
#endif
    
    const std::shared_ptr<Project> &project = this->projectFile_->getProject();
    const std::shared_ptr<Scene> &focusedScene = !project ? nullptr : project->getFocusedScene();
    const std::shared_ptr<Scene> &sharedScene = !project ? nullptr : project->getSharedScene();

    bool hasCanRunLooply = false;
    bool hasCanRunOneShot = false;
    bool hasRunLooply = false;
    bool hasRunOneShot = false;
    bool hasRefreshUi = false;
    //LOG_DEBUG("scene = {}", scene);
    std::vector<std::shared_ptr<Flow>> flows;
    if (focusedScene) {
        flows.push_back(focusedScene->globalFlow_);
        for (const std::shared_ptr<Flow> flow : focusedScene->flows_)
            flows.push_back(flow);
    }
    if (sharedScene) {
        //flows.push_back(sharedScene->globalFlow_); // sharedScene没有全局流程
        for (const std::shared_ptr<Flow> flow : sharedScene->flows_)
            flows.push_back(flow);
    }

    for (const std::shared_ptr<Flow> flow : flows) {
        FlowRunner::RunMode runMode = flow->getRunMode();
        LOG_DEBUG("flow runMode = {}", runMode);
        bool hasTask = (flow->tasks_.size() > 0);
        if (runMode != FlowRunner::RunMode::kNormalLooply) {
            if (flow->isDefaultRunning_ && hasTask) {
                hasCanRunLooply = true;
            }
        }
        else {
            hasRunLooply = true;
        }

        if (runMode == FlowRunner::RunMode::kNormalOneShot) {
            hasRunOneShot = true;
        }

        if (runMode == FlowRunner::RunMode::kRefreshUi) {
            hasRefreshUi = true;
        }

        if (runMode != FlowRunner::RunMode::kNormalOneShot
            && runMode != FlowRunner::RunMode::kNormalLooply) {
            if (flow->isDefaultRunning_ && hasTask) {
                hasCanRunOneShot = true;
            }
        }
    }

    LOG_INFO("isAllStopped = {}, hasCanRunLooply = {}, hasCanRunOneShot = {}",
        isAllStopped, hasCanRunLooply,
        hasCanRunOneShot);


    qSetEnabled(thiz_->ui->actionNewProject, "run_state", isAllStopped);
    qSetEnabled(thiz_->ui->actionOpen, "run_state", isAllStopped);
    qSetEnabled(thiz_->ui->actionArchive, "run_state", isAllStopped);
    //qSetEnabled(thiz_->ui->actionSave, "run_state", isAllStopped);
    //qSetEnabled(thiz_->ui->actionSaveAs, "run_state", isAllStopped);
    qSetEnabled(thiz_->ui->actionRecentProject, "run_state", isAllStopped);
    qSetEnabled(thiz_->ui->actionNewScene, "run_state", isAllStopped);
    qSetEnabled(thiz_->ui->actionNewFlow, "run_state", isAllStopped);
    qSetEnabled(thiz_->ui->actionNewImagePanel, "run_state", isAllStopped);
    qSetEnabled(thiz_->ui->actionRunFlowLoop, "run_state", hasCanRunLooply && !hasRefreshUi);
    qSetEnabled(thiz_->ui->actionRunFlowOnce, "run_state", hasCanRunOneShot && !hasRefreshUi);
    qSetEnabled(thiz_->ui->actionPauseFlow, "run_state", hasRunLooply || hasRunOneShot);
    qSetEnabled(thiz_->ui->actionStopFlowTask, "run_state", !isAllStopped);
    qSetEnabled(thiz_->ui->actionReleaseAs, "run_state", isAllStopped);
    qSetEnabled(thiz_->ui->actionSwitchToReleaseMode, "run_state", isAllStopped);
}

// 更新项目流程树
void MainWindow::Impl::updateFlowTree() {
    thiz_->ui->flowTree->clear();
    flowTreeMaps_.clear();
    taskTreeMaps_.clear();
    const auto &project = this->projectFile_->getProject();
    const auto &scenes = project->scenes_;
    const auto &sharedScene = project->getSharedScene();

    // 创建一个带图标功能的标签
    auto createIconLabel = []() -> QWidget * {
        QWidget *widget = new QWidget;
        QHBoxLayout *layout = new QHBoxLayout();
        layout->setContentsMargins(0, 0, 0, 0);
        widget->setLayout(layout);

        QLabel *text = new QLabel(widget);
        text->setObjectName("text");
        text->setTextFormat(Qt::TextFormat::RichText);
        text->setScaledContents(true);
        text->setText("");
        layout->addWidget(text);

        QLabel *icon = new QLabel(widget);
        icon->setObjectName("icon");
        icon->setScaledContents(true);
        icon->setFixedSize(12, 12);
        icon->setPixmap(QPixmap());
        layout->addWidget(icon);

        QWidget *expanding = new QWidget(widget);
        expanding->setSizePolicy(QSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred));
        layout->addWidget(expanding);

        return widget;
    };

    // 加入配方
    auto appendScene = [this, createIconLabel](
        const std::shared_ptr<Scene> &scene,
        bool isFocusedScene,
        bool isSharedScene) {
        LOG_DEBUG("isFocusedScene = {}, isSharedScene = {}", isFocusedScene, isSharedScene);

        // 非当前场景，灰色显示
        auto makeFocused = [isFocusedScene](const QString &label) {
            if (isFocusedScene)
                return label;
            else
                return QString("<font color='#888'>%1</font>").arg(label);
        };

        QTreeWidgetItem *treeScene = new QTreeWidgetItem(thiz_->ui->flowTree);
        treeScene->setData(0, Qt::UserRole, QVariant::fromValue(std::weak_ptr<Scene>(scene)));
        thiz_->ui->flowTree->addTopLevelItem(treeScene);

        QString sceneName;
        if (isSharedScene)
            sceneName = tr("Shared Flows");
        else if (scene->name_->empty())
            sceneName = tr("Untitled scene");
        else
            sceneName = QString::fromStdString(scene->name_);

        QLabel *text = new QLabel;
        text->setTextFormat(Qt::TextFormat::RichText);
        text->setText(makeFocused(sceneName));
        thiz_->ui->flowTree->setItemWidget(treeScene, 0, text);

        VList<std::shared_ptr<Flow>> flows = hi_copy_locked(scene->flows_);
        if (!isSharedScene) {
            flows.push_back(scene->globalFlow_);
        }

        for (auto &flow : flows) {

            //for (const std::shared_ptr<Task> &task : flow->tasks_) {
            //    LOG_WARN("task = {}, prev = {}, next = {}", task->widget_->customName(), task->prevTasks_.size(), task->nextTasks_.size());
            //}


            QTreeWidgetItem *treeFlow = new QTreeWidgetItem();
            flowTreeMaps_.insert({ flow, treeFlow });
            treeFlow->setData(0, Qt::UserRole, QVariant::fromValue(std::weak_ptr<Flow>(flow)));

            bool isDefaultRunning = (flow->isDefaultRunning_ || flow == scene->globalFlow_);
            QPixmap pixmap(isDefaultRunning ? ":/icons/favorite.png" : ":/icons/favorite_blur.png");
            if (!isFocusedScene) {
                pixmap = qGrayscale(pixmap);
            }
            QIcon defaultRunningIcon(pixmap);
            treeFlow->setIcon(0, defaultRunningIcon);

            treeScene->addChild(treeFlow);

            QString flowName = (flow->name_->empty() ? MainWindow::tr("Untitled flow") : QString::fromStdString(flow->name_));
            QWidget *flowWidget = createIconLabel();
            flowWidget->findChild<QLabel *>("text")->setText(makeFocused(flowName));
            flowWidget->findChild<QLabel *>("icon")->hide();
            thiz_->ui->flowTree->setItemWidget(treeFlow, 0, flowWidget);

            VList<std::shared_ptr<Task>> tasks = hi_copy_locked(flow->tasks_);
            for (auto &task : tasks) {
                QTreeWidgetItem *treeTask = new QTreeWidgetItem();
                taskTreeMaps_.insert({ task, treeTask });
                treeTask->setData(0, Qt::UserRole, QVariant::fromValue(std::weak_ptr<Task>(task)));

                auto widget = task->widget_;
                if (widget) {
                    QString logo = TlModule::getModuleFile(QString::fromStdString(widget->widgetType_.modulePath_), QString::fromStdString(widget->logo_));

                    QPixmap pixmap = QPixmap(logo);
                    if (!isFocusedScene) {
                        pixmap = qGrayscale(pixmap);
                    }
                    treeTask->setIcon(0, QIcon(pixmap));
                }

                QString taskLabel = (!widget ? MainWindow::tr("Untitled task") : QString::fromStdString(widget->customName()));
                QString comment = (!widget ? "" : QString::fromStdString(hi_copy_locked(widget->comment_)));
                LOG_INFO("taskName = {}", taskLabel);
                if (!comment.isEmpty()) {
                    taskLabel += QString(" <font color='#aaa'>%1</font>").arg(comment);
                }

                QWidget *taskWidget = createIconLabel();
                taskWidget->findChild<QLabel *>("text")->setText(makeFocused(taskLabel));
                taskWidget->findChild<QLabel *>("icon")->hide();

                QString toolTip = QString::fromStdString(task->getToolTips());
                taskWidget->setToolTip(toolTip);
                taskWidget->setToolTipDuration(5000);

                treeFlow->addChild(treeTask);
                thiz_->ui->flowTree->setItemWidget(treeTask, 0, taskWidget);
            }
        }

        struct QMyTreeWidget : public QTreeWidget {
            QModelIndex indexFromItem(const QTreeWidgetItem *item) const {
                return QTreeWidget::indexFromItem(item, 0);
            }
        };

        if (isFocusedScene) {
            const auto &index = static_cast<QMyTreeWidget *>(thiz_->ui->flowTree)->indexFromItem(treeScene);
#if QT_VERSION >= QT_VERSION_CHECK(5, 13, 0)
            thiz_->ui->flowTree->expandRecursively(index);
#endif
        }
        else {
            const auto &index = static_cast<QMyTreeWidget *>(thiz_->ui->flowTree)->indexFromItem(treeScene);
            thiz_->ui->flowTree->collapse(index);
        }
    };

    // 显示当前配方
    int sceneIndex = 0;
    for (auto itrScene = scenes.begin(); itrScene != scenes.end(); ++itrScene, ++sceneIndex) {
        const auto &scene = *itrScene;
        bool isFocusedScene = (sceneIndex == project->focusedScene_);
        if (isFocusedScene) {
            appendScene(scene, isFocusedScene, false);
            break;
        }
    }
    // 显示共享配方
    if (sharedScene) {
        // 提前插入共享配方
        appendScene(sharedScene, true, true);
    }
    // 显示其他配方
    sceneIndex = 0;
    for (auto itrScene = scenes.begin(); itrScene != scenes.end(); ++itrScene, ++sceneIndex) {
        const auto &scene = *itrScene;
        bool isFocusedScene = (sceneIndex == project->focusedScene_);
        if (!isFocusedScene) {
            appendScene(scene, isFocusedScene, false);
        }
    }

    //thiz_->ui->flowTree->expandAll();
}

// 更新流程编辑器
void MainWindow::Impl::updateFlowEditors() {
    auto project = this->projectFile_->getProject();
    auto fucusedScene = project->getFocusedScene();
    auto sharedScene = project->getSharedScene();

    // <1> 比对全局全局编辑器
    if (this->globalFlowEditor_->getFlow() != fucusedScene->globalFlow_) {
        globalFlowEditor_->setFlow(fucusedScene->globalFlow_);
    }

    // <2> 比对其他流程编辑器
    auto impl_ = this;
    auto updateFlowGroup = [impl_](std::shared_ptr<Scene> scene, std::vector<FlowEditor *> &flowEditors, int barStartIndex) {
        cloneList<std::shared_ptr<Flow>, VList<std::shared_ptr<Flow>>::iterator, int>(
            // 比对范围
            scene->flows_.begin(),
            scene->flows_.end(),
            0,
            (int)flowEditors.size(),
            // 取值函数
            [](const VList<std::shared_ptr<Flow>>::iterator &srcItr) {
                return *srcItr;
            },
            [&](const int &desItr) {
                return flowEditors[desItr]->getFlow();
            },
            // 删除函数
            [&](const int &desItr) {
                impl_->forceDeleteFlowEditor(impl_->flowEditorBar_, desItr + barStartIndex);
                return desItr;
            },
            // 插入函数
            [&](const VList<std::shared_ptr<Flow>>::iterator &srcItr, const int &desItr) {
                // 添加新的流程
                std::shared_ptr<Flow> flow = *srcItr;
                const QString &flowName = QString::fromStdString(flow->name_);
                FlowEditor *flowEditor = impl_->newFlowEditor(flow, impl_->flowEditorBar_, true, (desItr + barStartIndex), "flowEditor", flowName);
                flowEditors.insert(flowEditors.begin() + desItr, flowEditor);
                return desItr;
            },
            // 深度比较函数
            [&](const VList<std::shared_ptr<Flow>>::iterator &srcItr, const int &desItr) {
                std::shared_ptr<Flow> flow = *srcItr;
                const QString &tabName = QString::fromStdString(flow->name_);
                if (tabName != impl_->flowEditorBar_->tabText(desItr + barStartIndex)) {
                    impl_->flowEditorBar_->setTabText(desItr + barStartIndex, tabName);
                }

                bool isTabVisible = impl_->flowEditorBar_->isTabVisible(desItr + barStartIndex);
                bool isFlowVisible = !flow->isCollpased_;
                LOG_INFO("[{}]: isTabVisible = {}, isFlowVisible = {}", desItr, isTabVisible, isFlowVisible);
                if (isTabVisible != isFlowVisible) {
                    impl_->flowEditorBar_->setTabVisible(desItr + barStartIndex, isFlowVisible);
                    QIcon icon = impl_->flowEditorBar_->tabIcon(desItr + barStartIndex);
                    impl_->flowEditorBar_->setTabIcon(desItr + barStartIndex, QIcon());
                    impl_->flowEditorBar_->setTabIcon(desItr + barStartIndex, icon);
            }
        });
    };

    updateFlowGroup(fucusedScene, this->focusedFlowEditors_, 1);
    updateFlowGroup(sharedScene, this->sharedFlowEditors_, 1 + (int)this->focusedFlowEditors_.size());

    // <3> 设定焦点
    if (fucusedScene->focusedFlow_ < 0) {
        // 设置焦点到全局监听器
        setFocusFlowEditor(globalFlowEditor_);
    }
    else if (fucusedScene->focusedFlow_ < (int)focusedFlowEditors_.size()) {
        // 设置焦点到当前配方
        setFocusFlowEditor(focusedFlowEditors_[fucusedScene->focusedFlow_]);
    }
    else if (fucusedScene->focusedFlow_ < (int)focusedFlowEditors_.size() + (int)sharedFlowEditors_.size()) {
        // 设置焦点到共享配方
        int focusedFlow = fucusedScene->focusedFlow_ - (int)focusedFlowEditors_.size();
        setFocusFlowEditor(sharedFlowEditors_[focusedFlow]);
    }
}


// 更新图片托盘
void MainWindow::Impl::updateImageFrames() {
    auto project = this->projectFile_->getProject();
    auto focusedScene = project->getFocusedScene();
    auto sharedScene = project->getSharedScene();

    // <1> 比对全局全局编辑器
    if (this->globalImageFrame_->getFlow() != focusedScene->globalFlow_) {
        globalImageFrame_->setFlow(focusedScene->globalFlow_);
    }

    // <2> 比对其他流程编辑器
    auto impl_ = this;
    auto updateFlowGroup = [impl_](std::shared_ptr<Scene> scene, std::vector<ImageFrame *> &imageFrames){
        cloneList<std::shared_ptr<Flow>, VList<std::shared_ptr<Flow>>::iterator, int>(
            // 比对范围
            scene->flows_.begin(),
            scene->flows_.end(),
            0,
            (int)imageFrames.size(),
            // 取值函数
            [](const VList<std::shared_ptr<Flow>>::iterator &srcItr) {
                return *srcItr;
            },
            [&](const int &desItr) {
                return imageFrames[desItr]->getFlow();
            },
            // 删除函数
            [&](const int &desItr) {
                PTI;
                ImageFrame *imageFrame = imageFrames[desItr];
                delete imageFrame;
                imageFrames.erase(imageFrames.begin() + desItr);
                return desItr;
            },
            // 插入函数
            [&](const VList<std::shared_ptr<Flow>>::iterator &srcItr, const int &desItr) {
                // 添加新的流程
                PTI;
                std::shared_ptr<Flow> flow = *srcItr;
                ImageFrame *imageFrame = new ImageFrame(impl_->thiz_->ui->frameImage);
                imageFrame->setObjectName("imageFrame");
                impl_->thiz_->ui->frameImage->layout()->addWidget(imageFrame);
                imageFrame->setFlow(flow);
                imageFrames.insert(imageFrames.begin() + desItr, imageFrame);
                return desItr;
            },
            // 深度比较函数
            nullptr);
    };

    updateFlowGroup(focusedScene, this->focusedImageFrames_);
    updateFlowGroup(sharedScene, this->sharedImageFrames_);

    // <3> 设定焦点
    LOG_INFO("this->imageFrames_.size = {} {}", this->focusedImageFrames_.size(), this->sharedImageFrames_.size());
    if (focusedScene->focusedFlow_ < 0) {
        for (ImageFrame *imageFrame : this->focusedImageFrames_)
            imageFrame->hide();
        for (ImageFrame *imageFrame : this->sharedImageFrames_)
            imageFrame->hide();
        globalImageFrame_->show();
    }
    else {
        int index = 0;
        for (ImageFrame *imageFrame : this->focusedImageFrames_) {
            if (index == focusedScene->focusedFlow_)
                imageFrame->show();
            else
                imageFrame->hide();
            ++index;
        }
        for (ImageFrame *imageFrame : this->sharedImageFrames_) {
            if (index == focusedScene->focusedFlow_)
                imageFrame->show();
            else
                imageFrame->hide();
            ++index;
        }
        globalImageFrame_->hide();
    }
}



// 更新组件树
void MainWindow::Impl::updateWidgetTree() {
    // 显示Widgets类型
    LOG_DEBUG("getWidgetTypes size = {}\n", thiz_->tlModule_->getWidgetTypes().size());

    std::vector<std::shared_ptr<Widget>> widgets;
    const auto &widgetTypes = thiz_->tlModule_->getWidgetTypes();
    for (size_t widgetIndex = 0; widgetIndex < widgetTypes.size(); ++widgetIndex) {
        const auto &widgetType = widgetTypes[widgetIndex];
        std::shared_ptr<Widget> widget = thiz_->tlModule_->newWidget(thiz_, widgetType, nullptr, "", -1);
        if (!widget) {
            LOG_ERROR("create widget with type {} failed!", widgetType.name_);
            continue;
        }
        LOG_DEBUG("widgetType = {}, {}, {}, {}\n", widget->name_, widget->logo_, widget->widgetType_.modulePath_, widget->order_);
        if(!widget->isHidden_)
            widgets.push_back(widget);
    }
    // 对widget排序
    std::sort(widgets.begin(), widgets.end(), [](const std::shared_ptr<Widget> &l, const std::shared_ptr<Widget> &r) {
#if 0
        // 按照分组排序
        int res = l->groupName_.compare(r->groupName_);
        if (res < 0)
            return false;
        else if (res > 0)
            return true;
#endif

        // 按照顺序权重排序
        int res = l->order_.compare(r->order_);
        if (res < 0)
            return true;
        else if (res > 0)
            return false;

        return l->name_->compare(r->name_) < 0;
        //if(std::less<std::string>()(l->order_, r->order_))            
    });

    // 显示widgets
    thiz_->ui->widgetTree->clear();
    thiz_->widgetDescriptions_.clear();

    std::map<std::string, QTreeWidgetItem *> groupItems;
    for (size_t widgetIndex = 0; widgetIndex < widgets.size(); ++widgetIndex) {
        const auto &widget = widgets[widgetIndex];

        thiz_->widgetDescriptions_.push_back({
            widget->widgetType_,
            widget->name_,
            widget->localName_,
            widget->logo_
        });

        // 分组
        auto itrGroup = groupItems.find(widget->localGroupName_);
        if (itrGroup == groupItems.end()) {
            // 创建新的分组
            QTreeWidgetItem *treeGroup = new QTreeWidgetItem();
            QString groupName = (widget->localGroupName_->empty() ? MainWindow::tr("Untitled group") : QString::fromStdString(widget->localGroupName_));
            treeGroup->setText(0, groupName);
            thiz_->ui->widgetTree->addTopLevelItem(treeGroup);
            itrGroup = groupItems.insert({ widget->localGroupName_, treeGroup }).first;
        }

        QTreeWidgetItem *treeGroup = itrGroup->second;

        // 创建新的项目
        QString logo = TlModule::getModuleFile(QString::fromStdString(widget->widgetType_.modulePath_), QString::fromStdString(widget->logo_));
        QTreeWidgetItem *treeWidget = new QTreeWidgetItem();
        treeWidget->setIcon(0, QIcon(QPixmap(logo)));
        treeWidget->setText(0, QString::fromStdString(widget->localName_->empty() ? widget->name_ : widget->localName_));
        treeWidget->setData(0, Qt::UserRole, (uint)widgetIndex);

        const std::string &description = (!widget->description_->empty()
                                          ? widget->description_
                                          : (!widget->localName_->empty()
                                              ? widget->localName_
                                              : widget->name_));
        treeWidget->setData(0, Qt::ToolTipRole, QString::fromStdString(description));
        treeGroup->addChild(treeWidget);
    }

    thiz_->ui->widgetTree->expandAll();

    // 隐藏待开发项目
    auto itrCollapse = groupItems.find(tr("developing").toStdString());
    if (itrCollapse != groupItems.end())
        thiz_->ui->widgetTree->collapseItem(itrCollapse->second);
    auto itrCollapse2 = groupItems.find(tr("Experimental (Do not use)").toStdString());
    if (itrCollapse2 != groupItems.end())
        thiz_->ui->widgetTree->collapseItem(itrCollapse2->second);
}

// 更新流程状态
void MainWindow::Impl::updateFlowStatus(const std::weak_ptr<Flow> &flow) {
    auto sFlow = flow.lock();
    if (!sFlow) return;
    FlowRunner::RunMode runMode = sFlow->getRunMode();
    LOG_INFO("{}: runMode = {}", sFlow->name_, enumStr(runMode));

    // 查找流程树
    QTreeWidgetItem *treeFlow = nullptr;
    auto itrFree = flowTreeMaps_.find(flow);
    if (itrFree != flowTreeMaps_.end())
        treeFlow = itrFree->second;

    if (treeFlow != nullptr) {
        QWidget *flowWidget = dynamic_cast<QWidget *>(thiz_->ui->flowTree->itemWidget(treeFlow, 0));
        QLabel *label = (flowWidget == nullptr ? nullptr : flowWidget->findChild<QLabel *>("icon"));
        if (label != nullptr) {
            if (runMode == FlowRunner::RunMode::kPaused) {
                label->setPixmap(QPixmap(":/icons/paused"));
                label->show();
            }
            else if (runMode == FlowRunner::RunMode::kNormalLooply
                     || runMode == FlowRunner::RunMode::kNormalOneShot) {
                label->setPixmap(QPixmap(":/icons/running"));
                label->show();
            }
            else {
                label->setPixmap(QPixmap());
                label->hide();
            }
        }
    }


    // 查找FlowEditor
    QTabBar *tabBar = nullptr;
    int tabBarIndex = -1;
    auto project = this->projectFile_->getProject();
    int index = project->getFlowIndex(sFlow);
    if (index < 0) {
        tabBar = globalEditorBar_;
        tabBarIndex = 0;
    }
    else if (index >= 0 && index < flowEditorBar_->count() - 1) {
        tabBar = flowEditorBar_;
        tabBarIndex = index + 1;
    }

    if (tabBar != nullptr) {
        if (runMode == FlowRunner::RunMode::kPaused) {
            tabBar->setTabIcon(tabBarIndex, QIcon(QPixmap(":/icons/paused")));
        }
        else if (runMode == FlowRunner::RunMode::kNormalLooply
                 || runMode == FlowRunner::RunMode::kNormalOneShot) {
            tabBar->setTabIcon(tabBarIndex, QIcon(QPixmap(":/icons/running")));
        }
        else {
            tabBar->setTabIcon(tabBarIndex, QIcon());
        }
    }

}



// 更新任务状态
void MainWindow::Impl::updateTaskStatus(const std::weak_ptr<Task> &task) {
    auto sTask = task.lock();
    if (!sTask) return;


    auto onError = [=](const Result &result) {
        if (result.code_ == ENODATA
            || result.code_ == EBADF) {
            // 不做处理
            return;
        }

        QString text;
        if (result.code_ == ENODATA) //NG
            text = QString("<font color='red' size='7'><pre>NG: %1</pre>< / font>").arg(QString::fromStdString(result.message_));
        else if (result.code_ == EBADF) //NAG
            text = QString("<font color='#DC1C96' size='7'><pre>NG: %1</pre>< / font>").arg(QString::fromStdString(result.message_));
        else
            text = QString("<font color='orange' size='7'><pre>%1</pre>< / font>").arg(QString::fromStdString(result.message_));

        LOG_ERROR("{}", text);
        TlApplication::runInGuiThread(LOC, [=](const Loc &loc) { // 异步操作
            loc.trace();
            QPoint pos = thiz_->rect().bottomRight() + thiz_->pos();
            pos.setX(pos.x() + 50);
            pos.setY(pos.y() + 50);

            if (ngToolTip_ == nullptr)
                ngToolTip_ = new QLabel(thiz_);
            QLabel *label = static_cast<QLabel *>(ngToolTip_);
            label->setObjectName("runResult");
            label->setWindowFlag(Qt::WindowType::Window);
            label->setWindowFlag(Qt::WindowType::FramelessWindowHint);
            label->setText(text);
            label->show();
            QRect rect = label->rect();
            pos.setX(pos.x() - rect.width() - 60);
            pos.setY(pos.y() - rect.height() - 60);
            label->move(pos);

            ngToolTipShowTime_ = std::chrono::steady_clock::now();
            ++ngToolTipShowCount_;

            int totalShowMs = 10000;
            startNgToolTipTimer(totalShowMs, totalShowMs);
            //QToolTip::showText(pos, text, thiz_, QRect(), 10000);
        }, false);
    };

    // 查找流程树
    QTreeWidgetItem *treeTask = nullptr;
    auto itrFree = taskTreeMaps_.find(task);
    if (itrFree != taskTreeMaps_.end())
        treeTask = itrFree->second;

    if (treeTask != nullptr) {
        QWidget *taskWidget = dynamic_cast<QWidget *>(thiz_->ui->flowTree->itemWidget(treeTask, 0));

        QString toolTip = QString::fromStdString(sTask->getToolTips());
        taskWidget->setToolTip(toolTip);

        QLabel *label = (taskWidget == nullptr ? nullptr : taskWidget->findChild<QLabel *>("icon"));
        if (label != nullptr) {
            const Result &runResult = sTask->runResult();
            LOG_DEBUG("task = {} {}, {}, task Running = {}", sTask,
                      (sTask->widget_ ? sTask->widget_->customName() : ""),
                      runResult, sTask->isRunning_);
            QPixmap pixmap;
            if (runResult.empty()) {
                if (!sTask->isRunning_) {
                    //label->setPixmap(QPixmap());
                    //label->hide();
                }
                else {
                    pixmap = QPixmap(":/icons/waiting.png");
                    //label->setPixmap(status);
                    //label->show();
                }
            }
            else if (!runResult) {
                if (runResult.code_ == EFAULT) {
                    pixmap = QPixmap(":/icons/stop.png");
                    //label->setPixmap(status);
                    //label->show();
                }
                else if (runResult.code_ == EBUSY) {
                    // waiting manually
                    pixmap = QPixmap(":/icons/waiting.png");
                }
                else if (runResult.code_ == EBADF) {
                    // Nag
                    pixmap = QPixmap(":/icons/nag.png");
                    //label->setPixmap(status);
                    //label->show();
                    onError(runResult);
                }
                else {
                    // Ng
                    pixmap = QPixmap(":/icons/ng.png");
                    //label->setPixmap(status);
                    //label->show();
                    onError(runResult);
                }
            }
            else {
                pixmap = QPixmap(":/icons/ok.png");
                //label->setPixmap(status);
                //label->show();
            }

            if (pixmap.isNull())
                label->hide();
            else {
                if (sTask->getActivatedInLoop() == 0)
                    pixmap = qGrayscale(pixmap);
                label->setPixmap(pixmap);
                label->show();
            }
        }
    }
}



FlowEditor *MainWindow::Impl::newFlowEditor(const std::shared_ptr<Flow> &flow, QTabBar* flowEditorBar, bool addCloseButton, int index, const QString &editorName, const QString &tabName) {
    if(index < 0)
        index = flowEditorBar->count() + index;

    flowEditorBar->insertTab(index, tabName);
    if (flow && flow->isCollpased_)
        flowEditorBar->setTabVisible(index, false);

    QScrollArea *flowScrollArea = new QScrollArea(thiz_->ui->flowEditorContainer);
    flowScrollArea->setObjectName(QString::fromUtf8("flowScrollArea"));
    thiz_->ui->flowEditorContainer->layout()->addWidget(flowScrollArea);
    flowScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    flowScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    flowScrollArea->setWidgetResizable(true);

    FlowEditor* flowEditor = new FlowEditor(flowScrollArea);
    flowEditor->setObjectName(editorName);
    flowScrollArea->setWidget(flowEditor);
    QSizePolicy sizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::MinimumExpanding);
    sizePolicy.setHorizontalStretch(0);
    sizePolicy.setVerticalStretch(0);
    sizePolicy.setHeightForWidth(flowEditor->sizePolicy().hasHeightForWidth());
    flowEditor->setSizePolicy(sizePolicy);
    flowEditor->setMinimumSize(QSize(0, 0));
    flowEditor->setAcceptDrops(true);


    //const std::shared_ptr<Scene> &scene = this->projectFile_->getProject()->getFocusedScene();
    if(flow != nullptr) flowEditor->setFlow(flow);

    if (addCloseButton) {
        QToolButton *button = new QToolButton();
        button->setIcon(QIcon(QPixmap(":/icons/remove.png")));
        button->setIconSize(QSize(10, 10));
        flowEditorBar->setTabButton(index, QTabBar::RightSide, button);
        QToolButton::connect(button, &QToolButton::clicked, [=]() {
            auto flow = flowEditor->getFlow();
            auto project = (flow ? flow->getProject() : nullptr);
            auto scene = (project ? project->getFocusedScene() : nullptr);
            if (flow && scene) {
                const QPoint &point = button->pos();
                int index = flowEditorBar->tabAt(point);
                LOG_INFO("index = {}", index);
                flow->isCollpased_ = true;
                FlowEditor::setFocusedFlow(scene, nullptr);
                //deleteFlowEditor(flowEditorBar, index);
            }
        });
        //flowEditorBar->setTabsClosable(addCloseButton);
    }

    return flowEditor;
}

void MainWindow::Impl::deleteFlowEditor(QTabBar *flowEditorBar, int index) {
    // 全局流程不会删除，仅删除当前流程或共享流程
    if (index >= 1 && index < flowEditorBar->count() - 1) {
        auto project = this->projectFile_->getProject();
        auto focusedScene = project->getFocusedScene();
        auto sharedScene = project->getSharedScene();
        auto flow = project->getFlowByIndex(index - 1);

        if (focusedScene->flows_.size() == 0
            || focusedScene->flows_.front() == flow) {
            QMessageBox::critical(thiz_, "", MainWindow::tr("Need keep one flow at least."));
            return;
        }

        const QString &tabName = flowEditorBar->tabText(index);
        auto res = QMessageBox::question(thiz_, "", MainWindow::tr("Delete \"%1\" ? ").arg(tabName));
        if (res != QMessageBox::Yes)
            return;

        auto scene = flow->getScene();
        scene->flows_.remove(flow);

        size_t maxFlows = focusedScene->flows_.size() + sharedScene->flows_.size();
        if (focusedScene->focusedFlow_ >= (int)maxFlows)
            focusedScene->focusedFlow_ = (int)(maxFlows - 1);
     }
}


void MainWindow::Impl::forceDeleteFlowEditor(QTabBar *flowEditorBar, int index) {
    if (disableNewFlowEditor_) return;
    disableNewFlowEditor_ = true;
    OnScopeExit scoped([&] { disableNewFlowEditor_ = false; });

    int offset = 1; // 第一个tab用作隐藏作用，需要和 flowEditors_ 对齐
    if (index >= offset && index < this->focusedFlowEditors_.size() + offset) {

        int flowIndex = index - offset;
        FlowEditor *flowEditor = this->focusedFlowEditors_[flowIndex];
        this->focusedFlowEditors_.erase(this->focusedFlowEditors_.begin() + flowIndex);
        //flowEditor->getParentScrollArea()->deleteLater();
        delete flowEditor->getParentScrollArea();
        flowEditorBar->removeTab(index);
    }
    else {
        int offset = 1 + (int)this->focusedFlowEditors_.size();
        if (index >= offset && index < this->sharedFlowEditors_.size() + offset) {

            int flowIndex = index - offset;
            FlowEditor *flowEditor = this->sharedFlowEditors_[flowIndex];
            this->sharedFlowEditors_.erase(this->sharedFlowEditors_.begin() + flowIndex);
            //flowEditor->getParentScrollArea()->deleteLater();
            delete flowEditor->getParentScrollArea();
            flowEditorBar->removeTab(index);
        }
    }
}

void MainWindow::Impl::renameFlowEditor(QTabBar *flowEditorBar, int index) {
    FlowEditor *flowEditor = nullptr;
    int offset = 1; // 第一个tab用作隐藏作用，需要和 flowEditors_ 对齐
    if (index >= offset && index < this->focusedFlowEditors_.size() + offset) {
        int flowIndex = index - offset;
        flowEditor = this->focusedFlowEditors_[flowIndex];
    }
    else {
        int offset = 1 + (int)this->focusedFlowEditors_.size();
        if (index >= offset && index < this->sharedFlowEditors_.size() + offset) {
            int flowIndex = index - offset;
            flowEditor = this->sharedFlowEditors_[flowIndex];
        }
    }

    if(flowEditor != nullptr) {
        bool ok;
        const QString &oldName = flowEditorBar->tabText(index);
        QString newName = QInputDialog::getText(thiz_, MainWindow::tr("Rename the flow"),
            MainWindow::tr("Flow Name"),
            QLineEdit::Normal,
            oldName,
            &ok);
        if (ok && !newName.isEmpty() && newName != oldName) {
            flowEditorBar->setTabText(index, newName);
            flowEditor->getFlow()->name_ = newName.toStdString();
        }
    }
}


FlowEditor *MainWindow::Impl::focusedFlowEditor() {
    // flowEditorBar_ 从序号1开始（左侧补一个不可见tab，右侧补了一个 "+"号tab）
    // globalEditorBar_ 从序号0开始（右侧补一个不可见tab）

    int index0 = flowEditorBar_->currentIndex();
    int index1 = globalEditorBar_->currentIndex();
    if (index1 != 0) {
        int offset = 1; // 第一个tab用作隐藏作用，需要和 flowEditors_ 对齐
        if (index0 >= offset && index0 < (int)this->focusedFlowEditors_.size() + offset) {
            int flowIndex = index0 - offset;
            return this->focusedFlowEditors_[flowIndex];
        }
        else {
            int offset = 1 + (int)this->focusedFlowEditors_.size();
            if (index0 >= offset && index0 < (int)this->sharedFlowEditors_.size() + offset) {
                int flowIndex = index0 - offset;
                return this->sharedFlowEditors_[flowIndex];
            }
        }
    }
    else if (index0 == 0 && index1 == 0) {
        return globalFlowEditor_;
    }
    return nullptr;
}

void MainWindow::Impl::setFocusFlowEditor(FlowEditor *flowEditor) {
    if (flowEditor == nullptr) return;

    focusedFlowEditor_ = flowEditor;
    if (flowEditor == globalFlowEditor_) {
        flowEditorBar_->setCurrentIndex(0);
        globalEditorBar_->setCurrentIndex(0);
    }
    else {
        for (int i = 0; i < (int)this->focusedFlowEditors_.size(); ++i) {
            if (flowEditor == this->focusedFlowEditors_[i]) {
                int offset = 1;
                flowEditorBar_->setCurrentIndex(i + offset);
                globalEditorBar_->setCurrentIndex(1);
                break;
            }
        }
        for (int i = 0; i < (int)this->sharedFlowEditors_.size(); ++i) {
            if (flowEditor == this->sharedFlowEditors_[i]) {
                int offset = 1 + (int)this->focusedFlowEditors_.size();
                flowEditorBar_->setCurrentIndex(i + offset);
                globalEditorBar_->setCurrentIndex(1);
                break;
            }
        }
    }

    for (int i = 0; i < (int)focusedFlowEditors_.size(); ++i) {
        if (flowEditor == focusedFlowEditors_[i]) {
            focusedFlowEditors_[i]->getParentScrollArea()->show();
        }
        else {
            focusedFlowEditors_[i]->getParentScrollArea()->hide();
        }
    }
    for (int i = 0; i < (int)sharedFlowEditors_.size(); ++i) {
        if (flowEditor == sharedFlowEditors_[i]) {
            sharedFlowEditors_[i]->getParentScrollArea()->show();
        }
        else {
            sharedFlowEditors_[i]->getParentScrollArea()->hide();
        }
    }
    if (flowEditor == globalFlowEditor_) {
        globalFlowEditor_->getParentScrollArea()->show();
    }
    else {
        globalFlowEditor_->getParentScrollArea()->hide();
    }

    flowEditor->setFocus();
};

FlowEditor *MainWindow::Impl::getFlowEditor(const std::shared_ptr<Flow> &flow) {
    for (int i = 0; i < (int)focusedFlowEditors_.size(); ++i) {
        if (flow == focusedFlowEditors_[i]->getFlow()) {
            return focusedFlowEditors_[i];
        }
    }
    for (int i = 0; i < (int)sharedFlowEditors_.size(); ++i) {
        if (flow == sharedFlowEditors_[i]->getFlow()) {
            return sharedFlowEditors_[i];
        }
    }
    if (flow == globalFlowEditor_->getFlow()) {
        return globalFlowEditor_;
    }
    return nullptr;
}

FlowEditor *MainWindow::getFlowEditor(const std::shared_ptr<Flow> &flow) {
    return impl_->getFlowEditor(flow);
}


void MainWindow::Impl::clickFlowFree(const QPoint &point) {
    if (point.isNull())
        return;

    QTreeWidgetItem *item = thiz_->ui->flowTree->itemAt(point);
    const QPoint &menuPos = thiz_->ui->flowTree->mapToGlobal(point);

    // 获取所属的流程节点  
    std::function<QTreeWidgetItem *(QTreeWidgetItem *)> getFlowItem = [&](QTreeWidgetItem *item) -> QTreeWidgetItem * {
        if (!item) return nullptr;
        const QVariant &userRole = item->data(0, Qt::UserRole);
        if (userRole.isValid() && userRole.canConvert<std::weak_ptr<Flow>>()) {
            return item;
        }
        else {
            return getFlowItem(item->parent());
        }
    };

    // 展开流程节点  
    std::function<void(QTreeWidgetItem *)> expandFlow = [&](QTreeWidgetItem *item) {
        if (!item) return;

        const QVariant &userRole = item->data(0, Qt::UserRole);
        if (userRole.isValid() && userRole.canConvert<std::weak_ptr<Flow>>()) {
            thiz_->ui->flowTree->expand(static_cast<QTreeWidgetPublic *>(thiz_->ui->flowTree)->indexFromItem(item));
        }
        else {
            for (int i = item->childCount(); i-- != 0;) {
                expandFlow(item->child(i));
            }
        }
    };

    // 收起流程节点  
    std::function<void(QTreeWidgetItem *)> collapseFlow = [&](QTreeWidgetItem *item) {
        if (!item) return;

        const QVariant &userRole = item->data(0, Qt::UserRole);
        if (userRole.isValid() && userRole.canConvert<std::weak_ptr<Flow>>()) {
            thiz_->ui->flowTree->collapse(static_cast<QTreeWidgetPublic *>(thiz_->ui->flowTree)->indexFromItem(item));
        }
        else {
            for (int i = item->childCount(); i-- != 0;) {
                collapseFlow(item->child(i));
            }
        }
    };

    auto modifyMenu = [=](bool isOnFlow, bool isOnTask) {
        return [=](QMenu *menu) {
            if (!menu) return;

            menu->addSeparator();

            if (isOnFlow) {
                QTreeWidgetItem *flowItem = getFlowItem(item);
                if (flowItem && !flowItem->isExpanded()) {
                    QAction *actionExpandFlow = new QAction(tr("Expand current flow"), menu);
                    connect(actionExpandFlow, &QAction::triggered, [=]() {
                        thiz_->ui->flowTree->expand(static_cast<QTreeWidgetPublic *>(thiz_->ui->flowTree)->indexFromItem(flowItem));
                    });
                    menu->addAction(actionExpandFlow);
                }
            }

            if (isOnFlow || isOnTask) {
                QTreeWidgetItem *flowItem = getFlowItem(item);
                if (flowItem && flowItem->isExpanded()) {
                    QAction *actionCollapseFlow = new QAction(tr("Collapse current flow"), menu);
                    connect(actionCollapseFlow, &QAction::triggered, [=]() {
                        thiz_->ui->flowTree->collapse(static_cast<QTreeWidgetPublic *>(thiz_->ui->flowTree)->indexFromItem(flowItem));
                    });
                    menu->addAction(actionCollapseFlow);
                }
            }

            QAction *actionExpandAllFlows = new QAction(tr("Expand all flows in scene"), menu);
            connect(actionExpandAllFlows, &QAction::triggered, [=]() {
                for (int i = thiz_->ui->flowTree->topLevelItemCount(); i-- != 0; ) {
                    expandFlow(thiz_->ui->flowTree->topLevelItem(i));
                }
            });
            menu->addAction(actionExpandAllFlows);
            QAction *actionCollapseAllFlows = new QAction(tr("Collapse all flows in scene"), menu);
            connect(actionCollapseAllFlows, &QAction::triggered, [=]() {
                for (int i = thiz_->ui->flowTree->topLevelItemCount(); i-- != 0; ) {
                    collapseFlow(thiz_->ui->flowTree->topLevelItem(i));
                }
            });
            menu->addAction(actionCollapseAllFlows);
        };
    };

    if (item != nullptr) {
        const QVariant &userRole = item->data(0, Qt::UserRole);
        if (!(userRole.isValid())) {
            auto project = Project::current();
            if(project)
                FlowEditor::showContextMenu(thiz_, menuPos, project, modifyMenu(false, false));
        }
        else if (userRole.canConvert<std::weak_ptr<Scene>>()) {
            auto scene = userRole.value<std::weak_ptr<Scene>>().lock();
            LOG_INFO("scene = {}", scene);
            if (scene) FlowEditor::showContextMenu(thiz_, menuPos, scene, modifyMenu(false, false));
        }
        else if (userRole.canConvert<std::weak_ptr<Flow>>()) {
            auto flow = userRole.value<std::weak_ptr<Flow>>().lock();
            LOG_INFO("flow = {}", flow);
            if (flow) FlowEditor::showContextMenu(thiz_, menuPos, flow, modifyMenu(true, false));
        }
        else if (userRole.canConvert<std::weak_ptr<Task>>()) {
            auto task = userRole.value<std::weak_ptr<Task>>().lock();
            LOG_INFO("task = {}", task);
            if (task) FlowEditor::showContextMenu(thiz_, menuPos, task, modifyMenu(false, true));
        }
    }
    else {
        auto project = Project::current();
        if (project)
            FlowEditor::showContextMenu(thiz_, menuPos, project, modifyMenu(false, false));
    }
}

void MainWindow::Impl::startDragWidget(QTreeWidgetItem *item, int column) {
    PTI;
    const QVariant &userRole = item->data(column, Qt::UserRole);
    if (!(userRole.isValid() && userRole.canConvert<uint>()))
        return;

    uint widgetIndex = userRole.toUInt();
    LOG_INFO("widgetIndex = {}", widgetIndex);

    const auto &widgetDescription = thiz_->widgetDescriptions_[widgetIndex];

    // 拖拉图片
    QPixmap pixmap;
    auto scaleFactor = std::make_shared<DataProperty<float>>(1);
    if (widgetDescription.widgetType_.name_ == "ConnectLine") {
        // 缩放系数（和各成员共享）  
        pixmap = ConnectLine::getConnectLinePixmap(scaleFactor, thiz_);
    }
    else {
        pixmap = WidgetLabel::getWidgetPixmap(thiz_,
                                              scaleFactor,
                                              widgetDescription.localName_,
                                              widgetDescription.logo_,
                                              widgetDescription.widgetType_);
    }

    QByteArray itemData;
    QDataStream dataStream(&itemData, QIODevice::WriteOnly);
    int row = 0;
    int col = 0;
    QMap<int, QVariant> roleDataMap; // what is returned by QAbstractItemModel::itemData(index)
    roleDataMap.insert(Qt::UserRole, QVariant(widgetIndex));
    dataStream << row << col << roleDataMap;
    QMimeData *mimeData = new QMimeData;
    mimeData->setData("application/x-xhawk-WidgetList", itemData);
    //mimeData->setText("child->text()");

    QDrag *drag = new QDrag(thiz_);
    drag->setMimeData(mimeData);
    drag->setPixmap(pixmap);
    const auto &size = pixmap.size() / 2;
    drag->setHotSpot(QPoint(size.width(), size.height()));

    if (drag->exec(Qt::MoveAction, Qt::MoveAction) == Qt::MoveAction) {
    }
    else {
    }
}

void MainWindow::Impl::connectDockerToMenu() {
    thiz_->ui->menuView->addAction(thiz_->ui->toolBarNormal->toggleViewAction());
    thiz_->ui->menuView->addAction(this->dockWidget_flows_->toggleViewAction());
    thiz_->ui->menuView->addAction(this->dockWidget_widgets_->toggleViewAction());
    thiz_->ui->menuView->addAction(this->dockWidget_console_->toggleViewAction());
    thiz_->ui->menuView->addAction(this->dockWidget_alarm_->toggleViewAction());
    thiz_->ui->menuView->addAction(this->dockWidget_mainUi_->toggleViewAction());
    thiz_->ui->menuView->addAction(this->dockWidget_flowEditor_->toggleViewAction());

    thiz_->ui->menuView2->addAction(thiz_->ui->toolBarRelease->toggleViewAction());
    thiz_->ui->menuView2->addAction(this->dockWidget_flows_->toggleViewAction());
    thiz_->ui->menuView2->addAction(this->dockWidget_console_->toggleViewAction());
    thiz_->ui->menuView2->addAction(this->dockWidget_alarm_->toggleViewAction());
    thiz_->ui->menuView2->addAction(this->dockWidget_mainUi_->toggleViewAction());
    //thiz_->ui->menuView->addAction(this->dockWidget_widgetAttribute_->toggleViewAction());

    //thiz_->ui->menubar->addAction(thiz_->ui->actionHmiRunner);
    
    auto removeDynamicDocker = [](QMenu *menu) {
        auto actions = menu->actions();
        for (QAction *action : actions) {
            if (action->property("isDynamicDocker").isValid()) {
                menu->removeAction(action);
            }
        }
    };

    // 更新HMI项目菜单   
    auto getUpdateDockerMenu = [this, removeDynamicDocker](QMenu *menu) {
        auto updateDockerMenu = [menu, removeDynamicDocker, this] {
            removeDynamicDocker(menu);

            // 获取当前的Docker列表  
            auto dockWidgets = this->dockManager_->dockWidgetsMap();
            for (const auto &dockWidget : dockWidgets) {
                QAction *action = dockWidget->toggleViewAction();
                if (action
                    && action->property("isDynamicDocker").isValid()) {
                    menu->addAction(action);
                }
            }
        };
        return updateDockerMenu;
    };

    auto getClearDockerMenu = [this, removeDynamicDocker](QMenu *menu) {
        auto updateDockerMenu = [menu, removeDynamicDocker] {
            return removeDynamicDocker(menu);
        };
        return updateDockerMenu;
    };
    
    QMenu::connect(thiz_->ui->menuView, &QMenu::aboutToShow, getUpdateDockerMenu(thiz_->ui->menuView));
    QMenu::connect(thiz_->ui->menuView2, &QMenu::aboutToShow, getUpdateDockerMenu(thiz_->ui->menuView2));

    QMenu::connect(thiz_->ui->menuView, &QMenu::aboutToHide, getClearDockerMenu(thiz_->ui->menuView));
    QMenu::connect(thiz_->ui->menuView2, &QMenu::aboutToHide, getClearDockerMenu(thiz_->ui->menuView2));
}

// 输出信息到控制台窗口  
void MainWindow::Impl::onTextBrowserWrite(QTextBrowser *browser, const QDateTime &now, int level, const QString &msg2) {
    const QString &sNow = now.toString("yyyy-MM-dd_HH:mm:ss.zzz");
    QString msg = sNow + " " + msg2;

    browser->moveCursor(QTextCursor::End);

    if (level == spdlog::level::info) {
        // 默认显示
        browser->setTextColor(Qt::GlobalColor::black);
        browser->insertPlainText(msg);
    }
    else if(level == spdlog::level::warn) {
        // 橙色显示
        browser->setTextColor("orange");
        browser->insertPlainText(msg);
    }
    else if (level == spdlog::level::err) {
        // 红色显示
        browser->setTextColor(QColor(255, 0, 0));
        browser->insertPlainText(msg);
    }
    else if(level == spdlog::level::critical) {
        // 红色显示
        browser->setTextColor(QColor(170, 0, 0));
        browser->insertPlainText(msg);
    }
    else if (level == spdlog::level::debug) {
        // 蓝色显示
        browser->setTextColor(Qt::GlobalColor::blue);
        browser->insertPlainText(msg);
    }
    else {
        // 默认显示
        browser->setTextColor(Qt::GlobalColor::black);
        browser->insertPlainText(msg);
    }

    //browser->setReadOnly(false);
    //browser->ensureCursorVisible();

    QScrollBar *sb = browser->verticalScrollBar();
    if(sb != nullptr)
        sb->setValue(sb->maximum());
}


void MainWindow::onConsoleWrite(const QDateTime &now, int level, const QString &msg) {
    impl_->onTextBrowserWrite(ui->console, now, level, msg);
}

void MainWindow::onAlarmWrite(const QDateTime &now, int level, const QString &msg) {
    // 文本窗口输出  
    impl_->onTextBrowserWrite(ui->alarm, now, level, msg);
}


// 选中组件
void MainWindow::onWidgetFocusIn() {
    WidgetLabel *widgetLabel = dynamic_cast<WidgetLabel *>(this->sender());
    LOG_DEBUG("onWidgetFocusIn sender {} {}\n", this->sender(), widgetLabel);

    if(widgetLabel != nullptr) {
        // 设定组件名字
        impl_->setProperyPageName(tr("Module Attribute - %1").arg(QString::fromStdString(widgetLabel->getWidget()->name_)));
        std::shared_ptr<VariantMap> cfgTable = widgetLabel->getWidget()->cfgTable_;
        LOG_DEBUG("cfgTable size = {}\n", (cfgTable ? cfgTable->size() : 0));
    }
}


// 选中图像显示窗口
void MainWindow::onImageViewerFocusIn() {
    ImageBundleViewer *imageBundleViewer = dynamic_cast<ImageBundleViewer *>(this->sender());
    LOG_DEBUG("onImageViewerFocusIn sender {} {}\n", this->sender(), imageBundleViewer);
    if (imageBundleViewer != nullptr) {
        // 设定组件名字
        QString name = imageBundleViewer->getName();
        impl_->setProperyPageName(tr("Viewer Attribute - %1").arg(name));
        //std::shared_ptr<VariantMap> configItems = roi->getConfigItems();
        //LOG_DEBUG("configItems size = {}\n", (configItems ? configItems->size() : 0));
    }
}


// 打开配置页面
void MainWindow::onOpenSettings(const QString &page, const QString &initCmd) {
    if (impl_->configDialog_ == nullptr) {
        impl_->configDialog_ = new ConfigDialog(this);
        connect(static_cast<ConfigDialog *>(impl_->configDialog_), &ConfigDialog::finished, [this]() {
            PTI;
            impl_->configDialog_->deleteLater();
            impl_->configDialog_ = nullptr;
        });
    }
    
    promise::delay(50).then(PM_LOC, [=]() {
        impl_->configDialog_->show();
        impl_->configDialog_->raise();
        impl_->configDialog_->activateWindow();
        static_cast<ConfigDialog *>(impl_->configDialog_)->navigateTo(page, initCmd);
    });
}

void MainWindow::onShowLogin(QWidget *parent) {
    const auto &currentUser1 = User::getCurrentUser();
    User currentUser2;
    {
        auto lock = hi_shared_lock(currentUser1);
        currentUser2 = currentUser1.user_;
    }

    // 登录对话框 
    auto showLoginDialog = [parent](const std::string &username, bool isAutoLogin) {
        UserLoginDialog userLoginDialog(parent);
        userLoginDialog.username_ = username;
        userLoginDialog.isAutoLogin_ = isAutoLogin;
        if (userLoginDialog.exec() == QDialog::Accepted) {
            Result res = User::login(
                userLoginDialog.username_,
                userLoginDialog.password_,
                userLoginDialog.isAutoLogin_);
            if (!res) {
                QMessageBox::critical(parent, tr("error"), QString::fromStdString(res.message_));
            }
        }
    };

    // 显示用户信息对话框
    auto showCurrentUserDialog = [parent, &currentUser2, showLoginDialog] {
        bool isSystemUser = (currentUser2.group_ == UserGroup::kSystem);

        QMessageBox msgBox(parent);
        msgBox.setWindowTitle(tr("My account"));
        msgBox.setText(tr("Current username: %1\nNickname: %2")
            .arg(QString::fromStdString(currentUser2.username_))
            .arg(QString::fromStdString(currentUser2.nickname_)));

        if (isSystemUser)
            msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::Cancel);
        else
            msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);

        // 设置按钮文本
        QAbstractButton *okButton = msgBox.button(QMessageBox::Yes);
        okButton->setText(tr("Login as other"));
        QAbstractButton *cancelButton = msgBox.button(QMessageBox::No);
        if (cancelButton != nullptr)
            cancelButton->setText(tr("Change account"));

        // 显示对话框并获取用户选择
        int ret = msgBox.exec();

        // 根据用户选择进行处理
        if (ret == QMessageBox::Yes) {
            // 退出登录  
            User::logout();
            showLoginDialog(currentUser2.username_, currentUser2.isAutoLogin_);
        }
        else if (ret == QMessageBox::No) {
            // 修改账号信息  
            UserEditDialog userEditDialog(&currentUser2, parent);
            if (userEditDialog.exec() == QDialog::Accepted) {
                if (User::getCurrentRuntimeUserGroup() != currentUser2.group_) {
                    QMessageBox::critical(parent, "", tr("User login expired, please re-login"));
                }
                else {
                    UserGroupDialog::editUser(parent, userEditDialog.getResult());
                }
            }
        }
    };

    if (currentUser2.isValid())
        showCurrentUserDialog();
    else
        showLoginDialog("", false);
}

// 项目运行结束
void MainWindow::onProjectFinished(const std::shared_ptr<Project> &project) {
    PTI;
    if (impl_->isClosing_) {
        impl_->isAutoClosing_ = true;
        this->close();
    }
}



void MainWindow::on_buttonTest_clicked() {
}

Result MainWindow::on_actionNewProject_triggered() {
    __qsync__;
    return impl_->newProject(__qwait__);
}


// 运行配方关联的hmi  
bool MainWindow::Impl::runSceneHmi(const std::shared_ptr<Scene> &scene) {
    auto hmiRunBoard = UiRunStack::findRunBoardByScene(scene);
    if (!hmiRunBoard) {
        return false;
    }
    else {
        // 不允许运行多个实例  
        bool multipleAllowed = hmiRunBoard->multipleAllowed_;
        std::string integrationMode = hmiRunBoard->integrationMode_;
        OnScopeExit onScopeExit([&] {
            hmiRunBoard->multipleAllowed_ = multipleAllowed;
            hmiRunBoard->integrationMode_ = integrationMode;
        });
        hmiRunBoard->multipleAllowed_ = false;
        if (hmiRunBoard->integrationMode_ != "run-as-docker"
            && hmiRunBoard->integrationMode_ != "run-as-window"
            && hmiRunBoard->integrationMode_ != "run-as-dialog"
            && hmiRunBoard->integrationMode_ != "run-as-root-window"
            && hmiRunBoard->integrationMode_ != "run-as-level1-menu"
            && hmiRunBoard->integrationMode_ != "run-as-level2-menu"
            && hmiRunBoard->integrationMode_ != "shrink-as-level1-menu"
            && hmiRunBoard->integrationMode_ != "shrink-as-level2-menu") {
            hmiRunBoard->integrationMode_ = "run-as-window";
        }

        showHmiWindow(hmiRunBoard->clone(), hmiRunBoard, thiz_);
        return true;
    }
}


Result MainWindow::Impl::newProject(__qsync__) {
    auto impl_ = this;
    // 运行时保存有问题，先挡一挡
    // TODO：guiToDoc改成不变动运行流程
    if (impl_->projectFile_ && !impl_->projectFile_->isAllStopped()) {
        QMessageBox::critical(thiz_, tr("Error"), tr("Project is running, stop it first!"));
        return Result::error("Project is running, stop it first!");
    }

    const Result &result = impl_->checkUnsavedProject();
    if (!result) return result;

    impl_->eventHolder_.clear();

    impl_->closeAllProjectWindows();
    impl_->projectFile_ = ProjectFile::newInstance(thiz_);
    impl_->projectFile_->newProject();
    //varSet("projectFile", impl_->projectFile_);
    impl_->docToGui(__qwait__, true);
    impl_->bindProjectFile(true, nullptr);
    while (!impl_->projectFile_->isAllStopped()) {
        QCoreApplication::processEvents();
    }
    OneShotCaller::waitAll(__qwait__);

    AppConfig &appConfig = Singleton<AppConfig>::getInstance();
    appConfig.pushRecentProject("", {});
    appConfig.save();
    return Result::success();
}

Result MainWindow::on_actionOpen_triggered() {
    __qsync__;
    return impl_->openProject(__qwait__, "", true);
}

Result MainWindow::on_actionArchive_triggered() {
    __qsync__;

    // 打开项目档案对话框
    ResultEx<QString> result = impl_->openArchive(__qwait__);
    QString path = result.object_;
    if (!path.isEmpty()) {
        return impl_->openProject(__qwait__, path, true);
    }

    return Result::success();
}

Project::ShowProgress MainWindow::Impl::newLoadingProgressBar(const QString &prompt) {
    // 在本窗口中显示创建进度
    auto start = std::chrono::steady_clock::now();
    struct ProgressDialogWrapper {
        QProgressDialog *progressDialog_;
        ~ProgressDialogWrapper() {
            close();
        }
        ProgressDialogWrapper()
            : progressDialog_(nullptr) {
        }
        void close() {
            if (progressDialog_ != nullptr) {
                progressDialog_->deleteLater();
                progressDialog_ = nullptr;
            }
        }
    };

    std::shared_ptr<ProgressDialogWrapper> progressDialogWrapper = std::make_shared<ProgressDialogWrapper>();

    auto showNewProgress = [=](float progress,
        const anypro::any &relativeObject,
        const std::string &information
        ) {
        LOG_INFO("load progress = {}, {}", progress, information);
        if (progress >= 1) {
            progressDialogWrapper->close();
            return;
        }

        if (progressDialogWrapper->progressDialog_ != nullptr) {
            progressDialogWrapper->progressDialog_->setValue(progress * 100);
            progressDialogWrapper->progressDialog_->setLabelText(QString::fromStdString(information));
            QCoreApplication::processEvents();
        }
        else {
            auto now = std::chrono::steady_clock::now();
            long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
            const int PROGRESS_DIALOG_SHOW_MS = 1000;
            if (ms >= PROGRESS_DIALOG_SHOW_MS) {
                progressDialogWrapper->progressDialog_ = new QProgressDialog(thiz_);
                progressDialogWrapper->progressDialog_->setWindowTitle(prompt);
                progressDialogWrapper->progressDialog_->setMinimumWidth(400);
                progressDialogWrapper->progressDialog_->setRange(0, 100);
                progressDialogWrapper->progressDialog_->setCancelButton(nullptr);
                progressDialogWrapper->progressDialog_->setValue(progress * 100);
                progressDialogWrapper->progressDialog_->show();
                QCoreApplication::processEvents();
            }
        }
    };
    return showNewProgress;
}



Result MainWindow::Impl::openProject(__qsync__, QString path, bool refreshGuiAfterBind, const std::optional<int> &projectId, const std::optional<int>& recipeId) {
    // 运行时保存有问题，先挡一挡
    // TODO：guiToDoc改成不变动运行流程
    Impl *impl_ = this;
    if (impl_->projectFile_ && !impl_->projectFile_->isAllStopped()) {
        QMessageBox::critical(thiz_, tr("Error"), tr("Project is running, stop it first!"));
        return Result::error("Project is running, stop it first!");
    }

    const Result &result = impl_->checkUnsavedProject();
    if (!result) return result;

    if (path.isEmpty()) {
        path = QFileDialog::getOpenFileName(thiz_, tr("Open Project"), "", tr("Project File(*.tld *.tls *.tlv)"));
    }

    if (path.isEmpty())
        return Result::error("no project path");

    if (this->projectFile_ && this->projectFile_->getPath() == path
        && !projectId.has_value() && !path.endsWith(".tls")) {
        QMessageBox::information(thiz_, tr("Warning"), tr("Project is already opened!"));
        return Result::success();
    }

    // 当前tls项目格式,支持三种打开模式,重新打开,仅载入流程结构,仅载入工具配置
    QString mergedProjectFile;
    auto getTlsOpenMode = [&] {
        TlsModeDialog dialog(thiz_);
        if (dialog.exec() == QDialog::Accepted) {
            mergedProjectFile = dialog.getSavePath();
            return dialog.getSaveMode();
        }
        return TlsModeDialog::New;
    };

    LOG_INFO("ProjectFile: {}, load path: {}", this->projectFile_->getPath().toStdString(), path.toStdString());
    if (this->projectFile_->getPath().endsWith(".tls") && path.endsWith(".tls") && this->projectFile_->getPath() != path) {
        auto mode = getTlsOpenMode();
        Result res = Result::success();
        if (mode == TlsModeDialog::OnlyLoadStructure) {
            // 当前项目工具配置 + 载入项目流程结构
            res = mergeProject(path, this->projectFile_->getPath(), mergedProjectFile);
            path = mergedProjectFile;
        }
        else if (mode == TlsModeDialog::OnlyLoadWidget) {
            // 当前项目流程结构 + 载入项目工具配置
            res = mergeProject(this->projectFile_->getPath(), path, mergedProjectFile);
            path = mergedProjectFile;
        }

        if (!res) {
            QMessageBox::critical(thiz_, tr("Failed to merge project!"), QString::fromStdString(res.message_));
            return res;
        }
    }

    impl_->closeAllProjectWindows();

    // 在本窗口中显示创建进度
    auto start = std::chrono::steady_clock::now();
    LOG_NOTIFY(tr("Project: {}, loading ...").toStdString(), path);
    auto showNewProgress = newLoadingProgressBar(tr("Project Loading ..."));
    auto clearProject = [&] {
        // Workaround: 为解决deleteLater清除变量问题，
        // 先清除一次，到下个eventloop周期再加载
        // 2022-03-07: 必须清除一次，以便窗口元素占据的shared_ptr被完全清空
        impl_->eventHolder_.clear();
        impl_->projectFile_ = ProjectFile::newInstance(thiz_);
        //varSet("projectFile", impl_->projectFile_);
        impl_->docToGui(__qwait__, true);
        impl_->bindProjectFile(true, nullptr);
        while (!impl_->projectFile_->isAllStopped()) {
            QCoreApplication::processEvents();
        }
        OneShotCaller::waitAll(__qwait__);
    };
    clearProject();

    impl_->eventHolder_.clear(); // TODO: 2022/04/11：有时序问题
    impl_->projectFile_ = ProjectFile::newInstance(thiz_);

    std::optional<int> projectIdOnload = projectId;
    std::optional<int> recipeIdOnload = recipeId;
    Result result2;
    Result result3 = safeCall(path.toStdString(), [&] {
        // 此处开始加载工程,从json工程文件中反序列化构造出项目数据结构(scene,flow,task)
        // ProjectFile::load -> 
        // Project::loadFromJson -> Scene::newFromJson -> Flow::newFromJson -> Task::newFromJson ->
        // Project::newWidget_(MainWindow::newWidgetFunc返回的Lamda实例化函数对象) -> TlModule::newWidget ->
        // WidgetTypeCache.newWidgetFuncs_['widget_name'] -> 
        // Widget::registerWidget注册的NewWidget函数实例化一个XXXWidget对象
        result2 = impl_->projectFile_->load(path, showNewProgress, projectIdOnload, recipeIdOnload);
    }, nullptr, { SafeCallMode::kDump, SafeCallMode::kStackTrace });
    
    auto scene = impl_->projectFile_->getProject()->getFocusedScene();
    // 打印下scene下关键对象
    if (scene) {
        scene->printFlows(LOC);
    }

    if (result2.empty()) result2 = result3;
    if (!result2) {
        QMessageBox::critical(thiz_, tr("Failed to load file !"), QString::fromStdString(result2.message_));
        clearProject();
        return result2;
    }

    //varSet("projectFile", impl_->projectFile_);
    impl_->docToGui(__qwait__, true);
    impl_->bindProjectFile(refreshGuiAfterBind, showNewProgress);
    OneShotCaller::waitAll(__qwait__);
    if (!refreshGuiAfterBind && showNewProgress) {
        showNewProgress(1, {}, tr("Load finished").toStdString());
    }

    //随配方启动  
    impl_->runSceneHmi(this->projectFile_->getProject()->getFocusedScene());
    impl_->restoreDockOnly(this->projectFile_->getGuiPosition());

    // 加入最近列表
    AppConfig &appConfig = Singleton<AppConfig>::getInstance();
    appConfig.pushRecentProject(path.toStdString(), recipeIdOnload);
    appConfig.save();

    // 加入项目档案
    // 获取项目活动配方名称
    std::string scName = this->projectFile_->getProject()->getFocusedScene()->name_;
    if (!scName.empty()) {
        ArchiveConfig& arConfig = Singleton<ArchiveConfig>::getInstance();
        arConfig.add({ scName, "", path.toStdString() });
        arConfig.save();
    }

    auto now = std::chrono::steady_clock::now();
    long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
    LOG_NOTIFY(tr("Project: {}, load time = {} ms").toStdString(), path, ms);
    return result2;
}

ResultEx<QString> MainWindow::Impl::openArchive(__qsync__) {
    auto archiveDialog = dynamic_cast<ProjectArchiveDialog*>(projectArchive_);
    int ret = archiveDialog->exec();
    if (ret == QDialog::Accepted) {
        LOG_INFO("open project archive {}", archiveDialog->selectedProject());
        return { archiveDialog->selectedProject(), Result::success()};
    }

    return { "", Result::success()};
}

Result MainWindow::on_actionSave_triggered() {
    // 检查是否配置修改   
    QList<WidgetDialog *> aboutToApply;
    QList<QString> aboutToApplyWidgets;
    auto isWidgetConfigChanged = [&] {
        QList<WidgetDialog *> widgetDialogs = this->findChildren<WidgetDialog *>();
        LOG_INFO("widgetDialogs.size() = {}", widgetDialogs.size());
        for (WidgetDialog *widgetDialog : widgetDialogs) {
            std::shared_ptr<Task> task = widgetDialog->getTask();
            std::shared_ptr<Widget> widget = (task ? task->widget_ : nullptr);
            if (widget) {
                const std::string &oldWidgetConfig = widgetDialog->getOldWidgetConfig();
                const std::string &newWidgetConfig = widget->saveToJson().dump();
                if (oldWidgetConfig != newWidgetConfig) {
                    aboutToApply.push_back(widgetDialog);
                    aboutToApplyWidgets.push_back(QString::fromStdString(widget->customName()));
                }
            }
        }

        return aboutToApply.size() > 0;
    };

    auto applyWidgetConfig = [&] {
        for (WidgetDialog *widgetDialog : aboutToApply) {
            widgetDialog->applyDialog();
        }
    };

    bool willIgnore = true;
    if (!User::assertAuthorized("save_project",
        tr("Project not saved, continue anyway?").toStdString(),
        willIgnore
    )) {
        if (willIgnore)
            return Result::success(); // 不保存，但看作保存成功
        else
            return Result::error("unauthorized");
    }

    // 运行时保存有问题，先挡一挡
    // 2021-08-17 DONE：guiToDoc改成不变动运行流程
    //if (impl_->projectFile_->isRunning()) {
    //    QMessageBox::critical(this, tr("Error"), tr("Project is running, stop it first!"));
    //    return Result::error("Project is running, stop it first!");
    //}

    QString path = impl_->projectFile_->getPath();
    
    if (path.length() == 0) {
        path = QFileDialog::getSaveFileName(this, tr("Save Project"), "", tr("Enhanced format(*.tld);;Separate format(*.tls);;Text format(*.tlv)"));
    }

    if (path.length() == 0)
        return Result::error("no project path");

    if (isWidgetConfigChanged()) {
        // 提示配置对话框修改   
        QString name = "";
        if (aboutToApplyWidgets.size() >= 1)
            name += '"' + aboutToApplyWidgets[0] + '"';
        if (aboutToApplyWidgets.size() >= 2)
            name += tr(" etc");
        int res = QMessageBox::question(this, "", tr("There's unsaved configuration in %1, save it anyway?").arg(name));
        if (res != QMessageBox::Yes) {
            return Result::success(); // 不保存，但看作保存成功
        }
        
        applyWidgetConfig();
    }

    impl_->guiToDoc();
    impl_->dockWidgetMonitor_->saveMainWindowStatus();
    impl_->projectFile_->getProject()->savePersist();

    Result result = impl_->projectFile_->save(path, false);
    if (!result) {
        LOG_NOTIFY("{}: {}, {}", tr("Project saved failed"), path, result.toString());
        QMessageBox::critical(this, tr("Failed to save file !"), QString::fromStdString(result.message_));
    }
    else {
        LOG_NOTIFY("{}: {}", tr("Project saved"), path);
    }

    this->setWindowTitle(impl_->projectFile_->getTitleName());

    // 加入最近列表
    AppConfig &appConfig = Singleton<AppConfig>::getInstance();
    std::optional<int> recipeId;
    impl_->projectFile_->getProject()->getRecipeId(recipeId);
    appConfig.pushRecentProject(path.toStdString(), recipeId);
    appConfig.save();

    // 加入项目档案
    std::string scName = impl_->projectFile_->getProject()->getFocusedScene()->name_;
    if (!scName.empty()) {
        ArchiveConfig& arConfig = Singleton<ArchiveConfig>::getInstance();
        arConfig.add({ scName, "", path.toStdString() });
        arConfig.save();
    }

    return result;
}

Result MainWindow::on_actionSaveAs_triggered() {
    // 运行时保存有问题，先挡一挡
    // 2021-08-17 DONE：guiToDoc改成不变动运行流程
    //if (impl_->projectFile_ && !impl_->projectFile_->isAllStopped()) {
    //    QMessageBox::critical(this, tr("Error"), tr("Project is running, stop it first!"));
    //    return Result::error("Project is running, stop it first!");
    //}

    QString path = QFileDialog::getSaveFileName(this, tr("Save Project"), "", tr("Enhanced format(*.tld);;Separate format(*.tls);;Text format(*.tlv)"));
    if (path.length() == 0)
        return Result::error("no project path");

    impl_->guiToDoc();
    impl_->dockWidgetMonitor_->saveMainWindowStatus();
    impl_->projectFile_->getProject()->savePersist();

    Result result = impl_->projectFile_->save(path, false);
    if (!result) {
        LOG_NOTIFY("{}: {}, {}", tr("Project saved failed"), path, result.toString());
        QMessageBox::critical(this, tr("Failed to save file !"), QString::fromStdString(result.message_));
    }
    else {
        LOG_NOTIFY("{}: {}", tr("Project saved"), path);
    }
    this->setWindowTitle(impl_->projectFile_->getTitleName());

    // 加入最近列表
    AppConfig &appConfig = Singleton<AppConfig>::getInstance();
    std::optional<int> recipeId;
    impl_->projectFile_->getProject()->getRecipeId(recipeId);
    appConfig.pushRecentProject(path.toStdString(), recipeId);
    appConfig.save();


    // 加入项目档案
    std::string scName = impl_->projectFile_->getProject()->getFocusedScene()->name_;
    if (!scName.empty()) {
        ArchiveConfig& arConfig = Singleton<ArchiveConfig>::getInstance();
        arConfig.add({ scName, "", path.toStdString() });
        arConfig.save();
    }

    return result;
}

Result MainWindow::on_actionImport_triggered() {
    //QMessageBox::critical(this, "", tr("Come on soon") + tr("Would you like to import and merge to current project?"));

    // 运行时保存有问题，先挡一挡
    if (impl_->projectFile_ && !impl_->projectFile_->isAllStopped()) {
        QMessageBox::critical(this, tr("Error"), tr("Project is running, stop it first!"));
        return Result::error("Project is running, stop it first!");
    }

    QString path = QFileDialog::getOpenFileName(this, tr("Import Project"), "", tr("Import File(*.tle)"));
    if (path.isEmpty())
        return Result::error("no project path");

    // 读取配置文件
    auto projectResult = readTextFile(path);
    if (!projectResult.result_) {
        return projectResult.result_;
    }

    // 获取project对象
    nlohmann::json projectJson;
    try {
        projectJson = nlohmann::json::parse(projectResult.object_.toStdString());
    }
    catch (nlohmann::json::exception ex) {
        return Result::error("{}", ex.what());
    }

    return importJson(projectJson);
}

Result MainWindow::on_actionReleaseAs_triggered() {
    // 运行时保存有问题，先挡一挡
    // TODO：guiToDoc改成不变动运行流程
    if (impl_->projectFile_ && !impl_->projectFile_->isAllStopped()) {
        QMessageBox::critical(this, tr("Error"), tr("Project is running, stop it first!"));
        return Result::error("Project is running, stop it first!");
    }

    QString path = QFileDialog::getSaveFileName(this, tr("Release the Project"), "", tr("Enhanced format(*.tld);;Separate format(*.tls);;Text format(*.tlv)"));
    if (path.length() == 0)
        return Result::error("no project path");

    impl_->guiToDoc();
    impl_->dockWidgetMonitor_->saveMainWindowStatus();
    impl_->projectFile_->getProject()->savePersist();

    Result result = impl_->projectFile_->save(path, true);
    if (!result) {
        LOG_NOTIFY("{}: {}, {}", tr("Project saved failed"), path, result.toString());
        QMessageBox::critical(this, tr("Failed to save file !"), QString::fromStdString(result.message_));
    }
    else {
        LOG_NOTIFY("{}: {}", tr("Project released"), path);
    }
    this->setWindowTitle(impl_->projectFile_->getTitleName());
    return result;
}

Result MainWindow::on_actionRecoverHistory_triggered() {
    QString path;
    if (impl_->projectFile_) {
        QString currentPath = impl_->projectFile_->getPath();
        if (currentPath.toLower().endsWith(".tld")) {
            int res = QMessageBox::question(this, "", tr("Recover from current project ?"));
            if (res == QMessageBox::Yes)
                path = currentPath;
        }
    }

    if(path.isEmpty())
        path = QFileDialog::getOpenFileName(this, tr("Open Project"), "", tr("Enhanced format(*.tld);;Separate format(*.tls)"));

    if (path.isEmpty())
        return Result::error("no project path");

    // 获取项目历史  
    std::multimap<QDateTime, int> histories;
    Result res = loadProjectHistory(path, ".tld", histories);
    if (!res) return res;

    for (const auto &[dateTime, projectId] : histories) {
        LOG_INFO("dateTime = {}, projectId = {}", dateTime.toString("yyyy-MM-dd_HH-mm-ss"), projectId);
    }

    ProjectHistoryDialog dialog(histories, this);
    dialog.exec();

    const auto &projectId = dialog.getSelectProjectId();
    if (projectId.has_value()) {
        // 进入恢复流程   
        __qsync__;
        res = impl_->openProject(__qwait__, path, true, projectId);
    }

    return res;
}

void MainWindow::on_actionQuit_triggered() {
    this->close();
}

void MainWindow::on_actionNewScene_triggered() {
    PTI;
    if (!User::assertAuthorized("edit_flow")) return;

    if (impl_->warnProjectRunning()) return;

    // 增加一个场景
    // 在所有names中查找最大的序号，并序号返回+1或的名字
    std::vector<std::string> names;

    auto project = impl_->projectFile_->getProject();
    for (int i = 0; i < project->scenes_.size(); ++i)
        names.push_back(project->scenes_[i]->name_);
    QString newName = QString::fromStdString(strNewIndexedName(MainWindow::tr("Scene").toStdString(), names));

    bool ok;
    newName = QInputDialog::getText(this, MainWindow::tr("Create and switch to new scene"),
        MainWindow::tr("Scene Name<br/><font color='red'>Flow name may not be changed later</font>"),
        QLineEdit::Normal,
        newName,
        &ok);
    if (!ok) {
        return;
    }
    if (newName.isEmpty()) {
        QMessageBox::critical(this, MainWindow::tr("Create and switch to new scene"), MainWindow::tr("Scene name is empty!"));
        return;
    }
    for (const std::string &name : names) {
        if (newName.toStdString() == name) {
            QMessageBox::critical(this, MainWindow::tr("Create and switch to new scene"), MainWindow::tr("Scene name already exists!"));
            return;
        }
    }

    MainWindow::saveSceneGuiPosition(project->getFocusedScene(), project->getSharedScene());
    auto scene = Scene::newInstance(project, "");
    scene->name_ = newName.toStdString();
    project->scenes_.push_back(scene);
    project->focusedScene_ = (int)(project->scenes_.size() - 1);
}

void MainWindow::on_actionNewFlow_triggered() {
    PTI;
    if (!User::assertAuthorized("edit_flow")) return;

    if (impl_->warnProjectRunning()) {
        impl_->setFocusFlowEditor(impl_->focusedFlowEditor_);
        return;
    }

    // 增加一个流程
    // 在所有names中查找最大的序号，并序号返回+1或的名字
    auto getExistNames = [&](bool isShared) {
        std::vector<std::string> names;
#if 0
        for (int i = 1; i < impl_->flowEditorBar_->count() - 1; ++i)
            names.push_back(impl_->flowEditorBar_->tabText(i).toStdString());
#else
        auto project = impl_->projectFile_->getProject();
        auto sharedScene = project->getSharedScene();
        if (sharedScene) {
            for (int i = 0; i < sharedScene->flows_.size(); ++i)
                names.push_back(sharedScene->flows_[i]->name_);
        }

        if (!isShared) {
            // 非共享流程，只和当前场景检查命名冲突
            auto focusedScene = project->getFocusedScene();
            if (focusedScene) {
                for (int i = 0; i < focusedScene->flows_.size(); ++i)
                    names.push_back(focusedScene->flows_[i]->name_);
            }
        }
        else {
            // 共享流程，和所有场景检查命名冲突
            for (auto scene : project->scenes_) {
                if (scene) {
                    for (int i = 0; i < scene->flows_.size(); ++i)
                        names.push_back(scene->flows_[i]->name_);
                }
            }
        }
        return names;
#endif
    };

    std::vector<std::string> names = getExistNames(false);
    QString newName = QString::fromStdString(strNewIndexedName(MainWindow::tr("Flow").toStdString(), names));

#if 1
    QInputDialog inputDialog(this);
    inputDialog.setWindowTitle(tr("Create new flow"));
    inputDialog.setLabelText(tr("Flow Name<br/><font color='red'>Flow name may not be changed later</font>"));
    inputDialog.setTextEchoMode(QLineEdit::Normal);
    inputDialog.setTextValue(newName);
    inputDialog.show();
    inputDialog.hide();

    QVBoxLayout *layout = dynamic_cast<QVBoxLayout *>(inputDialog.layout());
    QRadioButton *rbShared = nullptr;
    if (layout) {
        QWidget *widget = new QWidget(&inputDialog);
        QVBoxLayout *widgetLayout = new QVBoxLayout(widget);
        widgetLayout->setSpacing(0);
        widgetLayout->setContentsMargins(10, 0, 0, 10);

        QButtonGroup *bgSharedFlow = new QButtonGroup(widget);
        QRadioButton *rbInScene = new QRadioButton(widget);
        rbShared = new QRadioButton(widget);
        bgSharedFlow->addButton(rbInScene);
        bgSharedFlow->addButton(rbShared);
        connect(bgSharedFlow, &QButtonGroup::buttonClicked, [&] {
            bool isSharedFlow = rbShared->isChecked();
            names = getExistNames(isSharedFlow);
            LOG_INFO("{}, {}", inputDialog.textValue(), newName);
            if (inputDialog.textValue() == newName) {
                QString namePrefix = tr("Flow");
                if (isSharedFlow) namePrefix = tr("Shared ") + namePrefix;
                newName = QString::fromStdString(strNewIndexedName(namePrefix.toStdString(), names));
                inputDialog.setTextValue(newName);
            }
        });

        rbInScene->setChecked(true);

        rbInScene->setText(tr("new in current scene"));
        rbShared->setText(tr("new shared flow"));

        widgetLayout->addWidget(rbInScene);
        widgetLayout->addWidget(rbShared);
        layout->insertWidget(1, widget);
    }

    int ret = inputDialog.exec();
    bool ok = (ret != 0);
    bool isSharedFlow = (rbShared != nullptr ? rbShared->isChecked() : false);
    newName = inputDialog.textValue();

    LOG_WARN("ok = {}, isSharedFlow = {}", ret, isSharedFlow);
#else
    bool ok;
    newName = QInputDialog::getText(this, MainWindow::tr("Create new flow"),
        MainWindow::tr("Flow Name<br/><font color='red'>Flow name may not be changed later</font>"),
        QLineEdit::Normal,
        newName,
        &ok);
#endif

    if (!ok) {
        impl_->setFocusFlowEditor(impl_->focusedFlowEditor_);
        return;
    }
    if (newName.isEmpty()) {
        impl_->setFocusFlowEditor(impl_->focusedFlowEditor_);
        QMessageBox::critical(this, MainWindow::tr("Create new flow"), MainWindow::tr("Flow name is empty!"));
        return;
    }
    for (const std::string &name : names) {
        if (newName.toStdString() == name) {
            QMessageBox::critical(this, MainWindow::tr("Create new flow"), MainWindow::tr("Flow name already exists!"));
            return;
        }
    }

    auto project = impl_->projectFile_->getProject();
    auto scene = (isSharedFlow ? project->getSharedScene() : project->getFocusedScene());

    auto flow = Flow::newInstance(scene, "");
    flow->name_ = newName.toStdString();
    //flow->isDefaultRunning_ = !isSharedFlow;
    flow->isDefaultRunning_ = false;
    scene->flows_.push(flow);
    scene->focusedFlow_ = (int)(scene->flows_.size() - 1);
}

void MainWindow::on_actionNewImagePanel_triggered() {
    if (!User::assertAuthorized("edit_flow")) return;

    if (impl_->warnProjectRunning()) return;

    // 输出图像
    auto project = impl_->projectFile_->getProject();
    int index = project->getFocusedFlowIndex();
    const auto &flow = project->getFlowByIndex(index);
    std::shared_ptr<ImagePanel> imagePanel = std::make_shared<ImagePanel>();

    flow->imagePanels_.push(imagePanel);

    //imageBundleViewer->setFlow(flow);
    //const auto &tasks = flow->tasks_;
    //if (tasks.size() > 0) {
    //    //imageBundleViewer->setTask(tasks[0]);
    //}
}

void MainWindow::on_actionRunFlowLoop_triggered() {
    if (!User::assertAuthorized("run_stop_project")) return;
    LOG_NOTIFY("{}", tr("start to run looply"));
    impl_->runCurrentProject(FlowRunner::RunCmd::kRunLooply, nullptr, {});
}

void MainWindow::on_actionRunFlowOnce_triggered() {
    // 单次运行流程
    if (!User::assertAuthorized("run_stop_project")) return;
    LOG_NOTIFY("{}", tr("start to run once"));
    impl_->runCurrentProject(FlowRunner::RunCmd::kRunOneShot, nullptr, {});
}

void MainWindow::on_actionRunFlowStep_triggered() {
    if (!User::assertAuthorized("step_project")) return;
    QMessageBox::critical(this, "", tr("Not implemented yet"));
    LOG_NOTIFY("{}", tr("step run project"));
}

void MainWindow::on_actionPauseFlow_triggered() {
    if (!User::assertAuthorized("run_stop_project")) return;

    QMessageBox::StandardButton resBtn = QMessageBox::question(this, "",
        tr("Pause all the flows now?\n"),
        QMessageBox::No | QMessageBox::Yes,
        QMessageBox::Yes);
    if (resBtn != QMessageBox::Yes) {
        return;
    }

    LOG_NOTIFY("{}", tr("pause project"));
    impl_->projectFile_->pause();
}

void MainWindow::on_actionStopFlowTask_triggered() {
    __qsync__;
    if (!User::assertAuthorized("run_stop_project")) return;

    QMessageBox::StandardButton resBtn = QMessageBox::question(this, "",
        tr("Stop all the flows now?\n"),
        QMessageBox::No | QMessageBox::Yes,
        QMessageBox::Yes);
    if (resBtn != QMessageBox::Yes) {
        return;
    }

    LOG_NOTIFY("{}", tr("stop project"));
    impl_->projectFile_->stop();
    //impl_->closeAllProjectWindows();
    OneShotCaller::waitAll(__qwait__);
}

void MainWindow::on_actionVariableBrowser_triggered() {
    if (!User::assertAuthorized("show_var_browser")) return;

    // 变量浏览器测试
    if (impl_->varBrowser_ == nullptr) {
        impl_->varBrowser_ = new VarTreeBrowser(this);
        impl_->varBrowser_->setWindowFlags(impl_->varBrowser_->windowFlags() | Qt::Window);

        // 删除事件后置空
        promise::waitEvent(impl_->varBrowser_, QEvent::Close).then(PM_LOC, [=]() {
            impl_->varBrowser_->deleteLater();
            impl_->varBrowser_ = nullptr;
            //this->show();
        }, [=]() {
            // destroyed
            LOG_INFO("destroyed");
            impl_->varBrowser_ = nullptr;
        });
    }

    impl_->varBrowser_->show();
}


void MainWindow::on_actionUiRunner_triggered() {
    //随配方启动  
    if (!impl_->runSceneHmi(impl_->projectFile_->getProject()->getFocusedScene())) {
        QMessageBox::critical(this, MainWindow::tr("Error"), tr("no hmi GUI was set for current scene"));
    }
}


void MainWindow::on_actionModuleConfiguration_triggered() {
    //WidgetLabel *widgetLabel = this->ui->flowEditor->getFocusedWidgetLabel();
    //if (widgetLabel == nullptr) {
    //    QMessageBox::information(this, tr("Error"), tr("Please select the module widget first!"));
    //    return;
    //}
    //widgetLabel->getWidget()->showConfigDialog();
}

void MainWindow::on_actionResetDefaultLayout_triggered() {
    // 恢复到默认布局

    // 布局数据来源：通过保存项目调用 saveState()，从项目文件中获取布局数据
    std::string windowStateNormalMode = "0000040878da9d53cb4ec33010bcf315abdc4b1e7da84869aaaad02308a5a14764e2a5b2706c64bb2d203e1e3b492321a549d483e5ecce64763d6bc7cbaf82c3119566522cbcf036f000452e2913fb85976d37a3b9b74ce267b3a2472272a4f732ffb058faad0d16f0d2fce841a65135b195594b61081336e3e0246e62d870494c5920b0f9f49333636cfa4931b414530afc3a8183300b2fbac819fde3ac1412d892b7b21cac0f4aa183a86d78c7e81ecdeb3b97276d9955088fa4c0161cd65c6aa4ae393f897d273b44fc546e5df267466b8194fda04ea2bb294cec8afd2ab67b7df27e0fc6833d78a0cc48d563444dbad28dc24e3a631d256a42bb17036e447f0bb9145a72ece8e1ccb8f28c84135574c85778c7b4a7d11cdc6a9d769909dd7d98cc219c441dac713083309885ad14bf7977f6fbc23b4e6efe00efed59fa";
    std::string windowStateReleaseMode = "0000040878da9d53cb4ec33010bcf315abdc4b1e7da84869aaaad02308a5a14764e2a5b2706c64bb2d203e1e3b492321a549d483e5ecce64763d6bc7cbaf82c3119566522cbcf036f000452e2913fb85976d37a3b9b74ce267b3a2472272a4f732ffb058faad0d16f0d2fce841a65135b195594b61081336e3e0246e62d870494c5920b0f9f49333636cfa4931b414530afc3a8183300b2fbac819fde3ac1412d892b7b21cac0f4aa183a86d78c7e81ecdeb3b97276d9955088fa4c0161cd65c6aa4ae393f897d273b44fc546e5df267466b8194fda04ea2bb294cec8afd2ab67b7df27e0fc6833d78a0cc48d563444dbad28dc24e3a631d256a42bb17036e447f0bb9145a72ece8e1ccb8f28c84135574c85778c7b4a7d11cdc6a9d769909dd7d98cc219c441dac713083309885ad14bf7977f6fbc23b4e6efe00efed59fa";

    std::shared_ptr<Project> project = impl_->projectFile_->getProject();
    const std::string &windowState = (project && (project->isEmuReleaseMode_ || project->isReleaseMode_))
        ? windowStateReleaseMode
        : windowStateNormalMode;
    this->restoreState(QByteArray::fromHex(QByteArray::fromStdString(windowState)));

    std::string dockStateNorMalMode = "0000040878da9d53cb4ec33010bcf315abdc4b1e7da84869aaaad02308a5a14764e2a5b2706c64bb2d203e1e3b492321a549d483e5ecce64763d6bc7cbaf82c3119566522cbcf036f000452e2913fb85976d37a3b9b74ce267b3a2472272a4f732ffb058faad0d16f0d2fce841a65135b195594b61081336e3e0246e62d870494c5920b0f9f49333636cfa4931b414530afc3a8183300b2fbac819fde3ac1412d892b7b21cac0f4aa183a86d78c7e81ecdeb3b97276d9955088fa4c0161cd65c6aa4ae393f897d273b44fc546e5df267466b8194fda04ea2bb294cec8afd2ab67b7df27e0fc6833d78a0cc48d563444dbad28dc24e3a631d256a42bb17036e447f0bb9145a72ece8e1ccb8f28c84135574c85778c7b4a7d11cdc6a9d769909dd7d98cc219c441dac713083309885ad14bf7977f6fbc23b4e6efe00efed59fa";
    std::string dockStateReleaseMode = "0000040878da9d53cb4ec33010bcf315abdc4b1e7da84869aaaad02308a5a14764e2a5b2706c64bb2d203e1e3b492321a549d483e5ecce64763d6bc7cbaf82c3119566522cbcf036f000452e2913fb85976d37a3b9b74ce267b3a2472272a4f732ffb058faad0d16f0d2fce841a65135b195594b61081336e3e0246e62d870494c5920b0f9f49333636cfa4931b414530afc3a8183300b2fbac819fde3ac1412d892b7b21cac0f4aa183a86d78c7e81ecdeb3b97276d9955088fa4c0161cd65c6aa4ae393f897d273b44fc546e5df267466b8194fda04ea2bb294cec8afd2ab67b7df27e0fc6833d78a0cc48d563444dbad28dc24e3a631d256a42bb17036e447f0bb9145a72ece8e1ccb8f28c84135574c85778c7b4a7d11cdc6a9d769909dd7d98cc219c441dac713083309885ad14bf7977f6fbc23b4e6efe00efed59fa";
    const std::string &dockState = (project && (project->isEmuReleaseMode_ || project->isReleaseMode_))
        ? dockStateReleaseMode
        : dockStateNorMalMode;
    impl_->dockManager_->restoreState(QByteArray::fromHex(QByteArray::fromStdString(dockState)));

#if 0
    QSize size = ui->splitter->size();
    float unit = size.height() / (float)(1 + DEFAULT_SPLITTER_1 + DEFAULT_SPLITTER_2);
    LOG_INFO("unit = {}, splitter = {}", unit, size);
    QList<int> sizes{ (int)unit, (int)(unit * DEFAULT_SPLITTER_1), (int)(unit * DEFAULT_SPLITTER_2) };

    ui->splitter->setSizes(sizes);
#endif
    //std::string swindowState = "000000ff000000010000000300000158000001b70000008d01ffffffff000000000201";
    //this->ui->splitter->restoreState(QByteArray::fromHex(QByteArray::fromStdString(swindowState)));
}


void MainWindow::on_actionResetImageArea_triggered() {
    const auto &imageControllers = this->findChildren<ImageController *>();
    for (ImageController *imageController : imageControllers) {
        LOG_DEBUG("imageController = {}", imageController);
        imageController->zoom_ = 1;
        imageController->zoomCenter_ = cv::Point2f(0.5, 0.5);
        imageController->update();
    }

    if (imageControllers.size() == 0) {
        QMessageBox::information(this, "", tr("No image area was found!"));
    }
    else {
        QMessageBox::information(this, "", tr("Reset %1 image area(s) to full size.").arg(imageControllers.size()));
    }
}


void MainWindow::on_actionOpenDataFolder_triggered() {
    QString path = getAppDataPath("");
    QUrl url = QUrl::fromLocalFile(path);
    QDesktopServices::openUrl(url);
}

void MainWindow::on_actionOpenLogFolder_triggered() {
    QString path = getLogDataPath("");
    QUrl url = QUrl::fromLocalFile(path);
    QDesktopServices::openUrl(url);
}

void MainWindow::on_actionSwitchToReleaseMode_triggered() {
    auto project = impl_->projectFile_->getProject();
    project->isEmuReleaseMode_ = !project->isEmuReleaseMode_;
}


void MainWindow::on_actionCameras_triggered() {
    emit this->openSettings("CameraPage", "");
}

void MainWindow::on_actionSerialPorts_triggered() {
    emit this->openSettings("SerialPortPage", "");
}

void MainWindow::on_actionTcpIpDevices_triggered() {
    emit this->openSettings("TcpIpDevicePage", "");
}

void MainWindow::on_actionSettings_triggered() {
    emit this->openSettings("", "");
}

void MainWindow::on_actionProjectParameters_triggered() {
    if (!User::assertAuthorized("project_parameter")) return;
    
    auto project = impl_->projectFile_->getProject();

    if (impl_->projectParameters_ == nullptr) {
        impl_->projectParameters_ = new ProjectParameters(this, project);

        impl_->projectParameters_->setWindowFlags(impl_->projectParameters_->windowFlags() | Qt::Window);
        // 删除事件后置空
        promise::waitEvent(impl_->projectParameters_, QEvent::Close).then(PM_LOC, [=]() {
            impl_->projectParameters_->deleteLater();
            impl_->projectParameters_ = nullptr;
            PTI;
        }, [=]() {
            // destroyed
            PTI;
            LOG_INFO("destroyed");
            impl_->projectParameters_ = nullptr;
        });
    }

    impl_->projectParameters_->show();

    //connect(projectParameters, &QWidget::destroyed, [=] {
    //});

}


void MainWindow::on_actionProjectUpgradeConfigurations_triggered() {
    if (impl_->projectUpdateSettingsDialog_ == nullptr) {
        impl_->projectUpdateSettingsDialog_ = new ProjectUpdateSettingsDialog(this);
        impl_->projectUpdateSettingsDialog_->setWindowFlags(impl_->projectUpdateSettingsDialog_->windowFlags() | Qt::Window);
        // 删除事件后置空  
        QObject::connect(impl_->projectUpdateSettingsDialog_, &QObject::destroyed, [=] {
            impl_->projectUpdateSettingsDialog_ = nullptr;
        });
    }
    impl_->projectUpdateSettingsDialog_->show();
}

void MainWindow::on_actionProjectUpgrade_triggered() {
    ProjectMergeOpen mergeOpenDialog(this);
    mergeOpenDialog.exec();

    if (mergeOpenDialog.upgradeContext_) {
        ProjectMergeDialog mergeDialog(mergeOpenDialog.upgradeContext_, this);
        mergeDialog.exec();
    }
}


void MainWindow::on_actionUiDesigner_triggered() {
    if (!User::assertAuthorized("edit_flow")) return;

#if 1
    if (impl_->uiList_ == nullptr) {
        std::shared_ptr<HmiList> hmiList = getHmiList(Project::current());
        if (hmiList != nullptr) {
            auto *uiList = new UiHmiList(this);
            uiList->init(hmiList);
            impl_->uiList_ = uiList;

            impl_->uiList_->setWindowFlags(impl_->uiList_->windowFlags() | Qt::Window);
            // 删除事件后置空
            promise::waitEvent(impl_->uiList_, QEvent::Close).then(PM_LOC, [=]() {
                impl_->uiList_->deleteLater();
                impl_->uiList_ = nullptr;
                //this->show();
                PTI;
            }, [=]() {
                // destroyed
                PTI;
                LOG_INFO("destroyed");
                impl_->uiList_ = nullptr;
            });
        }

        impl_->uiList_->show();
    }
#endif
}

void MainWindow::on_actionLoadAllPlugins_triggered() {
    impl_->loadPluginDlls({});
}

void MainWindow::on_actionTimeInspector_triggered() {


    if (impl_->timeInspector_ == nullptr) {
        auto timeInspector = new TimeInspectorList(this);
        timeInspector->init();
        impl_->timeInspector_ = timeInspector;

        impl_->timeInspector_->setWindowFlags(impl_->timeInspector_->windowFlags() | Qt::Window);
        // 删除事件后置空
        promise::waitEvent(impl_->timeInspector_, QEvent::Close).then(PM_LOC, [=]() {
            impl_->timeInspector_->deleteLater();
            impl_->timeInspector_ = nullptr;
            //this->show();
            PTI;
        }, [=]() {
            // destroyed
            PTI;
            LOG_INFO("destroyed");
            impl_->timeInspector_ = nullptr;
        });
    }

    impl_->timeInspector_->show();
}

void MainWindow::on_actionUserLogin_triggered() {
    this->onShowLogin(this);
}

void MainWindow::on_actionUserGroup_triggered() {
    PTI;
    if (!User::assertAuthorized("user_management")) return;
    //QMessageBox::information(this, "", "Come on soon ...");
    UserGroupDialog userGroupDialog(this);
    userGroupDialog.exec();
}

void MainWindow::on_actionPrivilege_triggered() {
    PTI;
    if (!User::assertAuthorized("user_management")) return;
    UserAuthItemDialog userAuthItemDialog(this);
    userAuthItemDialog.exec();
}

void MainWindow::on_actionAlarmTriggers_triggered() {
    //QMessageBox::information(this, "", "Come on soon ...");
    //return;
    if (impl_->alarmTriggers_ == nullptr) {
        auto alarmTriggers = new AlarmList(this);
        alarmTriggers->init();
        impl_->alarmTriggers_ = alarmTriggers;

        impl_->alarmTriggers_->setWindowFlags(impl_->alarmTriggers_->windowFlags() | Qt::Window);
        // 删除事件后置空
        promise::waitEvent(impl_->alarmTriggers_, QEvent::Close).then(PM_LOC, [=]() {
            impl_->alarmTriggers_->deleteLater();
            impl_->alarmTriggers_ = nullptr;
            //this->show();
            PTI;
        }, [=]() {
            // destroyed
            PTI;
            LOG_INFO("destroyed");
            impl_->alarmTriggers_ = nullptr;
        });
    }

    impl_->alarmTriggers_->show();
}

void MainWindow::on_actionAlarmHistory_triggered() {
    //QMessageBox::information(this, "", "Come on soon ..."); return;
    if (!User::assertAuthorized("show_alarm_browser")) return;

    // 告警浏览器  
    if (impl_->alarmBrowser_ == nullptr) {
        impl_->alarmBrowser_ = new AlarmBrowser(this);
        impl_->alarmBrowser_->setWindowFlags(impl_->alarmBrowser_->windowFlags() | Qt::Window);

        // 删除事件后置空
        promise::waitEvent(impl_->alarmBrowser_, QEvent::Close).then(PM_LOC, [=]() {
            impl_->alarmBrowser_->deleteLater();
            impl_->alarmBrowser_ = nullptr;
            //this->show();
        }, [=]() {
            // destroyed
            LOG_INFO("destroyed");
            impl_->alarmBrowser_ = nullptr;
        });
    }

    impl_->alarmBrowser_->show();
}

void MainWindow::on_actionHelp_triggered() {
    int port = ((TlApplication *)qApp)->httpPort_;
    QString url = QString("http://127.0.0.1:%1").arg(port);
    QDesktopServices::openUrl(QUrl(url));
}

void MainWindow::on_actionAbout_triggered() {
    AboutDialog aboutDialog(this);
    aboutDialog.exec();
}

// 重载的事件函数
bool MainWindow::onClose(QCloseEvent *event) {
    __qsync__;

    if (impl_->isClosing_) {
        // 强制退出提示
        QMessageBox::StandardButton resBtn = QMessageBox::question(this, "",
            tr("Probably flow is running, force to quit now?\n"),
            QMessageBox::No | QMessageBox::Yes,
            QMessageBox::Yes);
        if (resBtn != QMessageBox::Yes) {
            LOG_INFO("{}", tr("quit application cancelled"));
            event->ignore();
            return false;
        }

        LOG_WARN("{}", tr("force to quit application"));
        impl_->mainClear_();
        event->accept();
        TlApplication::saveClosePidFile();
        return true;
    }

    if (!impl_->isQuiteClosing_) {
        QMessageBox::StandardButton resBtn = QMessageBox::question(this, "",
            tr("Quit now?\n"),
            QMessageBox::No | QMessageBox::Yes,
            QMessageBox::Yes);
        if (resBtn != QMessageBox::Yes) {
            event->ignore();
            return false;
        }
        LOG_INFO("{}", tr("quit application"));

        // 判断未关闭的文件
        const Result &result = impl_->checkUnsavedProject();
        if (!result) {
            LOG_INFO("{}", tr("quit application cancelled"));
            event->ignore();
            return false;
        }
    }

    // 停止可能运行的流程
    LOG_NOTIFY("{}", tr("stop project"));
    impl_->isClosing_ = true;
    impl_->eventHolderCloser_.cancel();
    impl_->eventHolderCloser_ = qSetTimeout(LOC, [=](const Loc &) {
        impl_->isClosing_ = false;
    }, 3000);
    if (impl_->projectFile_) {
        impl_->projectFile_->stop();
        // 等等她停下来，她实在不停我也没办法   
        std::this_thread::sleep_for(std::chrono::milliseconds(TRY_WAIT_FLOW_PAUSE_TIME_MS));
        if (!impl_->projectFile_->isAllStopped()) {
            event->ignore();
            return false;
        }
    }

    OneShotCaller::waitAll(__qwait__);

    // 清除所有窗口
    impl_->eventHolder_.clear();
    impl_->closeAllProjectWindows();
    impl_->projectFile_ = ProjectFile::newInstance(this);
    //varSet("projectFile", impl_->projectFile_);
    impl_->docToGui(__qwait__, true);
    impl_->bindProjectFile(true, nullptr);
    while (!impl_->projectFile_->isAllStopped()) {
        QCoreApplication::processEvents();
    }
    OneShotCaller::waitAll(__qwait__);
    impl_->alarmManager_.onFinal();

    event->accept();
    TlApplication::saveClosePidFile();
    return true;
}

// 重载的事件函数
bool MainWindow::isClosing() const {
    return impl_->isClosing_;
}

void MainWindow::closeEvent(QCloseEvent *event) {
    QMainWindow::closeEvent(event);
    if (impl_->isAutoClosing_) {
        event->accept();
        return;
    }
    this->onClose(event);
}

void MainWindow::onConsoleContextMenu(const QPoint& pos)
{
    QMenu* menu = ui->console->createStandardContextMenu();
    menu->addSeparator();
    QAction* clearAction = menu->addAction(tr("Clear Console"));
    connect(clearAction, &QAction::triggered, this, [this]() {
        ui->console->clear();
    });
    menu->exec(ui->console->mapToGlobal(pos));
    delete menu;
}

// 拖拉事件
static QString getProjectPathFromEvent(QDropEvent *event) {
    // 拖入文件
    QList<QUrl> urls = event->mimeData()->urls();
    for (const QUrl &url : urls) {
        QString path = url.toLocalFile();
        LOG_DEBUG("url = {}", path);
        if (path.toLower().endsWith(".tlv")) {
            return path;
        }
    }
    return "";
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event) {
    if (event->mimeData()->hasFormat("text/uri-list")) {
        // 拖入文件
        const QString &path = getProjectPathFromEvent(event);
        if (!path.isEmpty()) {
            event->acceptProposedAction();
        }
    }
    QWidget::dragEnterEvent(event);
}

void MainWindow::dropEvent(QDropEvent *event) {
    // 支持拖拉打开文件
    if (event->mimeData()->hasFormat("text/uri-list")) {
        // 拖入文件
        const QString &path = getProjectPathFromEvent(event);
        if (!path.isEmpty()) {
            // 打开文件
            __qsync__;
            impl_->openProject(__qwait__, path, true);
            event->acceptProposedAction();
        }
    }
}

// 按键事件
void MainWindow::keyPressEvent(QKeyEvent *event) {
    if (event->modifiers().testFlag(Qt::ControlModifier)) {
        if (event->key() == Qt::Key_C) {
            LOG_INFO("About to copy project");

#if 1
            // 获取当前焦点的配方、流程、或任务
            std::shared_ptr<Scene> scene;
            std::shared_ptr<Flow> flow;
            std::vector<std::shared_ptr<Task>> tasks;
            impl_->getFocusedItem(scene, flow, tasks);

            if (scene != nullptr)
                this->copyJson(scene);
            else if (flow != nullptr)
                this->copyJson(flow);
            else if (tasks.size() > 0)
                this->copyJson(tasks);

#else
            // 获取当前焦点的流程
            auto project = impl_->projectFile_->getProject();
            int index = project->getFocusedFlowIndex();
            auto flow = project->getFlowByIndex(index);
            FlowEditor *flowEditor = this->getFlowEditor(flow);

            bool isCopiedTask = false;
            if (flowEditor != nullptr) {
                // 查找焦点工具
                WidgetLabel *widgetLabel = flowEditor->getFocusedWidgetLabel();
                if (widgetLabel) {
                    // 复制工具
                    isCopiedTask = true;
                    this->copyJson(widgetLabel->getTask());
                }
            }

            if (!isCopiedTask) {
                // 复制流程
                this->copyJson(flow);
            }
#endif
        }
        else if (event->key() == Qt::Key_V) {
            LOG_INFO("About to paste project");
            auto getPasteJson = []() -> nlohmann::json {
                if (!ProjectFile::isLikeClipBoardJson())
                    return {};

                QClipboard *cb = qApp->clipboard();
                const QString &text = cb->text();
                if (text.startsWith(TL_COPY_PASTE_MAGIC"{")) {
                    try {
                        return nlohmann::json::parse(text.mid(strlen(TL_COPY_PASTE_MAGIC)).toStdString());
                    }
                    catch (nlohmann::json::exception ex) {
                    }
                }
                return {};
            };

            importJson(getPasteJson());
        }
    }

    QMainWindow::keyPressEvent(event);
}


void MainWindow::moveEvent(QMoveEvent *event) {
    QMainWindow::moveEvent(event);
}

void MainWindow::resizeEvent(QResizeEvent *event) {
    QMainWindow::resizeEvent(event);
}




// 设置属性页名称
void MainWindow::Impl::setProperyPageName(const QString &name) {
    if (this->dockWidget_widgetAttribute_ != nullptr) {
        this->dockWidget_widgetAttribute_->setWindowTitle(name);
    }
}


// 此函数支持仅最多创建一个timer的情况下延迟更新tooltip
//  (由于QTimer cancel后仍有残留，系统不能立刻清除所有timer，
//  因此不能用cancel旧的timer的办法延迟tooltip显示，否则会导致timer堆积，系统挂B）
void MainWindow::Impl::startNgToolTipTimer(int timeoutMs, int totalShowMs) {
    if (!ngToolTipTimer_.isValid()) {
        int ngToolTipShowCount = ngToolTipShowCount_;
        ngToolTipTimer_ = qSetTimeout(LOC, [this, ngToolTipShowCount, totalShowMs](const Loc &loc) {
            loc.trace();
            ngToolTipTimer_ = QExTimerHandler();

            if (ngToolTipShowCount != this->ngToolTipShowCount_) {
                // 进入这里，说明上次qSetTimeout没结束，本次的qSetTimeout没被调用
                auto passed = (std::chrono::steady_clock::now() - this->ngToolTipShowTime_);
                int passedMs = std::chrono::duration_cast<std::chrono::milliseconds>(passed).count();
                if (passedMs < totalShowMs) {
                    startNgToolTipTimer(totalShowMs - passedMs, totalShowMs);
                }
            }
            else if (ngToolTip_ != nullptr) {
                ngToolTip_->deleteLater();
                ngToolTip_ = nullptr;
            }
        }, timeoutMs);
    }
};


// 关闭变量浏览器窗口
void MainWindow::Impl::closeVarBrowser() {
    if (this->varBrowser_ != nullptr) {
        this->varBrowser_->deleteLater();
        this->varBrowser_ = nullptr;
    }
}

// 关闭告警浏览器窗口  
void MainWindow::Impl::closeAlarmBrowser() {
    if (this->alarmBrowser_ != nullptr) {
        this->alarmBrowser_->deleteLater();
        this->alarmBrowser_ = nullptr;
    }
}

// 关闭参数窗口
void MainWindow::Impl::closeProjectParameters() {
    if (this->projectParameters_ != nullptr) {
        this->projectParameters_->deleteLater();
        this->projectParameters_ = nullptr;
    }
}

// 关闭项目升级参数窗口  
void MainWindow::Impl::closeProjectUpdateSettingsDialog() {
    if (this->projectUpdateSettingsDialog_ != nullptr) {
        this->projectUpdateSettingsDialog_->deleteLater();
        this->projectUpdateSettingsDialog_ = nullptr;
    }
}


// 关闭界面设计器
void MainWindow::Impl::closeUiDesigners() {
    if (this->uiList_ != nullptr) {
        this->uiList_->deleteLater();
        this->uiList_ = nullptr;
    }
    //TODO：关闭其他界面  
}

// 关闭时间监视器
void MainWindow::Impl::closeTimeInspector() {
    if (this->timeInspector_ != nullptr) {
        this->timeInspector_->deleteLater();
        this->timeInspector_ = nullptr;
    }
}

// 关闭所有HMI窗口
void MainWindow::Impl::closeHmiWindows() {
    QList<UiRunStack *> uiRunStacks = thiz_->findChildren<UiRunStack *>();
    for (UiRunStack *uiRunStack : uiRunStacks) {
        uiRunStack->close();
    }
}

// 关闭所有当前项目相关辅助窗口
void MainWindow::Impl::closeAllProjectWindows() {
    this->dockWidgetMonitor_->saveMainWindowStatus();
    if (this->projectFile_) {
        auto project = this->projectFile_->getProject();
        project->savePersist();
    }
    closeVarBrowser();
    closeAlarmBrowser();
    closeProjectParameters();
    closeProjectUpdateSettingsDialog();
    closeUiDesigners();
    closeTimeInspector();
    closeHmiWindows();
    this->alarmManager_.reset();
}


void MainWindow::Impl::runCurrentProject(FlowRunner::RunCmd runCmd,
    const char *startupSceneName,
    const std::vector<std::string> &startupFlowNames) {
    PTI;
    // 先挡一挡重复运行，此代码有竞争问题，还需要要根据场景优化
    //if (!this->projectFile_->isAllStopped()) {
    //    QMessageBox::critical(thiz_, MainWindow::tr("Error"), MainWindow::tr("Project is already running!"));
    //    return;
    //}

    guiToDoc();
    Result result = this->projectFile_->run(runCmd, startupSceneName, startupFlowNames);
    if (!result) {
        QMessageBox::critical(thiz_, MainWindow::tr("Error"), QString::fromStdString(result.message_));
        return;
    }

    // 输出图像测试
#if 0
    ImageBundleViewer *imageBundleViewer = this->findChild<ImageBundleViewer *>("imageBundleViewer");
    if (imageBundleViewer != nullptr) {
        const auto &flow = impl_->projectFile_->getProject()->getFocusedScene()->getFocusedFlow();
        ui->imageBundleViewer->setFlow(flow);
        const auto &tasks = flow->tasks_;
        if (tasks.size() > 0) {
            ui->imageBundleViewer->setTask(tasks[0]);
        }
    }
#endif
}

bool MainWindow::Impl::warnProjectRunning() {
    if (this->projectFile_ && !this->projectFile_->isAllStopped()) {
        QMessageBox::critical(thiz_, "", MainWindow::tr("Can not edit when project is running."));
        return true;
    }
    return false;
}

Result MainWindow::Impl::checkUnsavedProject() {
    if (this->projectFile_ && this->projectFile_->getProject()->isUnsaved()) {

        bool willIgnore = true;
        if (!User::assertAuthorized("save_project",
            tr("Project not saved, continue anyway?").toStdString(),
            willIgnore
        )) {
            if (willIgnore)
                return Result::success(); // 不保存，但看作保存成功
            else
                return Result::error("unauthorized");
        }

        auto reply = QMessageBox::question(thiz_,
            MainWindow::tr("TL Vision2"),
            MainWindow::tr("Current project has not been saved!\nSave it now?"),
            QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
        if (reply == QMessageBox::Cancel)
            return Result::error("user cancelled");
        else if (reply != QMessageBox::Yes)
            return Result::success();
        
        //return Result::error("user cancelled");

        // 保存原有文件
        const Result &result = thiz_->on_actionSave_triggered();
        if (!result)
            return result;
    }
    return Result::success();
}

// 将窗口对象转为文档对象
void MainWindow::Impl::guiToDoc() {
    //std::shared_ptr<Flow> flow = thiz_->ui->flowEditor->getFlow();
    //this->projectFile_->getProject()->getFocusedScene()->setFocusedFlow(flow);

    auto focusedScene = this->projectFile_->getProject()->getFocusedScene();
    auto sharedScene = this->projectFile_->getProject()->getSharedScene();
    // 流程编辑器（当前配方）
    VList<std::shared_ptr<Flow>> focusedFlows;
    for (int index = 0; index < (int)this->focusedFlowEditors_.size(); ++index) {
        FlowEditor *flowEditor = this->focusedFlowEditors_[index];
        focusedFlows.push(flowEditor->getFlow());
    }
    if(focusedScene->flows_ != focusedFlows)
        focusedScene->flows_.splice(0, (int)focusedScene->flows_.size(), focusedFlows);

    // 流程编辑器（共享配方）
    VList<std::shared_ptr<Flow>> sharedFlows;
    for (int index = 0; index < (int)this->sharedFlowEditors_.size(); ++index) {
        FlowEditor *flowEditor = this->sharedFlowEditors_[index];
        sharedFlows.push(flowEditor->getFlow());
    }
    if (sharedScene->flows_ != sharedFlows)
        sharedScene->flows_.splice(0, (int)sharedScene->flows_.size(), sharedFlows);

    // 当前流程
    int focusedFlow = -1;
    if (focusedFlowEditor_ != this->globalFlowEditor_) {
        focusedFlow = (int)flowEditorBar_->currentIndex() - 1;
        if (!(focusedFlow >= 0 && focusedFlow < focusedFlowEditors_.size() + sharedFlowEditors_.size()))
            focusedFlow = 0;
    }
    focusedScene->focusedFlow_ = focusedFlow;

    // 全局流程
    focusedScene->globalFlow_ = this->globalFlowEditor_->getFlow();

    this->projectFile_->setGuiPosition(saveGuiPoistion());
}

// 将文档对象转为窗口对象
void MainWindow::Impl::docToGui(__qsync__, bool restoreFullGui) {
    // 窗口标题
    auto project = this->projectFile_->getProject();
    thiz_->setWindowTitle(this->projectFile_->getTitleName());

    // 组件编辑区域
    //auto flow = project->getFocusedScene()->getFocusedFlow();

    // 场景、流程树
    this->updateMainToolbar();
    this->updateFlowTree();
    this->updateFlowEditors();
    this->updateImageFrames();

    // updateImageFrames() 调用 setFlow，通过再次触发事件绑定生成ImagePanel
    // 因此，需要用qSetTimeout延后到下一时钟周期，才有响应的ImagePanel，才可以恢复ImagePanel的位置。
    qSetTimeout(LOC, [this, restoreFullGui, project](const Loc &loc) {
        loc.trace();
        restoreGuiPosition(this->projectFile_->getGuiPosition(), restoreFullGui);
        this->updateReleaseMode();
    }, 0).wait(__qwait__);
}

// 保存窗口对象位置
nlohmann::json MainWindow::Impl::saveGuiPoistion() {
    //const nlohmann::json &flowEditor = thiz_->ui->flowEditor->saveGuiPoistion();

    auto project = this->projectFile_->getProject();
    auto focusedScene = project->getFocusedScene();
    auto sharedScene = project->getSharedScene();
    thiz_->saveSceneGuiPosition(focusedScene, sharedScene);

    nlohmann::json sceneGuiPositions = nlohmann::json::array();

    for (auto scene : project->scenes_) {
        scene->guiPosition_["id"] = project->objIdCreator_.getIdStr(scene.get());
        sceneGuiPositions.push_back(scene->guiPosition_);
    }
    sharedScene->guiPosition_["id"] = project->objIdCreator_.getIdStr(sharedScene.get());

    return {
        { "scenes", sceneGuiPositions },
        { "sharedScene", sharedScene->guiPosition_},
        { "geometry", thiz_->saveGeometry().toHex().toStdString() },
        { "windowState", thiz_->saveState().toHex().toStdString() },
        { "dockState", this->dockManager_->saveState().toHex().toStdString() },
#if 0
        { "sgeometry", thiz_->ui->splitter->saveGeometry().toHex().toStdString() },
        { "swindowState", thiz_->ui->splitter->saveState().toHex().toStdString() },
#endif
    };
}

// 恢复窗口对象位置(仅恢复Dock)  
void MainWindow::Impl::restoreDockOnly(const nlohmann::json &json) {
    auto project = this->projectFile_->getProject();

    auto enterLoadingState = this->dockWidgetMonitor_->scopedEnterLoadingState();
    std::shared_ptr<DockStates> dockState;
    if (this->dockWidgetMonitor_) {
        dockState = this->dockWidgetMonitor_->getDockStates(project);
    }

    if (dockState && !dockState->managerState_.isEmpty()) {
        // 恢复动态保存的dock位置  
        this->dockManager_->restoreState(dockState->managerState_);
    }
    else if (json.contains("dockState")) {
        // 恢复项目保存的dock位置  
        const std::string &dockState = json.at("dockState");
        this->dockManager_->restoreState(QByteArray::fromHex(QByteArray::fromStdString(dockState)));
    }
}

// 恢复窗口对象位置
void MainWindow::Impl::restoreGuiPosition(const nlohmann::json &json, bool restoreFullGui) {
    auto project = this->projectFile_->getProject();

    auto enterLoadingState = this->dockWidgetMonitor_->scopedEnterLoadingState();
    std::shared_ptr<DockStates> dockState;
    if (this->dockWidgetMonitor_) {
        dockState = this->dockWidgetMonitor_->getDockStates(project);
    }

    const QPoint &pos = thiz_->pos();
    if (restoreFullGui) {
        if (dockState && !dockState->mainWindowGeometry_.isEmpty()) {
            // 恢复动态保存的主窗口位置  
            thiz_->restoreGeometry(dockState->mainWindowGeometry_);
        }
        else if (json.contains("geometry")) {
            // 恢复项目保存的主窗口位置  
            const std::string &geometry = json.at("geometry");
            const QPoint &pos = thiz_->pos();
            thiz_->restoreGeometry(QByteArray::fromHex(QByteArray::fromStdString(geometry)));
        }

        if ((dockState && !dockState->mainWindowGeometry_.isEmpty()) || json.contains("geometry")) {
#if 0
            if (!thiz_->isMaximized() && !thiz_->isFullScreen())
                thiz_->move(pos);
#else
            // 当窗口被恢复到足以影响视界范围时，恢复到当前位置   

            QRect windowGeometry = thiz_->geometry();
            QList<QScreen *> screens = QGuiApplication::screens();

            QRect contentGeometry = thiz_->contentsRect();
            // 计算标题栏和边框的宽度和高度
            int borderWidth = (windowGeometry.width() - contentGeometry.width()) / 2;
            int titleBarHeight = windowGeometry.height() - contentGeometry.height() - 2 * borderWidth;


            LOG_INFO("Window geometry: {}, titleBarHeight = {}", windowGeometry, titleBarHeight);
            bool isWindowVisible = false;
            bool isWindowTitleVisible = false;
            for (int i = 0; i < screens.size(); ++i) {
                QScreen *screen = screens.at(i);
                QRect screenGeometry = screen->geometry();
                LOG_INFO("Screen {} geometry: {}", i, screenGeometry);
                QRect rect = screenGeometry.intersected(windowGeometry);
                if ((double)rect.width() * (double)rect.height() >=
                    (double)(windowGeometry.width() * (double)windowGeometry.height()) * 0.15) {
                    // 超过窗口的15%面积时，认为可见  
                    isWindowVisible = true;
                }

                LOG_INFO("rect {}, {} {} ", rect, (double)rect.width() * (double)rect.height(), (double)(windowGeometry.width() * (double)windowGeometry.height()) * 0.15);

                if (windowGeometry.y() >= 0) {
                    isWindowTitleVisible = true;
                }
            }

            if (!isWindowVisible || !isWindowTitleVisible) {
                thiz_->move(pos);
            }
#endif

        }

        if (dockState && !dockState->mainWindowState_.isEmpty()) {
            // 恢复动态保存的主窗口状态  
            thiz_->restoreState(dockState->mainWindowState_);
        }
        else if (json.contains("windowState")) {
            // 恢复项目保存的主窗口状态  
            const std::string &windowState = json.at("windowState");
            thiz_->restoreState(QByteArray::fromHex(QByteArray::fromStdString(windowState)));
        }

        this->restoreDockOnly(json);
    }

#if 0
    if (json.contains("sgeometry")) {
        const std::string &geometry = json.at("sgeometry");
        thiz_->ui->splitter->restoreGeometry(QByteArray::fromHex(QByteArray::fromStdString(geometry)));
    }

    if (json.contains("swindowState")) {
        const std::string &windowState = json.at("swindowState");
        thiz_->ui->splitter->restoreState(QByteArray::fromHex(QByteArray::fromStdString(windowState)));
    }
#endif

    nlohmann::json sceneGuiPositions;
    if (json.contains("sharedFlowEditors")) {
        sceneGuiPositions = json.at("sharedFlowEditors");
    }

    this->projectDockFlowsVisible_ = this->dockWidget_flows_->isVisible();
    this->projectDockWidgetsVisible_ = this->dockWidget_widgets_->isVisible();
    this->projectDockFlowEditorVisible_ = this->dockWidget_flowEditor_->isVisible();
#if 0
    this->projectSplitSizes_ = thiz_->ui->splitter->sizes();
#endif

    if (json.contains("scenes")) {
        std::map<std::string, std::shared_ptr<Scene>> sceneMap; //scence速查表
        for (const auto &scene : project->scenes_) {
            const std::string &sceneId = project->objIdCreator_.getIdStr(scene.get());
            sceneMap.insert({ sceneId, scene });
        }

        const nlohmann::json &sceneGuiPositions = json.at("scenes");
        if (sceneGuiPositions.is_array()) {
            for (const nlohmann::json &sceneGuiPosition : sceneGuiPositions) {
                if (sceneGuiPosition.contains("id")) {
                    const std::string &sceneId = sceneGuiPosition.at("id");
                    auto found = sceneMap.find(sceneId);
                    if (found != sceneMap.end()) {
                        auto scene = found->second;
                        scene->guiPosition_ = sceneGuiPosition;
                    }
                }
            }
        }
    }

    if (json.contains("sharedScene")) {
        const nlohmann::json &sceneGuiPosition = json.at("sharedScene");
        auto sharedScene = project->getSharedScene();
        sharedScene->guiPosition_ = sceneGuiPosition;
    }

    // 恢复默认的窗口配置  
    auto focusedScene = project->getFocusedScene();
    auto sharedScene = project->getSharedScene();
    thiz_->restoreSceneGuiPosition(focusedScene, sharedScene);
}

// 保存场景位置参数
void MainWindow::saveSceneGuiPosition(const std::shared_ptr<Scene> &focusedScene, const std::shared_ptr<Scene> &sharedScene) {
    if (focusedScene) {

        // 流程编辑器窗口
        nlohmann::json flowEditors = nlohmann::json::array();
        for (FlowEditor *flowEditor : impl_->focusedFlowEditors_) {
            flowEditors.push_back(flowEditor->saveGuiPoistion());
        }

        const nlohmann::json &globalFlowEditor = impl_->globalFlowEditor_->saveGuiPoistion();

        // 图像托盘窗口
        nlohmann::json imageFrames = nlohmann::json::array();
        for (ImageFrame *imageFrame : impl_->focusedImageFrames_) {
            imageFrames.push_back(imageFrame->saveGuiPoistion());
        }
        const nlohmann::json &globalImageFrame = impl_->globalImageFrame_->saveGuiPoistion();

        nlohmann::json sceneGuiPosition = {
            { "flowEditors", flowEditors },
            { "globalFlowEditor", globalFlowEditor },
            { "imageFrames", imageFrames },
            { "globalImageFrame", globalImageFrame },
        };

        focusedScene->guiPosition_ = sceneGuiPosition;
    }

    if (sharedScene) {
        // 流程编辑器窗口
        nlohmann::json flowEditors = nlohmann::json::array();
        for (FlowEditor *flowEditor : impl_->sharedFlowEditors_) {
            flowEditors.push_back(flowEditor->saveGuiPoistion());
        }

        nlohmann::json imageFrames = nlohmann::json::array();
        for (ImageFrame *imageFrame : impl_->sharedImageFrames_) {
            imageFrames.push_back(imageFrame->saveGuiPoistion());
        }

        nlohmann::json sceneGuiPosition = {
            { "flowEditors", flowEditors },
            { "imageFrames", imageFrames },
        };

        sharedScene->guiPosition_ = sceneGuiPosition;
    }
}

// 恢复场景位置参数
void MainWindow::restoreSceneGuiPosition(const std::shared_ptr<Scene> &focusedScene, const std::shared_ptr<Scene> &sharedScene) {
    if (focusedScene) {
        const nlohmann::json &sceneGuiPositions = focusedScene->guiPosition_;
        if (sceneGuiPositions.contains("globalImageFrame")) {
            impl_->globalImageFrame_->restoreGuiPosition(sceneGuiPositions.at("globalImageFrame"));
        }

        nlohmann::json imageFrames;
        if (sceneGuiPositions.contains("focusedImageFrames")) {
            // 兼容性保留
            imageFrames = sceneGuiPositions.at("focusedImageFrames");
        }
        else if (sceneGuiPositions.contains("imageFrames")) {
            imageFrames = sceneGuiPositions.at("imageFrames");
        }
        if (imageFrames.is_array()) {
            for (size_t i = 0; i < imageFrames.size() && i < impl_->focusedImageFrames_.size(); ++i) {
                const nlohmann::json &imageFrame = imageFrames.at(i);
                impl_->focusedImageFrames_[i]->restoreGuiPosition(imageFrame);
            }
        }

        if (sceneGuiPositions.contains("globalFlowEditor")) {
            impl_->globalFlowEditor_->restoreGuiPosition(sceneGuiPositions.at("globalFlowEditor"));
        }

        nlohmann::json flowEditors;
        if (sceneGuiPositions.contains("focusedFlowEditors")) {
            // 兼容性保留
            flowEditors = sceneGuiPositions.at("focusedFlowEditors");
        }
        else if (sceneGuiPositions.contains("flowEditors")) {            
            flowEditors = sceneGuiPositions.at("flowEditors");
        }

        if (flowEditors.is_array()) {
            for (size_t i = 0; i < flowEditors.size() && i < impl_->focusedFlowEditors_.size(); ++i) {
                const nlohmann::json &flowEditor = flowEditors.at(i);
                impl_->focusedFlowEditors_[i]->restoreGuiPosition(flowEditor);
            }
        }
    }
    
    if (sharedScene) {
        const nlohmann::json &sceneGuiPositions = sharedScene->guiPosition_;
        nlohmann::json imageFrames;
        if (sceneGuiPositions.contains("sharedImageFrames")) {
            // 兼容性保留
            imageFrames = sceneGuiPositions.at("sharedImageFrames");
        }
        else if (sceneGuiPositions.contains("imageFrames")) {
            imageFrames = sceneGuiPositions.at("imageFrames");
        }
        if (imageFrames.is_array()) {
            for (size_t i = 0; i < imageFrames.size() && i < impl_->sharedImageFrames_.size(); ++i) {
                const nlohmann::json &imageFrame = imageFrames.at(i);
                impl_->sharedImageFrames_[i]->restoreGuiPosition(imageFrame);
            }
        }

        nlohmann::json flowEditors;
        if (sceneGuiPositions.contains("sharedFlowEditors")) {
            flowEditors = sceneGuiPositions.at("sharedFlowEditors");
        }
        else if (sceneGuiPositions.contains("flowEditors")) {
            // 兼容性保留
            flowEditors = sceneGuiPositions.at("flowEditors");
        }

        if (flowEditors.is_array()) {
            for (size_t i = 0; i < flowEditors.size() && i < impl_->sharedFlowEditors_.size(); ++i) {
                const nlohmann::json &flowEditor = flowEditors.at(i);
                impl_->sharedFlowEditors_[i]->restoreGuiPosition(flowEditor);
            }
        }
    }
}


// 显示状态栏位。多个栏位将根据name排序, timtoutMs = 0是不用超时
void MainWindow::showStatusbar(const QString &name,
    const QPixmap &icon,
    const QString &text,
    int timeoutMs) {

    LOG_DEBUG("name = {}, text = {}, timeoutMs = {}",
        name, text, timeoutMs);

    // 创建一个带图标功能的标签
    auto createIconLabel = [](const QString &name) -> QWidget *{
        QWidget *widget = new QWidget;
        QHBoxLayout *layout = new QHBoxLayout();
        layout->setContentsMargins(0, 0, 0, 0);
        widget->setLayout(layout);

        QLabel *icon = new QLabel(widget);
        icon->setObjectName("icon_" + name);
        icon->setScaledContents(true);
        icon->setFixedSize(12, 12);
        icon->setPixmap(QPixmap());
        layout->addWidget(icon);

        QLabel *text = new QLabel(widget);
        text->setObjectName(name);
        text->setTextFormat(Qt::TextFormat::RichText);
        text->setScaledContents(true);
        text->setMinimumWidth(0);
        text->setText("");
        layout->addWidget(text);

        return widget;
    };

    QWidget *statusbar = nullptr;
    std::map<QString, QWidget *> &statusbars = impl_->statusbars_;
    auto found = statusbars.find(name);
    if (found == statusbars.end()) {
        statusbar = createIconLabel(name);
        statusbar->setObjectName(name);

        statusbars.insert({ name, statusbar });

        // 插入  
        int index = 2; // 左侧有两个
        for (auto itr = statusbars.begin(); itr != statusbars.end(); ++itr, ++index) {
            if (itr->first == name) {
                ui->statusbar->insertWidget(index, statusbar, 0);
                break;
            }
        }
    }
    else {
        statusbar = found->second;
    }

    QLabel *labelIcon = statusbar->findChild<QLabel *>("icon_" + name);
    QLabel *labelText = statusbar->findChild<QLabel *>(name);
    if (icon.isNull()) {
        if(!labelIcon->isHidden())
            labelIcon->hide();
    }
    else {
        if(!labelIcon->isHidden())
            labelIcon->show();
        labelIcon->setPixmap(icon);
    }
    labelText->setText(text);

    QLabel *expand = ui->statusbar->findChild<QLabel *>("__expand__");
    if (expand != nullptr && expand->isHidden())
        expand->show();

    if (statusbar->isHidden()) {
        statusbar->show();
    }

    // 清除旧的timer
    const QVariant &vOldTimer = labelText->property("closeTimer");
    if (vOldTimer.isValid()) {
        QExTimerHandler oldTimer = vOldTimer.value<QExTimerHandler>();
        qClearTimeout(oldTimer);
    }

    // 设定新的timer
    if (timeoutMs > 0) {
        auto timer = qSetTimeout(LOC, statusbar, [=](const Loc &) {
            if (!statusbar->isHidden())
                statusbar->hide();
        }, timeoutMs);
        labelText->setProperty("closeTimer", QVariant::fromValue(timer));
    }
}

void MainWindow::hideStatusbar(const QString &name) {
    QWidget *statusbar = nullptr;

    auto found = impl_->statusbars_.find(name);
    if (found != impl_->statusbars_.end()) {
        statusbar = found->second;
    }

    if (statusbar != nullptr) {
        QLabel *labelText = statusbar->findChild<QLabel *>(name);
        // 清除旧的timer
        const QVariant &vOldTimer = labelText->property("closeTimer");
        if (vOldTimer.isValid()) {
            QExTimerHandler oldTimer = vOldTimer.value<QExTimerHandler>();
            qClearTimeout(oldTimer);
        }

        if (!statusbar->isHidden())
            statusbar->hide();
    }
}


// 导出配置和数据到json字符串  
std::string MainWindow::exportJson(const std::shared_ptr<Project> &project) {
    // TODO：未实现
    // impl_->guiToDoc();
    return project ? project->saveToJson().dump(4) : "";
}
std::string MainWindow::exportJson(const std::shared_ptr<Scene> &scene) {
    impl_->guiToDoc();
    const auto &json = impl_->projectFile_->exportJson(scene);
    return json.dump(4);
}
std::string MainWindow::exportJson(const std::shared_ptr<Flow> &flow) {
    impl_->guiToDoc();
    const auto &json = impl_->projectFile_->exportJson(flow);
    return json.dump(4);
}
std::string MainWindow::exportJson(const std::shared_ptr<Task> &task) {
    impl_->guiToDoc();
    const auto &json = impl_->projectFile_->exportJson(task);
    return json.dump(4);
}
std::string MainWindow::exportJson(const std::vector<std::shared_ptr<Task>> &tasks) {
    impl_->guiToDoc();
    const auto &json = impl_->projectFile_->exportJson(tasks);
    return json.dump(4);
}

// 导出配置和数据到剪贴板  
void MainWindow::copyJson(const std::shared_ptr<Project> &project) {
    QClipboard *cb = qApp->clipboard();
    const QString &text = QString::fromStdString(exportJson(project));
    cb->setText(TL_COPY_PASTE_MAGIC + text);
}
void MainWindow::copyJson(const std::shared_ptr<Scene> &scene) {
    QClipboard *cb = qApp->clipboard();
    const QString &text = QString::fromStdString(exportJson(scene));
    cb->setText(TL_COPY_PASTE_MAGIC + text);
}
void MainWindow::copyJson(const std::shared_ptr<Flow> &flow) {
    QClipboard *cb = qApp->clipboard();
    const QString &text = QString::fromStdString(exportJson(flow));
    cb->setText(TL_COPY_PASTE_MAGIC + text);
}
void MainWindow::copyJson(const std::shared_ptr<Task> &task) {
    QClipboard *cb = qApp->clipboard();
    const QString &text = QString::fromStdString(exportJson(task));
    cb->setText(TL_COPY_PASTE_MAGIC + text);
}
void MainWindow::copyJson(const std::vector<std::shared_ptr<Task>> &tasks) {
    QClipboard *cb = qApp->clipboard();
    const QString &text = QString::fromStdString(exportJson(tasks));
    cb->setText(TL_COPY_PASTE_MAGIC + text);
}

// 导出配置和数据到文件  
void MainWindow::saveAsJson(const std::shared_ptr<Project> &projct) {
    // TODO：未实现
}

void MainWindow::saveAsJson(const std::shared_ptr<Scene> &scene) {
    QString path = QFileDialog::getSaveFileName(this, tr("Export"), "", tr("Exported File(*.tle)"));
    if (path.length() == 0)
        return;

    const QString &text = QString::fromStdString(exportJson(scene));
    Result result = writeTextFile(path, text, false, nullptr);
    if (!result) {
        QMessageBox::critical(this, tr("Failed to export file !"), QString::fromStdString(result.message_));
    }
}

void MainWindow::saveAsJson(const std::shared_ptr<Flow> &flow) {
    QString path = QFileDialog::getSaveFileName(this, tr("Export"), "", tr("Exported File(*.tle)"));
    if (path.length() == 0)
        return;

    const QString &text = QString::fromStdString(exportJson(flow));
    Result result = writeTextFile(path, text, false, nullptr);
    if (!result) {
        QMessageBox::critical(this, tr("Failed to export file !"), QString::fromStdString(result.message_));
    }
}

void MainWindow::saveAsJson(const std::shared_ptr<Task> &task) {
    QString path = QFileDialog::getSaveFileName(this, tr("Export"), "", tr("Exported File(*.tle)"));
    if (path.length() == 0)
        return;

    const QString &text = QString::fromStdString(exportJson(task));
    Result result = writeTextFile(path, text, false, nullptr);
    if (!result) {
        QMessageBox::critical(this, tr("Failed to export file !"), QString::fromStdString(result.message_));
    }
}

void MainWindow::saveAsJson(const std::vector<std::shared_ptr<Task>> &tasks) {
    QString path = QFileDialog::getSaveFileName(this, tr("Export"), "", tr("Exported File(*.tle)"));
    if (path.length() == 0)
        return;

    const QString &text = QString::fromStdString(exportJson(tasks));
    Result result = writeTextFile(path, text, false, nullptr);
    if (!result) {
        QMessageBox::critical(this, tr("Failed to export file !"), QString::fromStdString(result.message_));
    }
}


// 导入配置或数据
Result MainWindow::importJson(const nlohmann::json &json) {
    __qsync__;

    if (!json.contains("type")) {
        return Result::error("type error");
    }
    if (!User::assertAuthorized("edit_flow")) return Result::error("unauthorized");

    impl_->guiToDoc();

    std::string type = json.at("type");
    if (type == "export_task") {
        return impl_->importJson(__qwait__, json, type, &ProjectFile::importTask);
    }
    else if (type == "export_flow") {
        int res = QMessageBox::question(this, "", tr("Would you like to paste the flow ?"));
        if (res != QMessageBox::Yes)
            return Result::error("cancelled");
        return impl_->importJson(__qwait__, json, type, &ProjectFile::importFlow);
    }
    else if (type == "export_scene") {
        int res = QMessageBox::question(this, "", tr("Would you like to paste the scene ?"));
        if (res != QMessageBox::Yes)
            return Result::error("cancelled");
        return impl_->importJson(__qwait__, json, type, &ProjectFile::importScene);
    }
    else {
        return Result::error("type error");
    }
    //impl_->docToGui(__qwait__, false);

    return Result::success();
}

Result MainWindow::Impl::importJson(
    __qsync__,
    const nlohmann::json &json,
    const std::string &type,
    Result (ProjectFile::*importJson)(const nlohmann::json &json, const Project::ShowProgress &showProgressFunc)) {
    PTI;

    // 运行时保存有问题，先挡一挡
    // TODO：guiToDoc改成不变动运行流程
    Impl *impl_ = this;
    if (impl_->projectFile_ && !impl_->projectFile_->isAllStopped()) {
        QMessageBox::critical(thiz_, tr("Error"), tr("Project is running, stop it first!"));
        return Result::error("Project is running, stop it first!");
    }

    // 在本窗口中显示创建进度
    auto start = std::chrono::steady_clock::now();
    auto showNewProgress = this->newLoadingProgressBar(tr("Data import ..."));

    Result result2;
    Result result3 = safeCall(type, [&] {
        result2 = (impl_->projectFile_.get()->*importJson)(json, showNewProgress);
    }, nullptr, { SafeCallMode::kDump | SafeCallMode::kStackTrace });
    if (result2.empty()) result2 = result3;
    if (!result2) {
        QMessageBox::critical(thiz_, tr("Failed to import data!"), QString::fromStdString(result2.message_));
        return result2;
    }

    //varSet("projectFile", impl_->projectFile_);
    impl_->docToGui(__qwait__, false);
    OneShotCaller::waitAll(__qwait__);

    qSetTimeout(LOC, [=](const Loc &) {
        if (importJson == &ProjectFile::importScene) {
            auto project = impl_->projectFile_->getProject();
            project->focusedScene_ = (int)project->scenes_.size() - 1;
            OneShotCaller::waitAll(__qwait__);
        }
        if (importJson == &ProjectFile::importScene
            || importJson == &ProjectFile::importFlow) {
            auto project = impl_->projectFile_->getProject();
            auto focusedScene = project->getFocusedScene();
            int index = (int)focusedScene->flows_.size() - 1;
            project->setFocusedFlowIndex(index);
            OneShotCaller::waitAll(__qwait__);
        }
    }, 0);

    auto now = std::chrono::steady_clock::now();
    long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
    LOG_INFO("Import time {} ms", ms);
    return result2;
}

// 获取当前取得焦点的配方、流程或工具
// 注意：根据界面焦点状况，返回项目均可能为空
void MainWindow::Impl::getFocusedItem(
    std::shared_ptr<Scene> &scene,
    std::shared_ptr<Flow> &flow,
    std::vector<std::shared_ptr<Task>> &tasks
) {
    bool flowTreeHasFocus = thiz_->ui->flowTree->hasFocus();
    bool flowEditorBarHasFocus = thiz_->ui->flowEditorBar->hasFocus();
    bool globalEditorBarHasFocus = thiz_->ui->globalEditorBar->hasFocus();
    LOG_INFO("flowTreeHasFocus = {}, {}, {}",
        flowTreeHasFocus,
        flowEditorBarHasFocus,
        globalEditorBarHasFocus);


    if (thiz_->ui->flowTree->hasFocus()) {
        // 焦点在流程树
        QList<QTreeWidgetItem *> items = thiz_->ui->flowTree->selectedItems();
        QTreeWidgetItem *item = (items.size() > 0 ? items.front() : nullptr);
        LOG_INFO("item = {}", item);

        if (item != nullptr) {
            const QVariant &userRole = item->data(0, Qt::UserRole);
            if (!(userRole.isValid())) {
            }
            else if (userRole.canConvert<std::weak_ptr<Scene>>()) {
                scene = userRole.value<std::weak_ptr<Scene>>().lock();
                LOG_INFO("scene = {}", scene);
            }
            else if (userRole.canConvert<std::weak_ptr<Flow>>()) {
                flow = userRole.value<std::weak_ptr<Flow>>().lock();
                LOG_INFO("flow = {}", flow);
            }
            else if (userRole.canConvert<std::weak_ptr<Task>>()) {
                auto task = userRole.value<std::weak_ptr<Task>>().lock();
                LOG_INFO("task = {}", task);
                if (task)
                    tasks.push_back(task);
            }
        }
    }
    else if (thiz_->ui->flowEditorBar->hasFocus()
        || thiz_->ui->globalEditorBar->hasFocus()) {
        // 焦点在tab
        auto project = this->projectFile_->getProject();
        int index = project->getFocusedFlowIndex();
        flow = project->getFlowByIndex(index);
        LOG_INFO("flow = {}", flow);
    }
    else if(this->focusedFlowEditor_ != nullptr) {
        // 检查当前流程编辑器是否有焦点 
        LOG_INFO("this->focusedFlowEditor_ = {}", this->focusedFlowEditor_);

        const std::set<QWidget *> &selectedItems
            = this->focusedFlowEditor_->getSelectedItems();
        for (QWidget *widget : selectedItems) {
            WidgetLabel *widgetLabel = dynamic_cast<WidgetLabel *>(widget);
            if (widgetLabel != nullptr) {
                tasks.push_back(widgetLabel->getTask());
            }
            //ConnectLine *connectLine = dynamic_cast<ConnectLine *>(widget);
            //if (connectLine != nullptr) {
            //}
        }

        for (const auto &task : tasks) {
            LOG_INFO("task = {}", task);
        }

    }
}



void MainWindow::on_dockWidget_flows_visibilityChanged(bool visible) {
    if (!impl_->inUpdateReleaseMode_) {
        impl_->projectDockFlowsVisible_ = impl_->dockWidget_flows_->isVisible();
    }
}


void MainWindow::on_dockWidget_widgets_visibilityChanged(bool visible) {
    if (!impl_->inUpdateReleaseMode_) {
        impl_->projectDockWidgetsVisible_ = impl_->dockWidget_widgets_->isVisible();
    }
}

void MainWindow::on_dockWidget_flowEditor_visibilityChanged(bool visible) {
    if (!impl_->inUpdateReleaseMode_) {
        impl_->projectDockFlowEditorVisible_ = impl_->dockWidget_flowEditor_->isVisible();
    }
}

void MainWindow::on_splitter_splitterMoved(int pos, int index) {
    if (!impl_->inUpdateReleaseMode_) {
#if 0
        impl_->projectSplitSizes_ = this->ui->splitter->sizes();
#endif
    }
}


void MainWindow::on_actionLanguageEnglish_triggered() {
    impl_->useLanguage("en", ui->actionLanguageEnglish);
}

void MainWindow::on_actionLanguageSimplifiedChinese_triggered() {
    impl_->useLanguage("zh_CN", ui->actionLanguageSimplifiedChinese);
}




void MainWindow::Impl::antiMsgBoxStart() {

#if defined _WIN32
    auto checkDllWindow = [](const QString &title) {
        HWND hWnd = FindWindowW(NULL, title.toStdWString().c_str());
        if (hWnd == NULL) {
            return;
        }
        LOG_DEBUG("window found");
        SendMessage(hWnd, WM_CLOSE, 0, 0);
        return;

        // 找到 OK 按钮
        HWND hBtn = FindWindowEx(hWnd, NULL, TEXT("Button"), TEXT("确定"));
        if (hBtn == NULL) {
            return;
        }

        LOG_DEBUG("button found");

        // 模拟点击 OK 按钮
        SendMessage(hBtn, BM_CLICK, 0, 0);
    };

    this->antiMsgBoxThreadRunning_ = true;
    this->antiMsgBoxThread_ = std::thread([checkDllWindow, this] {
        // 获取名为 "gui.exe - 无法找到入口" 的窗口句柄 
        QString exePath = QCoreApplication::applicationFilePath();
        QString exeName = QFileInfo(exePath).fileName();

        QString localTitle = QString("%1 - %2").arg(exeName).arg(tr("Entry Point Not Found"));
        QString enTitle = QString("%1 - %2").arg(exeName).arg("Entry Point Not Found");
        QString localTitle2 = QString("TL Vision 2: %1 - %2").arg(exeName).arg(tr("Entry Point Not Found"));
        QString enTitle2 = QString("TL Vision 2: %1 - %2").arg(exeName).arg("Entry Point Not Found");

        while (this->antiMsgBoxThreadRunning_) {
            checkDllWindow(localTitle);
            checkDllWindow(enTitle);
            checkDllWindow(localTitle2);
            checkDllWindow(enTitle2);
            Sleep(300);
        }
    });
#endif
}

void MainWindow::Impl::antiMsgBoxStop() {
    if (this->antiMsgBoxThread_.joinable()) {
        this->antiMsgBoxThreadRunning_ = false;
        this->antiMsgBoxThread_.join();
        this->antiMsgBoxThread_ = std::thread();
    }
}

#if defined _WIN32
#include <psapi.h>
#include <tlhelp32.h>

QString getParentProcessName() {
    DWORD parentPID = 0;
    DWORD currentPID = GetCurrentProcessId();
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);

    if (hSnapshot == INVALID_HANDLE_VALUE) {
        return "Error: Unable to create process snapshot.";
    }

    OnScopeExit onScopeExit([&] {
        CloseHandle(hSnapshot);
    });

    PROCESSENTRY32 pe32 = { 0 };
    pe32.dwSize = sizeof(PROCESSENTRY32);

    if (Process32First(hSnapshot, &pe32)) {
        do {
            if (pe32.th32ProcessID == currentPID) {
                parentPID = pe32.th32ParentProcessID;
                break;
            }
        } while (Process32Next(hSnapshot, &pe32));
    }


    if (parentPID == 0) {
        return "Error: Unable to find parent process.";
    }

    HANDLE hParentProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, parentPID);
    if (!hParentProcess) {
        return "Error: Unable to open parent process.";
    }

    OnScopeExit onScopeExit2([&] {
        CloseHandle(hParentProcess);
    });

    char processName[MAX_PATH] = "<unknown>";
    if (GetModuleBaseNameA(hParentProcess, NULL, processName, MAX_PATH) > 0) {
        CloseHandle(hParentProcess);
        return QString::fromLocal8Bit(processName);
    }

    return "Error: Unable to retrieve process name.";
}
#else
QString getParentProcessName() {
    return "Error: Unknown platform.";
}
#endif

void MainWindow::Impl::useLanguage(const std::string &localeName, QAction *action) {
    std::string currentLocale = tl_global::getLocale();
    if (currentLocale.empty()) {
        QLocale locale;
        currentLocale = locale.name().toStdString();
    }

    if (currentLocale == localeName) {
        QMessageBox::information(thiz_, "", tr("Current language is %1").arg(action->text()));
        return;
    }

    AppConfig &appConfig = singleton<AppConfig>();
    appConfig.locale_ = localeName;
    appConfig.save();

    QString langInfo = action->property("langInfo").value<QString>();

    if (getParentProcessName() != "splash_screen.exe") {
        QMessageBox::warning(thiz_, "", langInfo.split("\n").front());
    }
    else {
        int res = QMessageBox::question(thiz_, "", langInfo);
        if (res == QMessageBox::Yes) {
            forceTerminateProcess(EXIT_FAILURE);
        }
    }
}

void MainWindow::Impl::setupConsoleEventFilter() {
    promise::PromiseEventFilter& filter = promise::PromiseEventFilter::getSingleInstance();

    auto listenerFunc = [=](QObject* object, QEvent* event) {
        // 判断鼠标中键按下
        if (event->type() == QEvent::MouseButtonPress) {
            QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
            if (mouseEvent->button() == Qt::MiddleButton) {
                auto project = projectFile_->getProject();
                if (project) {
                    auto scene = project->getFocusedScene();
                    if (scene) {
                        scene->printFlows(LOC);
                    }
                }
            }
        }
        return false;
    };

    LOG_INFO("setupConsoleEventFilter object:{} event:{}", thiz_->ui->console, QEvent::MouseButtonPress);
    listenerConsoleKeyPress_ = filter.addEventListener(thiz_->ui->console, QEvent::MouseButtonPress, listenerFunc);

}