#include "rdesktop/rd_server.h"
#include "mainwindow.h"
#include "app_config.h"
#include "archive_config.h"
#include "res_monitor.h"
#include "utils/qt_utils.h"
#include "jsengine/utils.h"
#include "utils/windump.h"
#include "utils/api_trace.h"
#include "app/host_console.h"
#include "app/host_module.h"
#include "app/flow_runner.h"
#include "utils/singleton.h"
#include "utils/log.h"
#include "utils/thread_pool.h"
#include "utils/db.h"
#include "co_core/co_qt.h"
#include "spdlog/async.h"
#include "spdlog/sinks/base_sink.h"
#include "spdlog/sinks/base_sink-inl.h"
#include "spdlog/sinks/rotating_file_sink.h"
#include "utils/log_sink/tl_stdout_color_sinks.h"
#include "rdesktop/paint_event_filter.h"
#include "promise-cpp/add_ons/qt/promise_qt.hpp"
#include "devices/io_service.h"
#include "ui/tl_application.h"
#include "hmi_dll/main.h"
#include "cmd_arg.h"
#include "utils/watch_dog.h"

#include <QApplication>
#include <QTranslator>
#include <QLocale>
#include <QStyleFactory>
#include <QDebug>
#include <QQuickWindow>
#include <QDir>
#include <QQmlEngine>
#include <QQuickStyle>
#include <QMessageBox>
#include <QStandardPaths>
#include <QLibraryInfo>
#include <QProcessEnvironment>
#include "../splash_screen/proc_notifier.h"

#include <cstdlib>  // for getenv_s and _putenv_s
#if defined _WIN32
#include <windows.h>
#endif

#if defined _MSC_VER
#include <crtdbg.h>
#endif
#if defined __unix__ || defined __linux__ || defined __APPLE__
#include <unistd.h>
#endif

// 测试函数入口
void testMain();

// 修正QT不同版本的分辨率、字体等问题
static void qtFixEnv() {
    // 高分辨率屏幕设置
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    // qputenv("QT_AUTO_SCREEN_SCALE_FACTOR", "1");
    // qputenv("QT_SCALE_FACTOR", "0.75");
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling, true);
#endif

    // 防止QML控件字体发虚
    qputenv("QML_DISABLE_DISTANCEFIELD", "1");
#if QT_VERSION >= QT_VERSION_CHECK(5, 10, 0)
    QQuickWindow::setTextRenderType(QQuickWindow::NativeTextRendering);
#endif

    // 防止QT6 qml窗口crash (why？)
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    QQuickWindow::setGraphicsApi(QSGRendererInterface::GraphicsApi::Software);
#elif  QT_VERSION >= QT_VERSION_CHECK(5, 8, 0)
    QQuickWindow::setSceneGraphBackend(QSGRendererInterface::GraphicsApi::Software);
#endif

    // QT的线程池管理似乎有问题，然后导致QPixmap并行转换时出不来  
    //   https://forum.qt.io/topic/136606/app-hangs-in-qpixmap-fromimage/2  
    // 禁用线程池超时可能能解决此问题  
#if 1
    QThreadPool *threadPool = QThreadPool::globalInstance();
    if (threadPool) {
        threadPool->setExpiryTimeout(-1);
    }
#endif
}


void myAppLoadTranslation() {

    

    const QLocale &locale = QLocale::system();

    static QTranslator qtTranslator;
    qApplyTranslator(qtTranslator, "qt_", QLibraryInfo::location(QLibraryInfo::TranslationsPath));

    static QTranslator qtBaseTranslator;
    qApplyTranslator(qtBaseTranslator, "qtbase_", QLibraryInfo::location(QLibraryInfo::TranslationsPath));

    static QTranslator myAppTranslator;
    qApplyTranslator(myAppTranslator, "gui_", ":/i18n");
}

