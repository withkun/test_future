#include "ui/tl_application.h"
#include "utils/qthread_ex.h"
#include "utils/log.h"
#include "utils/qt_utils.h"
#include "utils/qthread_ex.h"
#include "jsengine/jsr_widget.h"
#include <future>
#include <QFile>
#include <QLockFile>

template<typename T>
static bool CompareSmartPtr(const std::weak_ptr<T> &left, const std::weak_ptr<T> &right) {
    return left.owner_before(right);
};


class TlApplication::Impl {
public:
    Impl()
        : timerMutex_(LOC)
        , timers_()
        , namedThreadMutex_(LOC) 
        , namedThreads_() {
    }
    ~Impl() {
    }

    QString getRunFile() const;
    QString getCloseFile() const;
    std::shared_ptr<QExThread> getOrStartNamedThread(const std::string &name);
    std::shared_ptr<QExThread> getNamedThread(const std::string &name);
    void stopNamedThread(const std::string &name);

    std::unique_ptr<QLockFile> lockFile_; // 防止重复运行
    std::thread::id guiThreadId_;

    // Timer
    hi_mutex timerMutex_;
    hi_condition_variable timerCond_;
    std::map<std::string, std::set<std::weak_ptr<QExTimer>, decltype(&CompareSmartPtr<QExTimer>)>> timers_;

    std::atomic<QWidget *> mainWindow_{ nullptr };
    std::atomic<bool> isMainWindowReady_;
    hi_mutex namedThreadMutex_;
    std::map<std::string, std::shared_ptr<QExThread>> namedThreads_;
    std::unique_ptr<JSEngineRuntime> jsEngineRuntime_;
};


TlApplication::TlApplication(int &argc, char **argv)
    : QApplication(argc, argv)
    , BindingData(LOC)
    , impl_(new Impl){
    useConsole_ = true;

    static const int DEFAULT_HTTP_PORT = 9000;
    httpPort_ = DEFAULT_HTTP_PORT;

    impl_->guiThreadId_ = std::this_thread::get_id();
    impl_->mainWindow_ = nullptr;
    impl_->isMainWindowReady_ = false;
    connect(this, &TlApplication::sigRunInGuiThread, this, &TlApplication::onRunInGuiThread);
    connect(this, &TlApplication::sigRunInGuiThreadAsync, this, &TlApplication::onRunInGuiThreadAsync);
}

TlApplication::~TlApplication() {
    TlApplication::stopAllNamedThread();
}

void TlApplication::onRunInGuiThread(Loc loc, std::function<void(Loc)> func) {
    func(loc);
}


void TlApplication::onRunInGuiThreadAsync(Loc loc,
                                          std::function<void(Loc, Callback callback)> func,
                                          TlApplication::Callback callback) {
    func(loc, callback);
}

// 在 gui 线程中运行代码
Result TlApplication::runInGuiThread(const Loc &loc,
                                     const std::function<void(const Loc &)> &func,
                                     bool isWait) {
    TlApplication *tlApp = TlApplication::instance();

    if (isWait) {
        if (TlApplication::getGuiThreadId() == std::this_thread::get_id()) {
            if (tlApp->impl_->mainWindow_ != nullptr) {
                func(loc);
            }
        }
        else {

            std::shared_ptr<std::promise<void>> promise2 = std::make_shared<std::promise<void>>();;
            std::future<void> result2 = promise2->get_future();

            std::function<void(Loc)> func2 = [func, promise2, tlApp](const Loc &loc) {
                if (tlApp->impl_->mainWindow_ != nullptr) {
                    func(loc);
                    promise2->set_value();
                }
            };

            emit tlApp->sigRunInGuiThread(loc, func2);
            std::future_status status = std::future_status::timeout;
            while (tlApp->impl_->mainWindow_ != nullptr && status == std::future_status::timeout) {
                using namespace std::chrono_literals;
                status = result2.wait_for(1s);
            }

            if (status == std::future_status::ready)
                return Result::success();
            else
                return Result::error();
        }
    }
    else {
        std::function<void(Loc)> func2 = [func](Loc loc) {
            func(loc);
        };
        emit tlApp->sigRunInGuiThread(loc, func2);
    }

    return Result::success();
}

Result TlApplication::runInGuiThread(const std::function<void()> &func,
                                     bool isWait) {
    return runInGuiThread(LOC, [func](const Loc &loc) {
        loc.trace();
        func();
    }, isWait);
}

