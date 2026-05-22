#include <QCommandLineParser>
#include <QDir>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QScreen>
#include <QString>
#include <QtGlobal>

#include "configloader.h"
#include "maskcontroller.h"
#include "sysdata.h"

#ifdef HAVE_LAYER_SHELL
#include <LayerShellQt/Shell>
#include <LayerShellQt/Window>
#endif

static QString resolveConfigPath(const QString& cli) {
    if (!cli.isEmpty()) return cli;
    const QString env = qEnvironmentVariable("DEPARTURE_HUD_CONFIG");
    if (!env.isEmpty()) return env;
    QString xdg = qEnvironmentVariable("XDG_CONFIG_HOME");
    if (xdg.isEmpty()) xdg = QDir::homePath() + "/.config";
    return xdg + "/departure-hud/config.json";
}

static QScreen* pickScreen(const QString& name) {
    if (name.trimmed().isEmpty()) return QGuiApplication::primaryScreen();
    for (QScreen* s : QGuiApplication::screens()) {
        if (s->name() == name) return s;
    }
    return QGuiApplication::primaryScreen();
}

#ifdef HAVE_LAYER_SHELL
static void configureLayerShell(QQuickWindow* w, const QVariantMap& s) {
    using LSW = LayerShellQt::Window;
    LSW* ls = LSW::get(w);
    if (!ls) return;

    const QString layer = s.value("layer").toString().toLower();
    if      (layer == "background") ls->setLayer(LSW::LayerBackground);
    else if (layer == "bottom")     ls->setLayer(LSW::LayerBottom);
    else if (layer == "top")        ls->setLayer(LSW::LayerTop);
    else                            ls->setLayer(LSW::LayerOverlay);

    ls->setAnchors(LSW::Anchors(LSW::AnchorTop)
                 | LSW::AnchorBottom
                 | LSW::AnchorLeft
                 | LSW::AnchorRight);
    ls->setScope(QStringLiteral("departure-hud"));
    ls->setExclusiveZone(-1);

    const QString kbf = s.value("keyboardFocus").toString().toLower();
    if      (kbf == "exclusive") ls->setKeyboardInteractivity(LSW::KeyboardInteractivityExclusive);
    else if (kbf == "ondemand")  ls->setKeyboardInteractivity(LSW::KeyboardInteractivityOnDemand);
    else                         ls->setKeyboardInteractivity(LSW::KeyboardInteractivityNone);
}
#endif

int main(int argc, char* argv[]) {
#ifdef HAVE_LAYER_SHELL
#if QT_VERSION < QT_VERSION_CHECK(6, 5, 0)
    LayerShellQt::Shell::useLayerShell();
#endif
#endif

    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName("departure-hud");
    QGuiApplication::setApplicationDisplayName("Departure HUD");

    QCommandLineParser parser;
    parser.setApplicationDescription("Departure HUD — standalone Wayland system monitor");
    parser.addHelpOption();

    QCommandLineOption cfgOpt({"c", "config"}, "Path to JSON config", "path");
    QCommandLineOption installOpt("install-config",
        "Copy bundled defaults to $XDG_CONFIG_HOME/departure-hud/config.json and exit");
    QCommandLineOption printOpt("print-config",
        "Print active config path and exit");
    parser.addOption(cfgOpt);
    parser.addOption(installOpt);
    parser.addOption(printOpt);
    parser.process(app);

    const QString configPath = resolveConfigPath(parser.value(cfgOpt));

    if (parser.isSet(printOpt)) {
        printf("%s\n", qPrintable(configPath));
        return 0;
    }
    if (parser.isSet(installOpt)) {
        return ConfigLoader::installSample(configPath) ? 0 : 1;
    }

    ConfigLoader cfg;
    cfg.load(configPath);

    SysData sys(cfg.settings());
    MaskController mask;
    mask.setClickThrough(cfg.settings().value("clickThrough", true).toBool());

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("appSettings", cfg.settings());
    engine.rootContext()->setContextProperty("sysData", &sys);
#ifdef HAVE_LAYER_SHELL
    engine.rootContext()->setContextProperty("layerShellEnabled", true);
#else
    engine.rootContext()->setContextProperty("layerShellEnabled", false);
#endif

    engine.loadFromModule("DepartureHud", "Main");

    const auto roots = engine.rootObjects();
    if (roots.isEmpty()) return 1;

    QQuickWindow* window = qobject_cast<QQuickWindow*>(roots.first());
    if (!window) {
        qWarning("departure-hud: root QML object is not a Window");
        return 1;
    }

    QScreen* screen = pickScreen(cfg.settings().value("screen").toString());
    if (screen) {
        window->setScreen(screen);
        const QRect g = screen->geometry();
        window->setGeometry(g);
    }

#ifdef HAVE_LAYER_SHELL
    configureLayerShell(window, cfg.settings());
#endif

    QQuickItem* hudItem = window->contentItem()->findChild<QQuickItem*>("hud");
    if (hudItem) mask.attach(window, hudItem);

    window->setVisible(true);

    return app.exec();
}
