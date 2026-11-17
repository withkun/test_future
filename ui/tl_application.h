#pragma once
#ifndef INC_TL_APPLICATION_H_
#define INC_TL_APPLICATION_H_

#include "utils/lib_export.h"
#include "utils/debug.h"
#include "utils/misc.h"
#include "utils/qt_ex_timer.h"
#include "types/result.h"
#include "utils/data_bind.h"
#include <thread>
#include <memory>
#include <functional>
#include <QApplication>
#include <QPixmap>
#include <QString>

class EXPORTED QExThread;
class EXPORTED JSEngineRuntime;

// 一个组件关联的窗口对象基类
class EXPORTED TlApplication
    : public QApplication
    , public BindingData {
    Q_OBJECT

public:
    TlApplication(int &argc, char **argv);
    ~TlApplication();

    // 主线程相关
    // 在 gui 线程中运行代码（根据isWait决定同步或异步）
    static Result runInGuiThread(const Loc &loc, const std::function<void(const Loc &)> &func, bool isWait);
    static Result runInGuiThread(const std::function<void()> &func, bool isWait);
    // （异步）在 gui 线程中运行代码
    // 函数运行完，执行callback(result)
    using Callback = std::function<void(const Result &result)>;
    static void runInGuiThreadAsync(const Loc &loc, 
                                    const std::function<void(const Loc &, const Callback &callback)> &func,
                                    const TlApplication::Callback &callback);
    static void runInGuiThreadAsync(const std::function<void(const Callback &callback)> &func,
                                    const TlApplication::Callback &callback);

    // 界面线程的cpp线程id
    static std::thread::id getGuiThreadId();
    // 确保代码在gui线程（否则将打出错误，并且触发异常）
    static void assertGuiThread();

    // 命名线程相关
    static void startNamedThread(const std::string &name);
    static void stopNamedThread(const std::string &name);
    static void stopAllNamedThread();
    static std::shared_ptr<QExThread> namedThread(const std::string &name);

    // 在命名线程中运行代码（根据isWait决定同步或异步）
    static Result runInNamedThread(const std::string &name, const Loc &loc, const std::function<void(const Loc &)> &func, bool isWait);
    static Result runInNamedThread(const std::string &name, const std::function<void()> &func, bool isWait);

    // IO线程相关（兼容性保留，实际为一个名字为io的命名线程）  
    static void startIoThread();
    static void stopIoThread();
    static std::shared_ptr<QExThread> ioThread();

    // 在IO线程中运行代码（根据isWait决定同步或异步）
    static Result runInIoThread(const Loc &loc, const std::function<void(const Loc &)> &func, bool isWait);
    static Result runInIoThread(const std::function<void()> &func, bool isWait);

    // 获取js引擎
    static JSEngineRuntime *jsEngineRuntime();
    static void initJsEngineRuntime();

    // 启动一个timer
    static std::weak_ptr<QExTimer> setTimeout(const Loc &loc, const std::function<void(const Loc &)> &cb, int ms, const std::string &type = "");
    // 清除一个timer
    static void clearTimeout(const std::weak_ptr<QExTimer> &timer);
    // 清除所有的timer
    static void clearTimeoutAll(const std::string &type = "");
    // （其他线程，阻塞消息循环的）等待本线程所有timer完成  
    static void waitTimersFinished(__qsync__, const std::string &type = "");

    // 设定主窗口
    static QWidget *mainWindow();
    static void setMainWindow(QWidget *widget);

    // 设定是否在主窗口初始化完成状态状态
    static bool isMainWindowReady();
    static void setMainWindowReady(bool isRready);

    // 显示状态栏位。多个栏位将根据name排序, timtoutMs = 0是不用超时
    static void showStatusbar(const QString &name,
        const QPixmap &icon,
        const QString &text,
        int timeoutMs);
    // 隐藏状态栏位  
    static void hideStatusbar(const QString &name);

    // 记录运行文件  
    static Result savePidFile();
    // 记录运行文件兼退出标志（这种情况下，忽略后续异常自动的重启）  
    static Result saveClosePidFile();
    // 删除运行文件  
    static void removePidFile();
    // 判断运行文件是否存在（非空说明上次运行出现了异常）
    static std::string getLastPidFromFile();

    // 获取当前app对象  
    static TlApplication *instance();

    // 是否使用console输出
    bool useConsole_;

    // http服务端口
    int httpPort_;

private slots:
    void onRunInGuiThread(Loc loc, std::function<void(Loc)> func);
    void onRunInGuiThreadAsync(Loc loc, std::function<void(Loc, Callback callback)> func, TlApplication::Callback callback);

signals:
    void sigRunInGuiThread(Loc loc, std::function<void(Loc)> func);
    void sigRunInGuiThreadAsync(Loc loc, std::function<void(Loc, Callback callback)> func, TlApplication::Callback callback);

private:
    class EXPORTED Impl;
    std::unique_ptr<Impl> impl_;
};

#endif // INC_TL_APPLICATION_H_