void setSymbolEnvPath(const QStringList &symbolPaths) {

    QStringList paths;
    for (auto &symbolPath : symbolPaths) {
        //QString path = QDir(symbolPath).absoluteFilePath("");
        //path = QDir::toNativeSeparators(path);
        QString path = QDir::toNativeSeparators(symbolPath);
        LOG_INFO("_NT_SYMBOL_PATH: append path = {}", path);
        paths.push_back(path);
    }

    //QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    //QString pathValue = env.value("_NT_SYMBOL_PATH");

    char path[MAX_PATH] = { 0 };
    size_t pathSize = 0;
    getenv_s(&pathSize, path, sizeof(path), "_NT_SYMBOL_PATH");
    QString pathValue = QString::fromLocal8Bit(path);

    if (pathValue.isEmpty() || pathValue.endsWith(";")) {
        pathValue += paths.join(";");
    }
    else {
        pathValue += ";" + paths.join(";");
    }
    //env.insert("_NT_SYMBOL_PATH", pathValue);
    //QString path2 = env.value("_NT_SYMBOL_PATH");
    //LOG_WARN("path2 = {}", path2);

    _putenv_s("_NT_SYMBOL_PATH", pathValue.toLocal8Bit().data());

    const char *path3 = getenv("_NT_SYMBOL_PATH");
    LOG_INFO("_NT_SYMBOL_PATH = {}", path3);
}

void setSymbolPath() {
    // 这是为其他机器设定的符号路径，本机怎么都找得到  

    std::function<void(QStringList &, const QString &)> findSubDirs = [&](QStringList &symbolPaths, const QString &dirPath) {
        // 创建 QDir 对象
        QDir dir(dirPath);
        if (!dir.exists()) return;

        // 设置过滤器，仅获取子目录
        dir.setFilter(QDir::AllEntries | QDir::NoDotAndDotDot);

        // 获取子目录列表
        QFileInfoList subDirs = dir.entryInfoList();

        // 遍历子目录列表  
        bool isPdbDir = false;
        for (const QFileInfo &info : subDirs) {
            if (info.isDir()) {
                // 递归查找子目录  
                findSubDirs(symbolPaths, info.absoluteFilePath());
            }
            else if (!isPdbDir && info.isFile() && info.suffix() == "pdb") {
                isPdbDir = true;
            }
        }

        if (isPdbDir) {
            symbolPaths.push_back(dirPath);
        }
    };

    QDir appDir(qApp->applicationDirPath());

    QStringList symbolPaths;
    symbolPaths.push_back(appDir.absoluteFilePath(""));
    findSubDirs(symbolPaths, appDir.absoluteFilePath("pdb"));

    setSymbolEnvPath(symbolPaths);
}

// 初始化OS相关代码
static void osInit(int argc, char *argv[]) {
#if defined _MSC_VER
    // 检查是否有内存泄露
    _CrtSetDbgFlag(_CRTDBG_LEAK_CHECK_DF | _CRTDBG_ALLOC_MEM_DF);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);

    // Use _CrtSetBreakAlloc(n) to find a specific memory leak
#endif

#if defined __unix__ || defined __linux__ || defined __APPLE__
    // 设定 LD_LIBRARY_PATH，好像没啥用？
    const char *oldLdLibraryPath = getenv("LD_LIBRARY_PATH");
    QString applicationDirPath = QApplication::applicationDirPath();
    QDir dir(applicationDirPath);
    dir.cdUp();
    QString libPath = dir.absoluteFilePath("lib");

    std::string sNewLdLibraryPath;
    if (oldLdLibraryPath == nullptr || strcmp(oldLdLibraryPath, "") == 0) {
        sNewLdLibraryPath = QString("%1:%2")
                .arg(applicationDirPath).arg(libPath)
                .toStdString();
    }
    else {
        sNewLdLibraryPath = QString("%1:%2:%3")
                .arg(oldLdLibraryPath).arg(applicationDirPath).arg(libPath)
                .toStdString();
    }
    const char *newLdLibraryPath = sNewLdLibraryPath.c_str();
    qDebug() << QString("newLdLibraryPath = %1").arg(newLdLibraryPath);
    setenv("LD_LIBRARY_PATH", newLdLibraryPath, 1);
