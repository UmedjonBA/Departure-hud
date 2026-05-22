#include "sysdata.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStringList>
#include <QtGlobal>

#include <pwd.h>
#include <sys/statvfs.h>
#include <sys/utsname.h>
#include <unistd.h>

SysData::SysData(const QVariantMap& settings, QObject* parent)
    : QObject(parent) {
    if (settings.contains("updateMs"))    m_updateMs = settings.value("updateMs").toInt();
    if (settings.contains("disksToShow")) {
        m_disksToShow.clear();
        const auto raw = settings.value("disksToShow").toString().split(',', Qt::SkipEmptyParts);
        for (const auto& s : raw) m_disksToShow << s.trimmed();
    }
    if (settings.contains("netInterface")) m_netInterfaceOverride = settings.value("netInterface").toString();
    if (settings.contains("netMode"))      m_netMode = settings.value("netMode").toString();
    if (settings.contains("gpuScriptPath")) {
        QString p = settings.value("gpuScriptPath").toString();
        if (p.startsWith("~")) p.replace(0, 1, QDir::homePath());
        m_gpuScriptPath = p;
    }

    m_loadAvg = QVariantList{ 0.0, 0.0, 0.0 };

    initIdentity();

    m_pollTimer.setInterval(m_updateMs);
    connect(&m_pollTimer, &QTimer::timeout, this, &SysData::poll);
    m_pollTimer.start();
    QTimer::singleShot(0, this, &SysData::poll);

    m_slowTimer.setInterval(qMax(5000, m_updateMs * 5));
    connect(&m_slowTimer, &QTimer::timeout, this, [this]{ pollDisks(); });
    m_slowTimer.start();
    QTimer::singleShot(50, this, [this]{ pollDisks(); });
}

SysData::~SysData() = default;

QString SysData::readAll(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    return QString::fromUtf8(f.readAll());
}

void SysData::initIdentity() {
    char hbuf[256] = {0};
    if (gethostname(hbuf, sizeof(hbuf) - 1) == 0) m_hostName = QString::fromLocal8Bit(hbuf);

    if (struct passwd* pw = getpwuid(getuid())) {
        m_userName = QString("@") + QString::fromLocal8Bit(pw->pw_name);
    }

    struct utsname un;
    if (uname(&un) == 0) m_kernelVersion = QString::fromLocal8Bit(un.release);

    const char* sh = qgetenv("SHELL").constData();
    if (sh && *sh) {
        QFileInfo fi(QString::fromLocal8Bit(sh));
        m_shellName = fi.baseName();
    } else {
        m_shellName = "sh";
    }
}

void SysData::poll() {
    pollCpu();
    pollFreq();
    pollMem();
    pollNet();
    pollLoad();
    pollUptime();
    pollBattery();
    pollThermals();
    pollBrightness();
    pollVolume();
    pollGpu();
}

void SysData::pollCpu() {
    const QString text = readAll("/proc/stat");
    if (text.isEmpty()) return;

    static const QRegularExpression splitRx("\\s+");
    static const QRegularExpression cpuN("^cpu\\d+$");

    QHash<QString, quint64> newTot, newIdle;
    QVariantList loads;
    const auto lines = text.split('\n');

    for (const auto& line : lines) {
        if (!line.startsWith("cpu")) continue;
        const auto parts = line.split(splitRx, Qt::SkipEmptyParts);
        if (parts.size() < 5) continue;
        if (parts[0] == "cpu") continue;
        if (!cpuN.match(parts[0]).hasMatch()) continue;

        auto field = [&](int i) -> quint64 {
            return parts.size() > i ? parts[i].toULongLong() : 0;
        };
        const quint64 user = field(1), nice = field(2), sys = field(3),
                      idle = field(4), iowait = field(5), irq = field(6),
                      sirq = field(7), steal = field(8);
        const quint64 total = user + nice + sys + idle + iowait + irq + sirq + steal;
        const quint64 idleAll = idle + iowait;

        qreal pct = 0;
        const auto pt = m_prevCpuTotal.constFind(parts[0]);
        if (pt != m_prevCpuTotal.cend() && total > pt.value()) {
            const quint64 dt = total - pt.value();
            const quint64 di = idleAll - m_prevCpuIdle.value(parts[0]);
            pct = qBound<qreal>(0.0, (1.0 - qreal(di) / qreal(dt)) * 100.0, 100.0);
        }
        newTot[parts[0]]  = total;
        newIdle[parts[0]] = idleAll;
        loads.append(pct);
    }

    m_prevCpuTotal = newTot;
    m_prevCpuIdle  = newIdle;

    if (!loads.isEmpty()) {
        m_cpuPercents = loads;
        qreal sum = 0;
        for (const auto& v : loads) sum += v.toDouble();
        m_cpuAvgPercent = sum / loads.size();
        emit cpuChanged();
    }
}