void TlApplication::runInGuiThreadAsync(const Loc &loc,
                                        const std::function<void(const Loc &, const TlApplication::Callback &callback)> &func,
                                        const TlApplication::Callback &callback) {
    TlApplication *tlApp = TlApplication::instance();

    std::function<void(Loc, TlApplication::Callback callback)> func2
        = [func, tlApp ](Loc loc, TlApplication::Callback callback2) {
        if (tlApp->impl_->mainWindow_ != nullptr) {
            func(loc, callback2);
        }
    };

    emit tlApp->sigRunInGuiThreadAsync(loc, func2, callback);
}

void TlApplication::runInGuiThreadAsync(const std::function<void(const TlApplication::Callback &callback)> &func,
                                        const TlApplication::Callback &callback) {
    runInGuiThreadAsync(LOC, [func](const Loc &loc, const TlApplication::Callback &callback) {
        loc.trace();
        func(callback);
    }, callback);
}

std::thread::id TlApplication::getGuiThreadId() {
    TlApplication *tlApp = TlApplication::instance();
    return tlApp->impl_->guiThreadId_;
}


void TlApplication::assertGuiThread() {
    if (std::this_thread::get_id() != TlApplication::getGuiThreadId()) {
        const std::string error = fmt::format("current thread {} is not gui thread", std::this_thread::get_id());
        LOG_ERROR(error);
        throw std::runtime_error(error);
    }
}


std::shared_ptr<QExThread> TlApplication::Impl::getOrStartNamedThread(const std::string &name) {
    std::shared_ptr<QExThread> ioThread;
    {
        std::unique_lock<hi_mutex> lock(namedThreadMutex_);
        auto itr = namedThreads_.find(name);
        if (itr != namedThreads_.end())
            return itr->second;

        ioThread = std::shared_ptr<QExThread>(new QExThread);
        namedThreads_.insert({ name, ioThread });
    }

    if(ioThread) {
        ioThread->start();
        ioThread->waitStarted();
    }
    return ioThread;
}

std::shared_ptr<QExThread> TlApplication::Impl::getNamedThread(const std::string &name) {
    std::unique_lock<hi_mutex> lock(namedThreadMutex_);
    auto itr = namedThreads_.find(name);
    if (itr != namedThreads_.end())
        return itr->second;

    return nullptr;
}


// IO线程相关
// （同步）在 io 线程中运行代码
void TlApplication::startNamedThread(const std::string &name) {
    TlApplication *tlApp = TlApplication::instance();
    tlApp->impl_->getOrStartNamedThread(name);
}

void TlApplication::Impl::stopNamedThread(const std::string &name) {
    std::shared_ptr<QExThread> ioThread;
    {
        std::unique_lock<hi_mutex> lock(namedThreadMutex_);
        auto itr = namedThreads_.find(name);
        if (itr != namedThreads_.end()) {
            ioThread = itr->second;
            namedThreads_.erase(itr);
        }
    }

    if(ioThread) {
        ioThread->stopThread();
    }
}

void TlApplication::stopNamedThread(const std::string &name) {
    TlApplication *tlApp = TlApplication::instance();
    tlApp->impl_->stopNamedThread(name);
}

void TlApplication::stopAllNamedThread() {
    TlApplication *tlApp = TlApplication::instance();
    std::map<std::string, std::shared_ptr<QExThread>> ioThreads;
    {
        std::unique_lock<hi_mutex> lock(tlApp->impl_->namedThreadMutex_);
        ioThreads = std::move(tlApp->impl_->namedThreads_);
        tlApp->impl_->namedThreads_.clear();
    }

    for (const auto &item : ioThreads) {
        const auto &ioThread = item.second;
        if (ioThread) {
            ioThread->stopThread();
        }
    }
}

std::shared_ptr<QExThread> TlApplication::namedThread(const std::string &name) {
    TlApplication *tlApp = TlApplication::instance();
    return tlApp->impl_->getNamedThread(name);
}

// 在IO线程中运行代码（根据isWait决定同步或异步）
Result TlApplication::runInNamedThread(const std::string &name, const Loc &loc, const std::function<void(const Loc &)> &func, bool isWait) {
    TlApplication *tlApp = TlApplication::instance();
    auto thread = tlApp->impl_->getOrStartNamedThread(name);
    if (thread) {
        thread->runInThread(loc, func, isWait);
        return Result::success();
    }
    else
        return Result::error("no io thread");
}

Result TlApplication::runInNamedThread(const std::string &name, const std::function<void()> &func, bool isWait) {
    TlApplication *tlApp = TlApplication::instance();
    auto thread = tlApp->impl_->getOrStartNamedThread(name);
    if (thread) {
        thread->runInThread(func, isWait);
        return Result::success();
    }
    else
        return Result::error("no io thread");
}