#endif

    // 是否显示控制台窗口
    bool useConsole = true;
    for (int i = 0; i < argc; ++i) {
        if (strcmp(argv[i], "--no-console") == 0)
            useConsole = false;
    }
    ((TlApplication *)qApp)->useConsole_ = useConsole;

#if defined _WIN32
    if (useConsole) {
        if (!AttachConsole(ATTACH_PARENT_PROCESS)) {
            AllocConsole();
        }
        freopen("CONIN$", "r+t", stdin);
        freopen("CONOUT$", "w+t", stdout);
        freopen("CONOUT$", "w+t", stderr);
    }

    //setSymbolPath();

    hi_set_timeout_dump_fn([] { writeMiniDump("lock.dmp"); });
#endif

#if defined __unix__ || defined __linux__ || defined __APPLE__
    if (useConsole && !isatty(STDIN_FILENO)) {
        std::vector<char *> newArgv;
        newArgv.resize(static_cast<size_t>(argc + 3));
        newArgv[0] = const_cast<char *>("/usr/bin/x-terminal-emulator");
        newArgv[1] = const_cast<char *>("-e");
        for(int i = 0; i < argc; ++i)
            newArgv[i + 2] = argv[i];
        newArgv.back() = nullptr;
        // 替换应用程序，如果失败，继续运行
        execvp(newArgv[0], newArgv.data());
    }
#endif

} 


// 创建重要日志文件（少量，但是很重要的）  
static int importantLogInit() {
    std::vector<spdlog::sink_ptr> sinks;
    AppConfig &appConfig = Singleton<AppConfig>::getInstance();

#if defined NDEBUG
    const std::string &logPath = "tlvision_em.log";
#else
    const std::string &logPath = "tlvision_em_d.log";
#endif
    QString logFile = getLogDataPath(QString::fromStdString(logPath));
    try {
        sinks.push_back(std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
#ifdef _WIN32
            std::string(logFile.toLocal8Bit()),
#else
            emLogFile.toStdString(),
#endif
            appConfig.maxRotateFileSize_,
            appConfig.maxRotateCount_,
            false));
    }
    catch (const spdlog::spdlog_ex &ex) {
        QString errStr = QString::fromStdString(ex.what());
        QString msg = QObject::tr("Can not create log file\nProgram exit...\n\nPlease zip this folder and send to R&D for further research: \n%1\n\nReason: %2")
            .arg(logFile).arg(errStr);
        QMessageBox mbox(
            QMessageBox::Icon::Critical,
            qApp->applicationName(),
            msg,
            QMessageBox::StandardButton::Ok);
        mbox.setWindowFlag(Qt::WindowStaysOnTopHint, true);
        mbox.exec();
        return EXIT_FAILURE;
    }

    // spdlog 自定义输出器
    std::shared_ptr<spdlog::logger> combinedLogger;
    if (appConfig.useSynchronizedLog_) {
        // 创建同步日志
        combinedLogger = std::make_shared<spdlog::logger>("tlvision2_em", begin(sinks), end(sinks));
    }
    else {
        // 创建异步日志
        combinedLogger = std::make_shared<spdlog::async_logger>("tlvision2_em",
            begin(sinks),
            end(sinks),
            spdlog::thread_pool(),
            spdlog::async_overflow_policy::block);
    }

    spdlog::register_logger(combinedLogger);
    return EXIT_SUCCESS;
}

