#include <QCommandLineParser>
#include <QDir>
#include <QGuiApplication>
#include <QList>
#include <QMetaType>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QScreen>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariantList>
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

// Resolve the list of QScreens the HUD should run on.
// Priority:
//   1. "screens" key — either the literal string "*" (all screens) or
//      a JSON array of output names (["DP-1", "HDMI-A-1"]). Unknown
//      names are skipped silently.
//   2. Legacy singular "screen" — single output name, "" = primary.
//   3. Fallback — primary screen.
static QList<QScreen*> resolveTargetScreens(const QVariantMap& settings) {
    const QList<QScreen*> all = QGuiApplication::screens();
    QList<QScreen*> out;

    auto byName = [&](const QString& name) -> QScreen* {
        for (QScreen* s : all) if (s->name() == name) return s;
        return nullptr;
    };

    const QVariant v = settings.value("screens");

    if (v.metaType().id() == QMetaType::QString) {
        if (v.toString().trimmed() == "*") return all;
    }

    if (v.canConvert<QVariantList>()) {
        const QVariantList items = v.toList();
        bool sawWildcard = false;
        for (const QVariant& it : items) {
            const QString name = it.toString().trimmed();
            if (name == "*") { sawWildcard = true; break; }
            if (name.isEmpty()) continue;
            if (QScreen* s = byName(name)) {
                if (!out.contains(s)) out.append(s);
            }
        }
        if (sawWildcard) return all;
        if (!out.isEmpty()) return out;
    }

    const QString single = settings.value("screen").toString().trimmed();
    if (!single.isEmpty()) {
        if (QScreen* s = byName(single)) return { s };
    }

    if (QScreen* p = QGuiApplication::primaryScreen()) return { p };
    return {};
}

// Auto-scale to a configurable fraction of the screen. See README's
// "Sizing on different monitors" for the formula and a worked table.
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
    const QVariantMap settings = cfg.settings();

    const QList<QScreen*> targets = resolveTargetScreens(settings);
    if (targets.isEmpty()) {
        qWarning("departure-hud: no target screens resolved — aborting");
        return 1;
    }

    SysData sys(settings);

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("sysData", &sys);
#ifdef HAVE_LAYER_SHELL
    engine.rootContext()->setContextProperty("layerShellEnabled", true);
#else
    engine.rootContext()->setContextProperty("layerShellEnabled", false);
#endif

    QQmlComponent windowComponent(&engine,
        QUrl(QStringLiteral("qrc:/qt/qml/DepartureHud/Main.qml")));
    if (windowComponent.isError()) {
        qWarning() << "departure-hud: Main.qml load failed:"
                   << windowComponent.errorString();
        return 1;
    }

    QList<VisibilityWatcher*> watchers;
    QList<MaskController*>    masks;
    QList<QQuickWindow*>      windows;

    auto syncAggregateVisibility = [&]() {
        bool any = false;
        for (VisibilityWatcher* w : watchers) {
            if (w && w->visible()) { any = true; break; }
        }
        sys.setActive(any || watchers.isEmpty());
    };

    for (QScreen* screen : targets) {
        QVariantMap winSettings = settings;
        if (winSettings.value("autoScale", false).toBool()) {
            const qreal fit = winSettings.value("autoScaleFit", 0.7).toDouble();
            winSettings["scale"] = computeAutoScale(screen, fit);
        }

        auto* watcher = new VisibilityWatcher(&app);
        auto* mask    = new MaskController(&app);
        mask->setClickThrough(winSettings.value("clickThrough", true).toBool());

        auto* ctx = new QQmlContext(engine.rootContext(), &engine);
        ctx->setContextProperty("appSettings", winSettings);
        ctx->setContextProperty("visibility", watcher);

        QObject* obj = windowComponent.create(ctx);
        if (!obj) {
            qWarning() << "departure-hud: failed to create window for"
                       << screen->name();
            delete ctx;
            delete watcher;
            delete mask;
            continue;
        }

        auto* window = qobject_cast<QQuickWindow*>(obj);
        if (!window) {
            qWarning("departure-hud: root QML object is not a Window");
            obj->deleteLater();
            delete ctx;
            delete watcher;
            delete mask;
            continue;
        }
        // Tie the per-window context lifetime to the window. The window
        // itself is owned by the engine via QQmlComponent::create().
        ctx->setParent(window);

        window->setScreen(screen);
        window->setGeometry(screen->geometry());

#ifdef HAVE_LAYER_SHELL
        configureLayerShell(window, winSettings);
#endif

        QQuickItem* hudItem = window->contentItem()->findChild<QQuickItem*>("hud");
        if (hudItem) mask->attach(window, hudItem);
        watcher->attach(window);

        QObject::connect(watcher, &VisibilityWatcher::visibleChanged,
                         &sys, syncAggregateVisibility);

        watchers << watcher;
        masks    << mask;
        windows  << window;

        fprintf(stderr,
                "departure-hud: window on %s (%dx%d, scale=%.2f)\n",
                qPrintable(screen->name()),
                screen->geometry().width(),
                screen->geometry().height(),
                winSettings.value("scale").toDouble());

        window->setVisible(true);
    }

    if (windows.isEmpty()) {
        qWarning("departure-hud: no windows created — aborting");
        return 1;
    }

    return app.exec();
}