void SysData::pollFreq() {
    const QString text = readAll("/proc/cpuinfo");
    if (text.isEmpty()) return;

    static const QRegularExpression fr(
        "^cpu MHz\\s*:\\s*([\\d.]+)",
        QRegularExpression::MultilineOption);

    QVariantList freqs;
    auto it = fr.globalMatch(text);
    while (it.hasNext()) {
        freqs.append(it.next().captured(1).toDouble() / 1000.0);
    }
    if (!freqs.isEmpty()) {
        m_cpuFreqsGHz = freqs;
        qreal sum = 0, mx = 0;
        for (const auto& v : freqs) {
            const qreal d = v.toDouble();
            sum += d;
            if (d > mx) mx = d;
        }
        m_cpuFreqAvgGHz = sum / freqs.size();
        if (mx > m_cpuFreqMaxGHz) m_cpuFreqMaxGHz = mx;
        emit freqChanged();
    }
}

void SysData::pollMem() {
    const QString text = readAll("/proc/meminfo");
    if (text.isEmpty()) return;

    static const QRegularExpression rx(
        "^(\\w+):\\s+(\\d+)",
        QRegularExpression::MultilineOption);

    QHash<QString, quint64> info;
    auto it = rx.globalMatch(text);
    while (it.hasNext()) {
        const auto m = it.next();
        info[m.captured(1)] = m.captured(2).toULongLong();
    }

    const quint64 total    = info.value("MemTotal", 0);
    const quint64 freeKb   = info.value("MemFree", 0);
    const quint64 avail    = info.value("MemAvailable", freeKb);
    const quint64 buff     = info.value("Buffers", 0);
    const quint64 cache    = info.value("Cached", 0);
    const quint64 sReclaim = info.value("SReclaimable", 0);
    const quint64 used     = total > avail ? total - avail : 0;
    const quint64 cacheAll = buff + cache + sReclaim;
    const quint64 swapT    = info.value("SwapTotal", 0);
    const quint64 swapF    = info.value("SwapFree", 0);
    const quint64 swapU    = swapT > swapF ? swapT - swapF : 0;

    m_memTotalGB  = total    / 1024.0 / 1024.0;
    m_memUsedGB   = used     / 1024.0 / 1024.0;
    m_memCacheGB  = cacheAll / 1024.0 / 1024.0;
    m_swapTotalGB = swapT    / 1024.0 / 1024.0;
    m_swapUsedGB  = swapU    / 1024.0 / 1024.0;
    m_memUsedPct  = total ? (used * 100.0 / total) : 0;
    m_memCachePct = total ? (cacheAll * 100.0 / total) : 0;
    m_swapUsedPct = swapT ? (swapU * 100.0 / swapT) : 0;
    emit memChanged();
}