// 默认的日志输出  
static int defaultLogInit() {
    std::vector<spdlog::sink_ptr> sinks;
    AppConfig &appConfig = Singleton<AppConfig>::getInstance();

    if (((TlApplication *)qApp)->useConsole_) {
        // 创建控制台输出log
        sinks.push_back(std::make_shared<spdlog::sinks::tl_stdout_color_sink_mt>());
    }

#if 0
    // 删除旧的日志  
    const size_t MAX_ROTATE_PROCESS = 99;

    const QFileInfoList &fileInfoList = QDir(logDir).entryInfoList();
    struct FileInfoListByProcess {
        QDateTime lastModified_;
        std::string processId_;
        std::list<std::pair<int, QFileInfo>> fileInfoList_;
    };
    std::map<std::string, FileInfoListByProcess> fileInfoListByProcess;
    for (const QFileInfo &fileInfo : fileInfoList) {
        // 按照进程分类
        if (fileInfo.isFile()
            && fileInfo.fileName().startsWith("tlvision")
            && fileInfo.fileName().endsWith(".log")) {
            const std::vector<std::string> &strs = strSplit(fileInfo.fileName().toStdString(), ".");
            std::string processId;
            int rotateId;
            if (strs.size() == 2) {
                processId = strs[0];
                rotateId = -1;
            }
            else if (strs.size() == 3) {
                processId = strs[0];
                rotateId = std::stoi(strs[1]);
            }
            auto found = fileInfoListByProcess.find(processId);
            if (found == fileInfoListByProcess.end()) {
                found = fileInfoListByProcess.insert({ processId, FileInfoListByProcess() }).first;
                found->second.lastModified_ = fileInfo.lastModified();
                found->second.processId_ = processId;
            }
            else if (found->second.lastModified_ < fileInfo.lastModified()) {
                found->second.lastModified_ = fileInfo.lastModified();
            }
            std::pair<int, QFileInfo> info{ rotateId , fileInfo };
            found->second.fileInfoList_.push_back(info);
            //printf("%s, %d\n", fileInfo.fileName().toStdString().c_str(), (int)fileInfo.isFile());
        }
    }
    // 按照最后更新排序
    std::map<QDateTime, FileInfoListByProcess> fileInfoListByLastModified;
    for (const auto &item : fileInfoListByProcess) {
        fileInfoListByLastModified.insert({ item.second.lastModified_, item.second });
    }
    // 删除旧的
    //printf("fileInfoListByLastModified.size() = %d\n", (int)fileInfoListByLastModified.size());
    while (fileInfoListByLastModified.size() > MAX_ROTATE_PROCESS) {
        const auto &toBeDeletedFiles = fileInfoListByLastModified.begin()->second.fileInfoList_;
        for (const auto &toBeDeletedFile : toBeDeletedFiles) {
            const QFileInfo &fileInfo = toBeDeletedFile.second;
            QFile(fileInfo.absoluteFilePath()).remove();
        }
        fileInfoListByLastModified.erase(fileInfoListByLastModified.begin());
    }

    const std::string &logPath = fmt::format("tlvision{}.log", TlApplication::getGuiThreadId());
#elif defined NDEBUG
    const std::string &logPath = "tlvision.log";
#else
    const std::string &logPath = "tlvision_d.log";
#endif

    // 创建滚动文件  
    QString logFile = getLogDataPath(QString::fromStdString(logPath));
    try {
        sinks.push_back(std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
#ifdef _WIN32
            std::string(logFile.toLocal8Bit()),
#else
            logFile.toStdString(),
#endif
            appConfig.maxRotateFileSize_,
            appConfig.maxRotateCount_,
            false));
    }
    catch (const spdlog::spdlog_ex &ex) {
        QString errStr = QString::fromStdString(ex.what());
        QString msg = QObject::tr("Can not create log file\nProgram exit...\n\nPlease zip this folder and send to R&D for further research: \n%1\n\nReason: %2")
            .arg(logFile).arg(errStr);
        QMessageBox mbox(
            QMessageBox::Icon::Critical,
            qApp->applicationName(),
            msg,
            QMessageBox::StandardButton::Ok);
        mbox.setWindowFlag(Qt::WindowStaysOnTopHint, true);
        mbox.exec();
        return EXIT_FAILURE;
    }

    // spdlog 自定义输出器
    struct SharedHostConsoleSink : public spdlog::sinks::sink {
    public:
        SharedHostConsoleSink()
            : sink_(&Singleton<HostConsoleSink>::getInstance()) {
        }
        ~SharedHostConsoleSink() override {};
    protected:
        void log(const spdlog::details::log_msg &msg) override {
            sink_->log(msg);
        }
        void flush() override {
            sink_->flush();
        }
        void set_pattern(const std::string &pattern) override {
            sink_->set_pattern(pattern);
        }
        void set_formatter(std::unique_ptr<spdlog::formatter> sink_formatter) override {
            sink_->set_formatter(std::move(sink_formatter));
        }
    private:
        HostConsoleSink *sink_;
    };
    if (((TlApplication *)qApp)->useConsole_) {
        sinks.push_back(std::make_shared<SharedHostConsoleSink>());
    }

    std::shared_ptr<spdlog::logger> combinedLogger;
    if (appConfig.useSynchronizedLog_) {
        // 创建同步日志
        combinedLogger = std::make_shared<spdlog::logger>("tlvision2", begin(sinks), end(sinks));
    }
    else {
        // 创建异步日志
        combinedLogger = std::make_shared<spdlog::async_logger>("tlvision2",
            begin(sinks),
            end(sinks),
            spdlog::thread_pool(),
            spdlog::async_overflow_policy::block);
    }

    //register it if you need to access it globally
    //spdlog::register_logger(combinedLogger);
    spdlog::set_default_logger(combinedLogger);
    spdlog::flush_on(spdlog::level::info);
    spdlog::flush_every(std::chrono::seconds(10));
    //spdlog::set_pattern("%^[%Y-%m-%dT%T.%f,%L,%t,%s:%#:%!]%$ %v");
    spdlog::set_pattern("%^[%Y-%m-%dT%T.%f,%L,%t,%s:%#]%$ %v");
