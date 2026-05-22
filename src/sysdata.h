#pragma once

#include <QHash>
#include <QObject>
#include <QProcess>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

class SysData : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList cpuPercents    READ cpuPercents    NOTIFY cpuChanged)
    Q_PROPERTY(QVariantList cpuFreqsGHz    READ cpuFreqsGHz    NOTIFY freqChanged)
    Q_PROPERTY(qreal cpuAvgPercent         READ cpuAvgPercent  NOTIFY cpuChanged)
    Q_PROPERTY(qreal cpuFreqAvgGHz         READ cpuFreqAvgGHz  NOTIFY freqChanged)
    Q_PROPERTY(qreal cpuFreqMaxGHz         READ cpuFreqMaxGHz  NOTIFY freqChanged)

    Q_PROPERTY(qreal memUsedPct   READ memUsedPct   NOTIFY memChanged)
    Q_PROPERTY(qreal memCachePct  READ memCachePct  NOTIFY memChanged)
    Q_PROPERTY(qreal swapUsedPct  READ swapUsedPct  NOTIFY memChanged)
    Q_PROPERTY(qreal memUsedGB    READ memUsedGB    NOTIFY memChanged)
    Q_PROPERTY(qreal memCacheGB   READ memCacheGB   NOTIFY memChanged)
    Q_PROPERTY(qreal swapUsedGB   READ swapUsedGB   NOTIFY memChanged)
    Q_PROPERTY(qreal memTotalGB   READ memTotalGB   NOTIFY memChanged)
    Q_PROPERTY(qreal swapTotalGB  READ swapTotalGB  NOTIFY memChanged)

    Q_PROPERTY(QString netInterface  READ netInterface  NOTIFY netChanged)
    Q_PROPERTY(qreal   netDownBps    READ netDownBps    NOTIFY netChanged)
    Q_PROPERTY(qreal   netUpBps      READ netUpBps      NOTIFY netChanged)

    Q_PROPERTY(bool    hasBattery      READ hasBattery      NOTIFY batChanged)
    Q_PROPERTY(qreal   batPercent      READ batPercent      NOTIFY batChanged)
    Q_PROPERTY(QString batState        READ batState        NOTIFY batChanged)
    Q_PROPERTY(qreal   batRateW        READ batRateW        NOTIFY batChanged)
    Q_PROPERTY(int     batMinutesLeft  READ batMinutesLeft  NOTIFY batChanged)
    Q_PROPERTY(int     batCycles       READ batCycles       NOTIFY batChanged)

    Q_PROPERTY(QVariantList disks  READ disks  NOTIFY diskChanged)

    Q_PROPERTY(qreal cpuTempC    READ cpuTempC    NOTIFY thermChanged)
    Q_PROPERTY(qreal gpuTempC    READ gpuTempC    NOTIFY thermChanged)
    Q_PROPERTY(qreal ssdTempC    READ ssdTempC    NOTIFY thermChanged)
    Q_PROPERTY(qreal caseTempC   READ caseTempC   NOTIFY thermChanged)
    Q_PROPERTY(bool  hasGpuTemp  READ hasGpuTemp  NOTIFY thermChanged)
    Q_PROPERTY(bool  hasSsdTemp  READ hasSsdTemp  NOTIFY thermChanged)
    Q_PROPERTY(bool  hasCaseTemp READ hasCaseTemp NOTIFY thermChanged)

    Q_PROPERTY(qreal   gpuLoadPct     READ gpuLoadPct     NOTIFY gpuChanged)
    Q_PROPERTY(qreal   gpuPowerW      READ gpuPowerW      NOTIFY gpuChanged)
    Q_PROPERTY(int     gpuClockMHz    READ gpuClockMHz    NOTIFY gpuChanged)
    Q_PROPERTY(int     gpuClockMaxMHz READ gpuClockMaxMHz NOTIFY gpuChanged)
    Q_PROPERTY(QString gpuName        READ gpuName        NOTIFY gpuChanged)

    Q_PROPERTY(bool hasBrightness  READ hasBrightness  NOTIFY brightnessChanged)
    Q_PROPERTY(int  brightnessPct  READ brightnessPct  NOTIFY brightnessChanged)
    Q_PROPERTY(int  nightLightPct  READ nightLightPct  NOTIFY brightnessChanged)

    Q_PROPERTY(int  volumePct  READ volumePct  NOTIFY volumeChanged)
    Q_PROPERTY(bool volMuted   READ volMuted   NOTIFY volumeChanged)

    Q_PROPERTY(QString hostName       READ hostName       CONSTANT)
    Q_PROPERTY(QString userName       READ userName       CONSTANT)
    Q_PROPERTY(QString kernelVersion  READ kernelVersion  CONSTANT)
    Q_PROPERTY(QString shellName      READ shellName      CONSTANT)
    Q_PROPERTY(int     uptimeSec      READ uptimeSec      NOTIFY uptimeChanged)
    Q_PROPERTY(QVariantList loadAvg   READ loadAvg        NOTIFY loadChanged)