void SysData::pollNet() {
    const QString text = readAll("/proc/net/dev");
    if (text.isEmpty()) return;

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const qreal dt = (m_lastNetTickMs == 0)
        ? (m_updateMs / 1000.0)
        : (nowMs - m_lastNetTickMs) / 1000.0;
    m_lastNetTickMs = nowMs;

    static const QRegularExpression splitRx("\\s+");

    QHash<QString, quint64> newRx, newTx;
    const auto lines = text.split('\n');
    for (const auto& line : lines) {
        const int colon = line.indexOf(':');
        if (colon < 0) continue;
        const QString iface = line.left(colon).trimmed();
        if (iface.isEmpty() || iface == "lo") continue;
        const auto fields = line.mid(colon + 1).trimmed().split(splitRx, Qt::SkipEmptyParts);
        if (fields.size() < 10) continue;
        newRx[iface] = fields[0].toULongLong();
        newTx[iface] = fields[8].toULongLong();
    }

    const QString override = m_netInterfaceOverride.trimmed();

    if (m_prevNetRx.isEmpty() || dt <= 0) {
        m_prevNetRx = newRx;
        m_prevNetTx = newTx;
        if (!override.isEmpty()) {
            m_netInterface = override;
        } else {
            QString best;
            quint64 bestSum = 0;
            for (auto it = newRx.cbegin(); it != newRx.cend(); ++it) {
                const quint64 s = it.value() + newTx.value(it.key());
                if (s > bestSum) { bestSum = s; best = it.key(); }
            }
            m_netInterface = best;
        }
        emit netChanged();
        return;
    }

    quint64 totalDown = 0, totalUp = 0;
    QString bestIface;
    qint64 bestDelta = -1;
    for (auto it = newRx.cbegin(); it != newRx.cend(); ++it) {
        const QString& n = it.key();
        const quint64 prevR = m_prevNetRx.value(n, it.value());
        const quint64 prevT = m_prevNetTx.value(n, newTx.value(n));
        const quint64 dRx = it.value()       > prevR ? it.value()       - prevR : 0;
        const quint64 dTx = newTx.value(n)   > prevT ? newTx.value(n)   - prevT : 0;
        totalDown += dRx;
        totalUp   += dTx;
        const qint64 d = qint64(dRx + dTx);
        if (d > bestDelta) { bestDelta = d; bestIface = n; }
    }

    QString pickName;
    quint64 dRxPick = 0, dTxPick = 0;
    if (!override.isEmpty() && newRx.contains(override)) {
        pickName = override;
        const quint64 prevR = m_prevNetRx.value(override, newRx.value(override));
        const quint64 prevT = m_prevNetTx.value(override, newTx.value(override));
        dRxPick = newRx.value(override) > prevR ? newRx.value(override) - prevR : 0;
        dTxPick = newTx.value(override) > prevT ? newTx.value(override) - prevT : 0;
    } else if (m_netMode == "sum") {
        pickName = bestIface;
        dRxPick = totalDown;
        dTxPick = totalUp;
    } else {
        pickName = bestIface;
        const quint64 prevR = m_prevNetRx.value(bestIface, newRx.value(bestIface));
        const quint64 prevT = m_prevNetTx.value(bestIface, newTx.value(bestIface));
        dRxPick = newRx.value(bestIface) > prevR ? newRx.value(bestIface) - prevR : 0;
        dTxPick = newTx.value(bestIface) > prevT ? newTx.value(bestIface) - prevT : 0;
    }

    m_netDownBps = dRxPick / dt;
    m_netUpBps   = dTxPick / dt;
    m_prevNetRx  = newRx;
    m_prevNetTx  = newTx;
    m_netInterface = pickName
        + ((m_netMode == "sum" && override.isEmpty()) ? QStringLiteral(" (sum)") : QString());
    emit netChanged();
}

void SysData::pollLoad() {
    const QString text = readAll("/proc/loadavg");
    if (text.isEmpty()) return;
    static const QRegularExpression splitRx("\\s+");
    const auto parts = text.trimmed().split(splitRx, Qt::SkipEmptyParts);
    if (parts.size() >= 3) {
        m_loadAvg = QVariantList{
            parts[0].toDouble(),
            parts[1].toDouble(),
            parts[2].toDouble()
        };
        emit loadChanged();
    }
}

void SysData::pollUptime() {
    const QString text = readAll("/proc/uptime");
    if (text.isEmpty()) return;
    static const QRegularExpression splitRx("\\s+");
    const auto parts = text.trimmed().split(splitRx, Qt::SkipEmptyParts);
    if (!parts.isEmpty()) {
        m_uptimeSec = int(parts[0].toDouble());
        emit uptimeChanged();
    }
}