#ifdef NDEBUG
    //spdlog::set_pattern("%^[%Y-%m-%dT%T.%e,%L,%t]%$ %v");
#endif

    spdlog::set_level(spdlog::level::info);
    spdlog::cfg::load_env_levels();

    // 打印输出测试   
    NAMED_LOG_INFO("tlvision2_em", "Program started ...\n");
    LOG_INFO("Program started ...\n");
    LOG_INFO("\xE7\xA8\x8B\xE5\xBA\x8F\xE5\x90\xAF\xE5\x8A\xA8 ...\n"); //程序启动UTF8编码，控制台不应显示乱码  

    QString version = AboutDialog::getVerStr(true);
    LOG_INFO("{}", version);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    LOG_INFO("QT version: {}, for windows 7 or later", QT_VERSION_STR);
#else
    LOG_INFO("QT version: {}, for windows 10 or later", QT_VERSION_STR);
#endif

    spdlog::set_level(spdlog::level::from_str(appConfig.logLevel_.selectedText().c_str()));
    return EXIT_SUCCESS;
}

// 初始化消息输出
static int msgInit() {

    // 初始化消息输出
    HostConsole::installConvertorForHost();

    // 初始化日志系统
    const QString &logDir = getLogDataPath("");
    QDir().mkpath(logDir);

    AppConfig &appConfig = Singleton<AppConfig>::getInstance();
    if (!appConfig.useSynchronizedLog_) {
        spdlog::init_thread_pool(1024 * 1024, 1); // queue with 1M items and 1 backing thread.
    }

    int ret = importantLogInit();
    if (ret != EXIT_SUCCESS)
        return EXIT_SUCCESS;

    ret = defaultLogInit();

    return ret;
}


static void dbInit() {
    Db &db = Singleton<Db>::getInstance();
    QString dbFile = getAppDataPath("tlvision2.db");
    Result res = db.init(dbFile.toStdString());
    if (!res) throw std::runtime_error(res.message_);
    //res = db.log("system", "info", QObject::tr("application start to run").toStdString());
    //if (!res) throw std::runtime_error(res.message_);
}