#define IO_THREAD_NAME "io"
// IO线程相关（兼容性保留，实际为一个名字为io的命名线程）  
void TlApplication::startIoThread() {
    startNamedThread(IO_THREAD_NAME);
}

void TlApplication::stopIoThread() {
    stopNamedThread(IO_THREAD_NAME);
}

std::shared_ptr<QExThread> TlApplication::ioThread() {
    return namedThread(IO_THREAD_NAME);
}


// 在IO线程中运行代码（根据isWait决定同步或异步）
Result TlApplication::runInIoThread(const Loc &loc, const std::function<void(const Loc &)> &func, bool isWait) {
    return runInNamedThread(IO_THREAD_NAME, loc, func, isWait);
}

Result TlApplication::runInIoThread(const std::function<void()> &func, bool isWait) {
    return runInNamedThread(IO_THREAD_NAME, func, isWait);
}

// 获取js引擎
JSEngineRuntime *TlApplication::jsEngineRuntime() {
    TlApplication *tlApp = TlApplication::instance();
    return tlApp->impl_->jsEngineRuntime_.get();
}

void TlApplication::initJsEngineRuntime() {
    TlApplication *tlApp = TlApplication::instance();
    if(!tlApp->impl_->jsEngineRuntime_)
        tlApp->impl_->jsEngineRuntime_ = std::make_unique<JSEngineRuntime>();
}


// 启动一个timer
std::weak_ptr<QExTimer> TlApplication::setTimeout(const Loc &loc, const std::function<void(const Loc &)> &cb, int ms, const std::string &type) {
    //LOG_INFO("setTimeout({})", ms);
    TlApplication *tlApp = TlApplication::instance();
    const auto &impl_ = tlApp->impl_;

    auto handler = std::make_shared<std::weak_ptr<QExTimer>>();
    *handler = QExTimer::setTimeout(
        [cb, handler](const Loc &loc) {
            TlApplication::clearTimeout(*handler);
            cb(loc);
        },
        type,
        ms,
        loc).lock();

    loc.trace();

    {
        auto lock = hi_unique_lock(impl_->timerMutex_);
        auto itr = impl_->timers_.find(type);
        if (itr == impl_->timers_.end()) {
            std::set<std::weak_ptr<QExTimer>, decltype(&CompareSmartPtr<QExTimer>)> timerSet(&CompareSmartPtr<QExTimer>);
            itr = impl_->timers_.emplace(type, std::move(timerSet)).first;
        }
        itr->second.insert(*handler);
        impl_->timerCond_.notify_all();
    }

    return *handler;
}

// 清除一个timer
void TlApplication::clearTimeout(const std::weak_ptr<QExTimer> &handler) {
    auto sHandler = handler.lock();
    if (sHandler) {
        QExTimer::clearTimeout(handler);

        TlApplication *tlApp = TlApplication::instance();
        const auto &impl_ = tlApp->impl_;

        auto lock = hi_unique_lock(impl_->timerMutex_);
        auto itr = impl_->timers_.find(sHandler->type());
        if (itr != impl_->timers_.end()) {
            itr->second.erase(sHandler);
            if (itr->second.size() == 0)
                impl_->timers_.erase(itr);
        }

        impl_->timerCond_.notify_all();
    }
}

// 清除所有的timer
void TlApplication::clearTimeoutAll(const std::string &type) {
    TlApplication *tlApp = TlApplication::instance();
    const auto &impl_ = tlApp->impl_;

    std::list<std::weak_ptr<QExTimer>> handlers;
    {
        auto lock = hi_unique_lock(impl_->timerMutex_);
        auto itr = impl_->timers_.find(type);
        if (itr != impl_->timers_.end()) {
            const auto &timerSet = itr->second;
            for (const auto &time : timerSet)
                handlers.push_back(time);
        }
    }

    for (const std::weak_ptr<QExTimer> &handler : handlers) {
        clearTimeout(handler);
    }
}

// （其他线程，阻塞消息循环的）等待本线程所有timer完成
void TlApplication::waitTimersFinished(__qsync__, const std::string &type) {
    TlApplication *tlApp = TlApplication::instance();
    const auto &impl_ = tlApp->impl_;

    auto timerIsEmpty = [type, &impl_]() {
        auto itr = impl_->timers_.find(type);
        if (itr == impl_->timers_.end())
            return true;
        if (itr->second.size() == 0)
            return true;
        return false;
    };

    if (std::this_thread::get_id() == TlApplication::getGuiThreadId()) {
        // 本线程
        while (!timerIsEmpty()) {
            QCoreApplication::processEvents();
        }
    }
    else {
        // 其他线程, 可能要改  
        auto lock = hi_shared_lock(impl_->timerMutex_);
        while (!timerIsEmpty()) {
            impl_->timerCond_.wait(lock);
        }
    }
}


