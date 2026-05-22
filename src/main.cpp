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
#include "visibilitywatcher.h"

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

// Auto-scale to a configurable fraction of the screen. The HUD's design
// surface is fixed at 1180×600 (≈ 1.97:1, a wide horizontal panel) and
// is always scaled uniformly to preserve its proportions. The factor is
// picked so the HUD takes up at most `fit` of the screen's width AND at
// most `fit` of the screen's height — whichever is more constraining
// wins, so the HUD never overflows on any aspect ratio (21:9, 32:9,
// portrait, 4:3, square, …).
//
// Defaults: fit = 0.70 → HUD targets ~70% of the smaller dimension.
// Clamp range [0.5, 4.0] keeps it usable on tiny / huge displays.
static qreal computeAutoScale(QScreen* s, qreal fit) {
    constexpr qreal baseW = 1180.0;
    constexpr qreal baseH = 600.0;
    if (!s) return 1.0;
    const QRect g = s->geometry();
    if (g.width() <= 0 || g.height() <= 0) return 1.0;
    fit = qBound<qreal>(0.1, fit, 1.0);
    const qreal sx = (g.width()  * fit) / baseW;
    const qreal sy = (g.height() * fit) / baseH;
    return qBound<qreal>(0.5, qMin(sx, sy), 4.0);
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

    QVariantMap settings = cfg.settings();
    QScreen* screen = pickScreen(settings.value("screen").toString());

    if (settings.value("autoScale", false).toBool()) {
        const qreal fit = settings.value("autoScaleFit", 0.7).toDouble();
        const qreal s   = computeAutoScale(screen, fit);
        settings["scale"] = s;
        fprintf(stderr,
                "departure-hud: autoScale → %.2f (fit=%.2f, screen %dx%d)\n",
                s, fit,
                screen ? screen->geometry().width()  : 0,
                screen ? screen->geometry().height() : 0);
    }

    SysData sys(settings);
    MaskController mask;
    mask.setClickThrough(settings.value("clickThrough", true).toBool());

    VisibilityWatcher visibility;
    QObject::connect(&visibility, &VisibilityWatcher::visibleChanged,
                     &sys, [&]{ sys.setActive(visibility.visible()); });

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("appSettings", settings);
    engine.rootContext()->setContextProperty("sysData", &sys);
    engine.rootContext()->setContextProperty("visibility", &visibility);
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

    if (screen) {
        window->setScreen(screen);
        const QRect g = screen->geometry();
        window->setGeometry(g);
    }

#ifdef HAVE_LAYER_SHELL
    configureLayerShell(window, settings);
#endif

    QQuickItem* hudItem = window->contentItem()->findChild<QQuickItem*>("hud");
    if (hudItem) mask.attach(window, hudItem);
    visibility.attach(window);

    window->setVisible(true);

    return app.exec();
}