static void objIdInit() {
    ObjId::initCategory({
        { typeid(Project), "project" },
        { typeid(Scene), "scene" },
        { typeid(Flow), "flow" },
        { typeid(Task), "task" },
        { typeid(Widget), "widget" },
        { typeid(ConnectLine), "connectLine" }
    });
}


static void uncaughtPromiseExceptionHandler(promise::Promise &promise) {
    promise.fail(PM_LOC, [](const std::runtime_error &err) {
        LOG_ERROR("onUncaughtException {}", err.what());
    }).fail(PM_LOC, [](const cv::Exception &err) {
        LOG_ERROR("{}", err.what());
    }).fail(PM_LOC, [](const std::string &err) {
        LOG_ERROR("{}", err);
    }).fail(PM_LOC, [](const int &err) {
        LOG_ERROR("{}", err);
    }).fail(PM_LOC, []() {
        //go here for all other uncaught parameters.
        // 未知的错误，通常为未处理的 promise reject 对象，不当作错误显示
        LOG_DEBUG("onUncaughtException");
    });
}

void forceTerminateProcess(int code) {
#ifdef _WIN32
    HANDLE hnd;
    DWORD pid = GetCurrentProcessId();
    hnd = OpenProcess(SYNCHRONIZE | PROCESS_TERMINATE, TRUE, pid);
    TerminateProcess(hnd, (UINT)code);
    ExitProcess((UINT)code);
#else
    exit(0);
#endif
}

void usePerformaceCoreOnly();