// 设定主窗口
QWidget *TlApplication::mainWindow() {
    TlApplication *tlApp = TlApplication::instance();
    if (tlApp == nullptr) return nullptr;
    const auto &impl_ = tlApp->impl_;
    return impl_->mainWindow_;
}

void TlApplication::setMainWindow(QWidget *widget) {
    TlApplication *tlApp = TlApplication::instance();
    const auto &impl_ = tlApp->impl_;
    impl_->mainWindow_ = widget;
}

// 设定是否在主窗口初始化完成状态状态
bool TlApplication::isMainWindowReady() {
    TlApplication *tlApp = TlApplication::instance();
    if (tlApp == nullptr) return false;
    const auto &impl_ = tlApp->impl_;
    return impl_->isMainWindowReady_;
}

void TlApplication::setMainWindowReady(bool isRready) {
    TlApplication *tlApp = TlApplication::instance();
    const auto &impl_ = tlApp->impl_;
    impl_->isMainWindowReady_ = isRready;
}

// 获取（或新增）状态显示QLabel，多个状态栏位，将根据name排序, timtoutMs = 0是不用超时
void TlApplication::showStatusbar(const QString &name,
    const QPixmap &icon,
    const QString &text,
    int timeoutMs) {
    QWidget *mainWindow = TlApplication::mainWindow();
    if (mainWindow) {
        QMetaObject::invokeMethod(mainWindow, "showStatusbar",
            QArgument("QString", name),
            QArgument("QPixmap", icon),
            QArgument("QString", text),
            QArgument("int", timeoutMs));
    }
}

void TlApplication::hideStatusbar(const QString &name) {
    QWidget *mainWindow = TlApplication::mainWindow();
    if (mainWindow) {
        QMetaObject::invokeMethod(mainWindow, "hideStatusbar",
            QArgument("QString", name));
    }
}


QString TlApplication::Impl::getRunFile() const {
    return getAppDataPath("tlvision2.run");
}

QString TlApplication::Impl::getCloseFile() const {
    return getAppDataPath("tlvision2.close");
}

// 记录运行文件
Result TlApplication::savePidFile() {
    TlApplication *tlApp = TlApplication::instance();
    QString path = tlApp->impl_->getRunFile();

    std::string str = fmt::format("{}", TlApplication::getGuiThreadId());
    const Result &result = writeTextFile(path, QString::fromStdString(str));
    if (!result) return result;

    QString path2 = tlApp->impl_->getCloseFile();
    QFile::remove(path2);

#ifdef _WIN32
    tlApp->impl_->lockFile_ = std::unique_ptr<QLockFile>(new QLockFile(path));
    if (!tlApp->impl_->lockFile_->tryLock()) {
        return Result::error("can not lock pid file: {}", path);
    }
#endif

    return Result::success();
}


// 记录运行文件兼退出标志（这种情况下，忽略后续异常自动的重启）  
Result TlApplication::saveClosePidFile() {
    TlApplication *tlApp = TlApplication::instance();
    QString path = tlApp->impl_->getCloseFile();

    std::string str = fmt::format("{}", TlApplication::getGuiThreadId());
    return writeTextFile(path, QString::fromStdString(str));
}


// 删除运行文件
void TlApplication::removePidFile() {
    TlApplication *tlApp = TlApplication::instance();
    QString path = tlApp->impl_->getRunFile();

    if (tlApp->impl_->lockFile_) {
        tlApp->impl_->lockFile_->unlock();
        tlApp->impl_->lockFile_.release();
    }

    QFile::remove(path);

    QString path2 = tlApp->impl_->getCloseFile();
    QFile::remove(path2);
}

// 判断运行文件是否存在（非空说明上次运行出现了异常）
std::string TlApplication::getLastPidFromFile() {
    TlApplication *tlApp = TlApplication::instance();
    QString path = tlApp->impl_->getRunFile();

    ResultEx<QString> res = readTextFile(path);
    if (!res) return "";
    else return res.object_.toStdString();
}

// 获取当前app对象  
TlApplication *TlApplication::instance() {
    TlApplication *tlApp = static_cast<TlApplication *>(QCoreApplication::instance());
    return tlApp;
}