void SysData::pollBattery() {
    QDir psd("/sys/class/power_supply");
    const auto entries = psd.entryList(QStringList() << "BAT*",
                                       QDir::Dirs | QDir::NoDotAndDotDot);
    if (entries.isEmpty()) {
        if (m_hasBattery) { m_hasBattery = false; emit batChanged(); }
        return;
    }

    const QString dir = psd.absoluteFilePath(entries.first());
    const QString text = readAll(dir + "/uevent");
    if (text.isEmpty()) return;

    QHash<QString, QString> info;
    for (const auto& ln : text.split('\n')) {
        const int eq = ln.indexOf('=');
        if (eq > 0) info[ln.left(eq)] = ln.mid(eq + 1).trimmed();
    }
    if (info.value("POWER_SUPPLY_PRESENT") == "0") {
        if (m_hasBattery) { m_hasBattery = false; emit batChanged(); }
        return;
    }

    m_hasBattery = true;
    m_batPercent = info.value("POWER_SUPPLY_CAPACITY").toDouble();
    m_batState   = info.value("POWER_SUPPLY_STATUS", "UNKNOWN").toUpper();

    qreal rateW = 0;
    if (info.contains("POWER_SUPPLY_POWER_NOW")) {
        rateW = info.value("POWER_SUPPLY_POWER_NOW").toDouble() / 1e6;
    } else if (info.contains("POWER_SUPPLY_CURRENT_NOW") && info.contains("POWER_SUPPLY_VOLTAGE_NOW")) {
        const qreal I = info.value("POWER_SUPPLY_CURRENT_NOW").toDouble() / 1e6;
        const qreal V = info.value("POWER_SUPPLY_VOLTAGE_NOW").toDouble() / 1e6;
        rateW = I * V;
    }
    if      (m_batState == "DISCHARGING") rateW = -qAbs(rateW);
    else if (m_batState == "CHARGING")    rateW =  qAbs(rateW);
    m_batRateW = rateW;

    const QString fKey = info.contains("POWER_SUPPLY_CHARGE_FULL")
        ? "POWER_SUPPLY_CHARGE_FULL" : "POWER_SUPPLY_ENERGY_FULL";
    const QString nKey = info.contains("POWER_SUPPLY_CHARGE_NOW")
        ? "POWER_SUPPLY_CHARGE_NOW"  : "POWER_SUPPLY_ENERGY_NOW";
    const QString rKey = info.contains("POWER_SUPPLY_CURRENT_NOW")
        ? "POWER_SUPPLY_CURRENT_NOW" : "POWER_SUPPLY_POWER_NOW";

    const qreal fullU = info.value(fKey).toDouble();
    const qreal nowU  = info.value(nKey).toDouble();
    const qreal rate  = info.value(rKey).toDouble();

    if (rate > 0 && nowU > 0) {
        qreal hours = 0;
        if      (m_batState == "DISCHARGING") hours = nowU / rate;
        else if (m_batState == "CHARGING")    hours = (fullU - nowU) / rate;
        m_batMinutesLeft = qMax(0, int(hours * 60));
    } else {
        m_batMinutesLeft = 0;
    }

    const QString cyc = readAll(dir + "/cycle_count").trimmed();
    if (!cyc.isEmpty()) m_batCycles = cyc.toInt();
    emit batChanged();
}

void SysData::pollDisks() {
    QVariantList out;
    for (const auto& mount : m_disksToShow) {
        if (mount.isEmpty()) continue;
        struct statvfs s;
        if (statvfs(mount.toLocal8Bit().constData(), &s) != 0) continue;
        const quint64 frsize = s.f_frsize ? s.f_frsize : s.f_bsize;
        const quint64 total  = quint64(s.f_blocks) * frsize;
        const quint64 avail  = quint64(s.f_bavail) * frsize;
        const quint64 used   = total > avail ? total - avail : 0;
        if (total == 0) continue;
        QVariantMap m;
        m["mount"]   = mount;
        m["usedPct"] = qreal(used) * 100.0 / qreal(total);
        m["usedGB"]  = qreal(used)  / 1024.0 / 1024.0 / 1024.0;
        m["totalGB"] = qreal(total) / 1024.0 / 1024.0 / 1024.0;
        out.append(m);
    }
    if (!out.isEmpty()) {
        m_disks = out;
        emit diskChanged();
    }
}