public:
    explicit SysData(const QVariantMap& settings, QObject* parent = nullptr);
    ~SysData() override;

    QVariantList cpuPercents()   const { return m_cpuPercents; }
    QVariantList cpuFreqsGHz()   const { return m_cpuFreqsGHz; }
    qreal cpuAvgPercent()        const { return m_cpuAvgPercent; }
    qreal cpuFreqAvgGHz()        const { return m_cpuFreqAvgGHz; }
    qreal cpuFreqMaxGHz()        const { return m_cpuFreqMaxGHz; }

    qreal memUsedPct()  const { return m_memUsedPct; }
    qreal memCachePct() const { return m_memCachePct; }
    qreal swapUsedPct() const { return m_swapUsedPct; }
    qreal memUsedGB()   const { return m_memUsedGB; }
    qreal memCacheGB()  const { return m_memCacheGB; }
    qreal swapUsedGB()  const { return m_swapUsedGB; }
    qreal memTotalGB()  const { return m_memTotalGB; }
    qreal swapTotalGB() const { return m_swapTotalGB; }

    QString netInterface() const { return m_netInterface; }
    qreal netDownBps()     const { return m_netDownBps; }
    qreal netUpBps()       const { return m_netUpBps; }

    bool    hasBattery()     const { return m_hasBattery; }
    qreal   batPercent()     const { return m_batPercent; }
    QString batState()       const { return m_batState; }
    qreal   batRateW()       const { return m_batRateW; }
    int     batMinutesLeft() const { return m_batMinutesLeft; }
    int     batCycles()      const { return m_batCycles; }

    QVariantList disks() const { return m_disks; }

    qreal cpuTempC()    const { return m_cpuTempC; }
    qreal gpuTempC()    const { return m_gpuTempC; }
    qreal ssdTempC()    const { return m_ssdTempC; }
    qreal caseTempC()   const { return m_caseTempC; }
    bool  hasGpuTemp()  const { return m_hasGpuTemp; }
    bool  hasSsdTemp()  const { return m_hasSsdTemp; }
    bool  hasCaseTemp() const { return m_hasCaseTemp; }

    qreal   gpuLoadPct()     const { return m_gpuLoadPct; }
    qreal   gpuPowerW()      const { return m_gpuPowerW; }
    int     gpuClockMHz()    const { return m_gpuClockMHz; }
    int     gpuClockMaxMHz() const { return m_gpuClockMaxMHz; }
    QString gpuName()        const { return m_gpuName; }

    bool hasBrightness() const { return m_hasBrightness; }
    int  brightnessPct() const { return m_brightnessPct; }
    int  nightLightPct() const { return m_nightLightPct; }

    int  volumePct() const { return m_volumePct; }
    bool volMuted()  const { return m_volMuted; }

    QString hostName()      const { return m_hostName; }
    QString userName()      const { return m_userName; }
    QString kernelVersion() const { return m_kernelVersion; }
    QString shellName()     const { return m_shellName; }
    int     uptimeSec()     const { return m_uptimeSec; }
    QVariantList loadAvg()  const { return m_loadAvg; }

public slots:
    void poll();

signals:
    void cpuChanged();
    void freqChanged();
    void memChanged();
    void netChanged();
    void batChanged();
    void diskChanged();
    void thermChanged();
    void gpuChanged();
    void brightnessChanged();
    void volumeChanged();
    void uptimeChanged();
    void loadChanged();

private:
    void initIdentity();
    void pollCpu();
    void pollFreq();
    void pollMem();
    void pollNet();
    void pollLoad();
    void pollUptime();
    void pollBattery();
    void pollDisks();
    void pollThermals();
    void pollBrightness();
    void pollVolume();
    void pollGpu();

    static QString readAll(const QString& path);

    // settings
    int m_updateMs = 1000;
    QStringList m_disksToShow = { "/", "/home" };
    QString m_netInterfaceOverride;
    QString m_netMode = "sum";
    QString m_gpuScriptPath;

    // CPU
    QVariantList m_cpuPercents;
    QVariantList m_cpuFreqsGHz;
    qreal m_cpuAvgPercent = 0;
    qreal m_cpuFreqAvgGHz = 0;
    qreal m_cpuFreqMaxGHz = 5.0;
    QHash<QString, quint64> m_prevCpuTotal;
    QHash<QString, quint64> m_prevCpuIdle;

    // Memory
    qreal m_memUsedPct = 0, m_memCachePct = 0, m_swapUsedPct = 0;
    qreal m_memUsedGB = 0,  m_memCacheGB = 0,  m_swapUsedGB = 0;
    qreal m_memTotalGB = 0, m_swapTotalGB = 0;

    // Network
    QString m_netInterface;
    qreal m_netDownBps = 0, m_netUpBps = 0;
    QHash<QString, quint64> m_prevNetRx, m_prevNetTx;
    qint64 m_lastNetTickMs = 0;

    // Battery
    bool   m_hasBattery = false;
    qreal  m_batPercent = 0;
    QString m_batState = "UNKNOWN";
    qreal  m_batRateW = 0;
    int    m_batMinutesLeft = 0, m_batCycles = 0;

    // Disks
    QVariantList m_disks;

    // Thermals
    qreal m_cpuTempC = 0, m_gpuTempC = 0, m_ssdTempC = 0, m_caseTempC = 0;
    bool  m_hasGpuTemp = false, m_hasSsdTemp = false, m_hasCaseTemp = false;

    // GPU
    qreal m_gpuLoadPct = 0, m_gpuPowerW = 0;
    int   m_gpuClockMHz = 0, m_gpuClockMaxMHz = 0;
    QString m_gpuName;

    // Display
    bool m_hasBrightness = false;
    int  m_brightnessPct = 0, m_nightLightPct = 0;

    // Audio
    int  m_volumePct = 0;
    bool m_volMuted = false;

    // Identity / slow-moving
    QString m_hostName = "host";
    QString m_userName = "@user";
    QString m_kernelVersion;
    QString m_shellName;
    int     m_uptimeSec = 0;
    QVariantList m_loadAvg;

    QTimer m_pollTimer;
    QTimer m_slowTimer;

    QProcess* m_volProc = nullptr;
    QProcess* m_gpuProc = nullptr;
};
