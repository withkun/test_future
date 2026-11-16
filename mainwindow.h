#pragma once
#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "project/project_file.h"
#include "jsengine/tl_module.h"
#include "types/widget.h"

#include <list>
#include <memory>
#include <atomic>

#include <QMainWindow>
#include <QTextBrowser>
#include <QListWidget>
#include <QQmlEngine>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

// 强制退出程序
void forceTerminateProcess(int code);

class Scene;
class Flow;
class FlowEditor;
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(uint32_t autoClose_ms, // 自动关闭时间, 0不自动关闭
               bool isLastRunOk,      // 上次运行是否正常退出
               const std::function<void()> &mainClear, // 主程序清理函数，传到这里用于强制退出
               QWidget *parent = nullptr);
    ~MainWindow();

    // 在本窗口中创建算法组件
    Project::NewWidget newWidgetFunc();
    // 根据流程获取对应的窗口  
    FlowEditor *getFlowEditor(const std::shared_ptr<Flow> &flow);
    // 保存场景位置参数
    void saveSceneGuiPosition(const std::shared_ptr<Scene> &focusedScene, const std::shared_ptr<Scene> &sharedScene);
    // 恢复场景位置参数
    void restoreSceneGuiPosition(const std::shared_ptr<Scene> &focusedScene, const std::shared_ptr<Scene> &sharedScene);

    // 导出配置和数据到json字符串  
    std::string exportJson(const std::shared_ptr<Project> &project);
    std::string exportJson(const std::shared_ptr<Scene> &scene);
    std::string exportJson(const std::shared_ptr<Flow> &flow);
    std::string exportJson(const std::shared_ptr<Task> &task);
    std::string exportJson(const std::vector<std::shared_ptr<Task>> &tasks);
    // 导出配置和数据到剪贴板  
    void copyJson(const std::shared_ptr<Project> &project);
    void copyJson(const std::shared_ptr<Scene> &scene);
    void copyJson(const std::shared_ptr<Flow> &flow);
    void copyJson(const std::shared_ptr<Task> &task);
    void copyJson(const std::vector<std::shared_ptr<Task>> &tasks);
    // 导出配置和数据到文件  
    void saveAsJson(const std::shared_ptr<Project> &project);
    void saveAsJson(const std::shared_ptr<Scene> &scene);
    void saveAsJson(const std::shared_ptr<Flow> &flow);
    void saveAsJson(const std::shared_ptr<Task> &task);
    void saveAsJson(const std::vector<std::shared_ptr<Task>> &tasks);
    // 导入配置或数据
    Result importJson(const nlohmann::json &json);

signals:
    void cppSend(QString textOut);
public slots:
    void cppReceive(QString textIn);

signals:
    void consoleWrite(const QDateTime &now, int level, const QString &msg);
    void alarmWrite(const QDateTime &now, int level, const QString &msg);
    void quiteClose();
    void openSettings(const QString &page, const QString &initCmd);
    void showLogin(QWidget *parent);

public slots:
    // 输出信息到控制台窗口
    void onConsoleWrite(const QDateTime &now, int level, const QString &msg);
    void onAlarmWrite(const QDateTime &now, int level, const QString &msg);
    // 选中组件
    void onWidgetFocusIn();
    // 选中图像显示窗口
    void onImageViewerFocusIn();
    // 打开配置页面 (initCmd：打开的同时是否插入新配置项目）
    void onOpenSettings(const QString &page, const QString &initCmd);
    // 在指定的窗口上显示登录对话框  
    static void onShowLogin(QWidget *parent);
    // 项目运行结束
    void onProjectFinished(const std::shared_ptr<Project> &project);

    // 显示状态栏位。多个栏位将根据name排序, timtoutMs = 0是不用超时
    void showStatusbar(const QString &name,
        const QPixmap &icon,
        const QString &text,
        int timeoutMs);
    // 隐藏状态栏位 
    void hideStatusbar(const QString &name);

private slots:
    void on_buttonTest_clicked();
    Result on_actionNewProject_triggered();
    Result on_actionOpen_triggered();
    Result on_actionArchive_triggered();
    Result on_actionSave_triggered();
    Result on_actionSaveAs_triggered();
    Result on_actionImport_triggered();
    Result on_actionReleaseAs_triggered();
    Result on_actionRecoverHistory_triggered();
    void on_actionQuit_triggered();
    void on_actionNewScene_triggered();
    void on_actionNewFlow_triggered();
    void on_actionNewImagePanel_triggered();
    void on_actionRunFlowLoop_triggered();
    void on_actionRunFlowOnce_triggered();
    void on_actionRunFlowStep_triggered();
    void on_actionPauseFlow_triggered();
    void on_actionStopFlowTask_triggered();
    void on_actionUiRunner_triggered();
    void on_actionVariableBrowser_triggered();
    void on_actionModuleConfiguration_triggered();
    void on_actionResetDefaultLayout_triggered();
    void on_actionResetImageArea_triggered();
    void on_actionOpenDataFolder_triggered();
    void on_actionOpenLogFolder_triggered();
    void on_actionSwitchToReleaseMode_triggered();
    void on_actionCameras_triggered();
    void on_actionSerialPorts_triggered();
    void on_actionTcpIpDevices_triggered();
    void on_actionSettings_triggered();
    void on_actionProjectParameters_triggered();
    void on_actionProjectUpgradeConfigurations_triggered();
    void on_actionProjectUpgrade_triggered();
    void on_actionUiDesigner_triggered();
    void on_actionLoadAllPlugins_triggered();
    void on_actionTimeInspector_triggered();
    void on_actionUserLogin_triggered();
    void on_actionUserGroup_triggered();
    void on_actionPrivilege_triggered();
    void on_actionAlarmTriggers_triggered();
    void on_actionAlarmHistory_triggered();
    void on_actionHelp_triggered();
    void on_actionAbout_triggered();
    void on_dockWidget_flows_visibilityChanged(bool visible);
    void on_dockWidget_widgets_visibilityChanged(bool visible);
    void on_dockWidget_flowEditor_visibilityChanged(bool visible);
    void on_splitter_splitterMoved(int pos, int index);

    void on_actionLanguageEnglish_triggered();
    void on_actionLanguageSimplifiedChinese_triggered();

    // 重载的事件函数
    bool onClose(QCloseEvent *event);
    bool isClosing() const;
    // 处理console右键菜单
    void onConsoleContextMenu(const QPoint& pos);                       

protected:

    // 重载的事件函数
    void closeEvent(QCloseEvent *event) override;
    // 拖拉事件
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    // 按键事件
    void keyPressEvent(QKeyEvent *event) override;
    void moveEvent(QMoveEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    Ui::MainWindow                          *ui;
    class Impl;
    std::shared_ptr<Impl>                    impl_;
public:
    std::shared_ptr<TlModule>                tlModule_;     // 模块维护类

    struct WidgetDesc {
        Widget::WidgetType widgetType_;
        std::string name_;
        std::string localName_;
        std::string logo_;
    };
    std::vector<WidgetDesc>                  widgetDescriptions_; // 模块描述
};
#endif // MAINWINDOW_H