void SysData::pollThermals() {
    QDir hw("/sys/class/hwmon");
    const auto entries = hw.entryList(QStringList() << "hwmon*",
                                      QDir::Dirs | QDir::NoDotAndDotDot);

    QList<qreal> cpuV, gpuV, ssdV, caseV;
    for (const auto& e : entries) {
        const QString d    = hw.absoluteFilePath(e);
        const QString name = readAll(d + "/name").trimmed().toLower();
        QDir dd(d);
        const auto inputs = dd.entryList(QStringList() << "temp*_input", QDir::Files);
        for (const auto& f : inputs) {
            QString base = f;
            base.chop(QString("_input").size());
            const QString lbl = readAll(d + "/" + base + "_label").trimmed().toLower();
            bool ok = false;
            const qreal v = readAll(d + "/" + f).trimmed().toDouble(&ok) / 1000.0;
            if (!ok || v <= 0 || v > 200) continue;

            if (name.contains("nvme") || name.contains("nvm")
                || lbl.contains("ssd") || lbl.contains("composite")) {
                ssdV << v;
            } else if (name.contains("amdgpu") || name.contains("nouveau")
                       || name.contains("nvidia") || name.contains("i915")
                       || lbl.contains("edge") || lbl.contains("junction")
                       || lbl.contains("gpu")) {
                gpuV << v;
            } else if (name.contains("coretemp") || name.contains("k10temp")
                       || name.contains("zenpower")
                       || lbl.contains("package") || lbl.contains("tctl") || lbl.contains("tdie")
                       || (lbl.startsWith("core") && cpuV.size() < 16)) {
                cpuV << v;
            } else if (name.contains("acpitz") || name.contains("ec")) {
                caseV << v;
            }
        }
    }

    auto mx = [](const QList<qreal>& a) -> qreal {
        qreal m = 0;
        for (qreal v : a) if (v > m) m = v;
        return m;
    };

    bool changed = false;
    if (!cpuV.isEmpty())  { m_cpuTempC  = mx(cpuV);  changed = true; }
    if (!gpuV.isEmpty())  { m_gpuTempC  = mx(gpuV);  m_hasGpuTemp  = true; changed = true; }
    if (!ssdV.isEmpty())  { m_ssdTempC  = mx(ssdV);  m_hasSsdTemp  = true; changed = true; }
    if (!caseV.isEmpty()) { m_caseTempC = mx(caseV); m_hasCaseTemp = true; changed = true; }
    if (changed) emit thermChanged();
}

void SysData::pollBrightness() {
    QDir bl("/sys/class/backlight");
    const auto entries = bl.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const auto& e : entries) {
        const QString d = bl.absoluteFilePath(e);
        bool ok1 = false, ok2 = false;
        const int b = readAll(d + "/brightness").trimmed().toInt(&ok1);
        const int m = readAll(d + "/max_brightness").trimmed().toInt(&ok2);
        if (ok1 && ok2 && m > 0) {
            m_hasBrightness = true;
            m_brightnessPct = int(qreal(b) / m * 100.0 + 0.5);
            emit brightnessChanged();
            return;
        }
    }
}

void SysData::pollVolume() {
    if (m_volProc && m_volProc->state() != QProcess::NotRunning) return;
    if (!m_volProc) {
        m_volProc = new QProcess(this);
        connect(m_volProc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, [this](int, QProcess::ExitStatus){
            const QString out = QString::fromUtf8(m_volProc->readAllStandardOutput()).toLower();
            const bool muted = out.contains("muted") && !out.contains("muted: no");

            static const QRegularExpression rxA("volume:\\s+([\\d.]+)");
            auto m = rxA.match(out);
            if (m.hasMatch()) {
                m_volMuted  = muted;
                m_volumePct = int(m.captured(1).toDouble() * 100.0 + 0.5);
                emit volumeChanged();
                return;
            }
            static const QRegularExpression rxB("(\\d+)\\s*%");
            m = rxB.match(out);
            if (m.hasMatch()) {
                m_volMuted  = muted;
                m_volumePct = m.captured(1).toInt();
                emit volumeChanged();
            }
        });
    }
    const QString cmd =
        "if command -v wpctl >/dev/null 2>&1; then wpctl get-volume @DEFAULT_AUDIO_SINK@; "
        "elif command -v pactl >/dev/null 2>&1; then pactl get-sink-volume @DEFAULT_SINK@; fi";
    m_volProc->start("sh", { "-c", cmd });
}