#if defined _WIN32
int __stdcall WinMain(
    HINSTANCE hInstance,
    HINSTANCE /*hPrevInstance*/,
    LPSTR     lpCmdLine,
    int       /*nShowCmd*/
)  {
    int argc = __argc;
    char **argv = __argv;
#else
int main(int argc, char *argv[])  {
#endif

    // <0> 设置生成dump文件和api debug相关函数
    initDumpHandler();
    ApiTrace apiTrace;

    // <1> 修正QT不同版本的分辨率、字体等问题
    qtFixEnv();

    // <2> 创建QT应用程序对象
    std::srand(time(0));
    TlApplication app(argc, argv);
#if !defined _WIN32
    app.setWindowIcon(QIcon(":/icons/tlalign.png"));
#endif
    app.setApplicationName("TlVision2");
    app.setApplicationDisplayName("");
    usePerformaceCoreOnly();

    // 当前目录设置
    //QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QString appData = getAppInstallPath() + QDir::separator() + "data";
    QDir::setCurrent(appData);

    // <3> 设定翻译文件
    myAppLoadTranslation();

    // <4> 初始化信息输出和调试相关代码  
    osInit(argc, argv);
    if (!singleton<CmdArg>().init(argc, argv)) {
        return EXIT_FAILURE;
    }

    AppConfig &appConfig = Singleton<AppConfig>::getInstance();
    appConfig.load();
    ArchiveConfig& archiveConfig = Singleton<ArchiveConfig>::getInstance();
    archiveConfig.load();
    FlowRunner::setSyncGuiEnabled(appConfig.flowWaitGui_);
    appConfig.updateRunPrivilege();

    // <5> 判断上次运行是否正常，记录本次运行pid
    bool isLastRunOk = TlApplication::getLastPidFromFile().empty();
    const Result &pidResult = TlApplication::savePidFile();
    if (!pidResult) {
        QString msg = QObject::tr("Data file not writtable, probably previous progam is running!");
        QMessageBox mbox(
            QMessageBox::Icon::Critical,
            qApp->applicationName(),
            msg,
            QMessageBox::StandardButton::Ok);
        mbox.setWindowFlag(Qt::WindowStaysOnTopHint, true);
        mbox.exec();
        return EXIT_SUCCESS; // 这种情况看作已经成功运行，维护线程不会再次拉起
    }

    // <6> 初始化日志输出
    int ret = msgInit();
    if (ret != EXIT_SUCCESS)
        return EXIT_SUCCESS; // 这种情况看作已经成功运行，维护线程不会再次拉起

    // 设定pdb查找路径  
    setSymbolPath();

    QPixmap p(QSize(1, 1));
    fmt::print("dpi = {} {}, {} {}\n", p.physicalDpiX(), p.physicalDpiY(), p.logicalDpiX(), p.logicalDpiY());
    fmt::print("devicePixelRatio = {}\n", app.devicePixelRatio());

    std::cout << "0x" << std::hex << (size_t)std::shared_ptr<int>(new int).get() << "\n";
    // （有意的）内存泄露验证，内存测试工具需输出1字节泄露
    new char;

    // <7> 设定组件dll的翻译文件
    coreDllLoadTranslation(); // 加载core dll中的翻译
    hmiDllLoadTranslation();  // 加载hmi dll中的翻译

    // <8> 初始化程序数据库
    dbInit();

    // <9> 设定窗口风格
#if 1   // 还不如默认风格...
    qInfo() << QStyleFactory::keys();
#if defined __APPLE__
    app.setStyle(QStyleFactory::create("Fusion"));
#endif
#endif


    // 设定qml窗口风格
#if defined _WIN32
    QQuickStyle::setStyle("Windows");
#endif

    // <10> 一个测试（可以去除）
    testMain();

    // <11> 注册自定义的对象类型
    qRegisterMetaType<Loc>("Loc");
    qRegisterMetaType<std::function<void(Loc)>>("std::function<void(Loc)>");
    qRegisterMetaType<Image>("Image");
    qRegisterMetaType<RepaintWidget>("RepaintWidget");
    qRegisterMetaType<VStrSet>("VStrSet");
    qRegisterMetaType<std::shared_ptr<Widget>>("std::share_ptr<Widget>");
    qRegisterMetaType<std::shared_ptr<QWidget *>>("std::share_ptr<QWidget *>");
    qRegisterMetaType<std::function<void()>>("std::function<void()>");
    // qml控件中
    qmlRegisterSingletonType("TlMod", 0, 0, "Mod", [](QQmlEngine *qmlEngine, QJSEngine *jsEngine){
        fmt::print("qmlEngine = {}, jsEngine = {}\n", qmlEngine, jsEngine);
        return jsInitQmlEngine(jsEngine);
    });
    // 初始化对象ID  
    objIdInit();

    // <12> 维护父进程通知器（通知splash screen线程本程序运行状态）
    struct mutex_remove
    {
        mutex_remove() { boost::interprocess::named_mutex::remove(PROC_STATUS_INTERPROCESS_NAME); }
        ~mutex_remove() { boost::interprocess::named_mutex::remove(PROC_STATUS_INTERPROCESS_NAME); }
    } remover;

    // <13> 启动io线程
    TlApplication::startIoThread();

    // <14> 全局窗口更新拦截器和远程访问服务
    CurrentUser &currentUser = const_cast<CurrentUser &>(User::getCurrentUser());
    app.installEventFilter(&currentUser);
    Singleton<RdServer>::getInstance().start();
//#define ENABLE_RDESKTOP
#ifdef ENABLE_RDESKTOP
    PaintEventFilter &paintEventFilter = Singleton<PaintEventFilter>::getInstance();
    app.installEventFilter(&paintEventFilter);
#endif
    promise::handleUncaughtException(uncaughtPromiseExceptionHandler);
    promise::PromiseEventFilter &promiseEventFilter = promise::PromiseEventFilter::getSingleInstance();
    app.installEventFilter(&promiseEventFilter);

    // <15> 启动资源监视线程
    ResMonitor &resMonitor = singleton<ResMonitor>();
    resMonitor.start();

    // 退出流程
    std::atomic<bool> mainCleared{ false };
    auto mainClear = [&] {
        if (mainCleared.exchange(true))
            return;

        // <18> 监控主界面线程僵死  
        // watchDogStop();
        // watchDogStart 已经移动到加载dll之后，不然会卡死
        
        // <19> 停止cpp20协程支持
        coUnInit();
        // <20> 停止资源监视线程
        resMonitor.stop();

        // <21> 停止远程访问服务
        app.removeEventFilter(&promiseEventFilter);
        Singleton<RdServer>::getInstance().stop();
        app.removeEventFilter(&currentUser);

        // <22> 确保已经停止所有所有通讯设备服务，并停止io线程
        Singleton<IoService>::getInstance().checkUnremovedDevices();
        TlApplication::stopAllNamedThread();

        // <23> 先保存一遍配置
        appConfig.save();

        // <24> 放到线程运行（第3方dll可能有意外不退出情况）
        std::thread([] {
            __qsync__;
            // <24.1> 等待所有线程退出
            Singleton<ThreadPool>::getInstance().stopEx(__qwait__);

            // <24.2> 关闭配置文件和数组库
            AppConfig &appConfig = Singleton<AppConfig>::getInstance();
            appConfig.save();
            Db &db = Singleton<Db>::getInstance();
            db.close();

            // <24.3> 成功退出主流程，删除本次运行pid
            TlApplication::removePidFile();

            forceTerminateProcess(0);
        }).detach();

        // <25> N秒后未关闭完成的提示
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        std::shared_ptr<int> waitSec = std::make_shared<int>(5);
        QMessageBox *mbox = new QMessageBox;
        mbox->setWindowTitle(MainWindow::tr("wait close"));
        mbox->setStandardButtons(QMessageBox::StandardButton::NoButton);
        mbox->setText(MainWindow::tr("Wait application to close (%1) ...").arg(*waitSec));

        std::function<void()> update = [&] {
            QTimer::singleShot(1000, mbox, [=] {
                if (*waitSec > 0) {
                    -- *waitSec;
                    mbox->setText(MainWindow::tr("Wait application to close (%1) ...").arg(*waitSec));
                    update();
                }
                else {
                    mbox->accept();
                    Db &db = Singleton<Db>::getInstance();
                    db.close();
                    forceTerminateProcess(0);
                }
            });
        };
        update();
        mbox->exec();
    };

    // <16> 启动主窗口（可设定CHECK_LOOP，运行n次检查内存泄露状况)
    coInit(); // 启动cpp20协程支持

    // <17> 监控主界面线程僵死  
    // watchDogStart 已经移动到加载dll之后，不然会卡死

#define CHECK_LOOP 1
//#define CHECK_LOOP 100000   //to check memory leak
    for (int n = 0; n < CHECK_LOOP; ++n) {
        if ((n + 5) % 10 == 0) {
            fmt::print("add break pointer here to check memory\n");
        }
        MainWindow *mainWindow = new MainWindow((CHECK_LOOP <= 1 ? 0 : 100), isLastRunOk, mainClear);
        MainWindow::connect(mainWindow, &MainWindow::destroyed, [&] {
            mainWindow = nullptr;
        });

#ifdef ENABLE_RDESKTOP
        auto future = paintEventFilter.initThread(&w);
#endif
        mainWindow->show();
        ret = app.exec();
#ifdef ENABLE_RDESKTOP
        paintEventFilter.closeThread(future);
#endif

        if (mainWindow != nullptr) {
            PTI;
            delete mainWindow;
        }

        isLastRunOk = true;
    }

#ifdef ENABLE_RDESKTOP
#endif
    mainClear();

    return ret;
}
//catch (const std::exception &ex) {
//    fmt::print("error caught - {}\n", ex.what());
//    return EXIT_FAILURE;
//}
//catch (...) {
//    fmt::print("error caught!\n");
//    return EXIT_FAILURE;
//}