void SysData::pollGpu() {
    if (m_gpuScriptPath.isEmpty()) return;
    const QFileInfo fi(m_gpuScriptPath);
    if (!fi.exists() || !fi.isExecutable()) return;
    if (m_gpuProc && m_gpuProc->state() != QProcess::NotRunning) return;

    if (!m_gpuProc) {
        m_gpuProc = new QProcess(this);
        connect(m_gpuProc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, [this](int, QProcess::ExitStatus){
            const QByteArray raw = m_gpuProc->readAllStandardOutput();
            if (raw.trimmed().isEmpty()) return;

            QJsonParseError err;
            const auto doc = QJsonDocument::fromJson(raw, &err);
            if (err.error != QJsonParseError::NoError || !doc.isObject()) return;

            const auto obj = doc.object();
            const QString t = obj.value("tooltip").toString();

            static const QRegularExpression rxTemp ("Temperature\\s*[:=]\\s*([\\d.]+)\\s*°?\\s*C",
                                                    QRegularExpression::CaseInsensitiveOption);
            static const QRegularExpression rxUtil ("Utilization\\s*[:=]\\s*([\\d.]+)\\s*%",
                                                    QRegularExpression::CaseInsensitiveOption);
            static const QRegularExpression rxPower("Power[^:]*[:=]\\s*([\\d.]+)",
                                                    QRegularExpression::CaseInsensitiveOption);
            static const QRegularExpression rxClock("Clock[^:]*[:=]\\s*([\\d.]+)\\s*/\\s*([\\d.]+)\\s*MHz",
                                                    QRegularExpression::CaseInsensitiveOption);
            static const QRegularExpression rxName ("[^\\n]*NVIDIA[^\\n]*|[^\\n]*Radeon[^\\n]*|[^\\n]*Intel[^\\n]*GPU[^\\n]*",
                                                    QRegularExpression::CaseInsensitiveOption);

            const auto mt = rxTemp.match(t);
            const auto mu = rxUtil.match(t);
            const auto mp = rxPower.match(t);
            const auto mc = rxClock.match(t);
            const auto mn = rxName.match(t);

            bool changedGpu = false;
            if (mt.hasMatch()) {
                m_gpuTempC = mt.captured(1).toDouble();
                m_hasGpuTemp = true;
                emit thermChanged();
            }
            if (mu.hasMatch()) { m_gpuLoadPct = mu.captured(1).toDouble(); changedGpu = true; }
            if (mp.hasMatch()) { m_gpuPowerW  = mp.captured(1).toDouble(); changedGpu = true; }
            if (mc.hasMatch()) {
                m_gpuClockMHz    = int(mc.captured(1).toDouble() + 0.5);
                m_gpuClockMaxMHz = int(mc.captured(2).toDouble() + 0.5);
                changedGpu = true;
            }
            if (mn.hasMatch()) { m_gpuName = mn.captured(0).trimmed(); changedGpu = true; }

            if (!mt.hasMatch()) {
                const QString tx = obj.value("text").toString();
                static const QRegularExpression rxTempT("([\\d.]+)\\s*°?\\s*C",
                                                        QRegularExpression::CaseInsensitiveOption);
                const auto m2 = rxTempT.match(tx);
                if (m2.hasMatch()) {
                    m_gpuTempC = m2.captured(1).toDouble();
                    m_hasGpuTemp = true;
                    emit thermChanged();
                }
            }
            if (changedGpu) emit gpuChanged();
        });
    }
    m_gpuProc->start(m_gpuScriptPath, {});
}
