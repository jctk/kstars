#include "extensions.h"
#include "auxiliary/kspaths.h"
#include "version.h"

#include <QDir>
#include <QIcon>
#include <QDebug>
#include <QProcess>

extensions::extensions(QObject *parent) : QObject{ parent } {
    found = new QMap<QString, extDetails>;
    extensionProcess = new QProcess(this);

    connect(extensionProcess, &QProcess::started, this, [=]()
                     {
                         emit extensionStateChanged(Ekos::EXTENSION_STARTED);
                     });
    connect(extensionProcess, static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished), this, [=]()
                     {
                         emit extensionStateChanged(Ekos::EXTENSION_STOPPED);
                     });
    connect(extensionProcess, &QProcess::readyRead, this, [this] {
        emit extensionOutput(extensionProcess->readAll());
    });
}

bool extensions::discover()
{
    bool sucess = false;

    QDir dir = QDir(KSPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/extensions");
    // Makes directory if not existing, doesn't change anything if already exists
    if (dir.mkpath(".")) {
        m_Directory = dir.absolutePath();
        QStringList filesExe = dir.entryList(QStringList(), QDir::Files | QDir::Executable);
        QStringList filesConf = dir.entryList(QStringList() << "*.conf", QDir::Files | QDir::Readable);
        QStringList filesIcons = dir.entryList(QStringList() << "*.jpg" << "*.bmp" << "*.gif" << "*.png" << "*.svg",
                                               QDir::Files | QDir::Readable);
        if (! filesExe.isEmpty()) {
            qDebug() << "Found extension(s): " << filesExe;

            // Remove any executable without a .conf fileROCm
            foreach (QString exe, filesExe) {
                if (!filesConf.contains(QString(exe).append(".conf"))) {
                    qDebug() << QString(".conf file not found for extension %1").arg(exe);
                    filesExe.removeOne(exe);
                }
            }

            if (! filesExe.isEmpty()) {

                // Remove any .conf files that don't share filename with an executable
                foreach (QString conf, filesConf) {
                    if (!filesExe.contains(QString(conf).remove(".conf"))) {
                        qDebug() << QString("Extraneous extension %1 file found without executable").arg(conf);
                        filesConf.removeOne(conf);
                    }
                    // Remove any .conf file that is not valid
                    if (!confValid(conf)) {
                        qDebug() << QString(".conf file %1 is not valid").arg(conf);
                        filesConf.removeOne(conf);
                    }
                }

                //Check if any executable doesn't have a valid .conf
                foreach (QString exe, filesExe) {
                    if (!filesConf.contains(QString(exe).append(".conf"))) {
                        filesExe.removeOne(exe);
                    }
                }

                // Check if we have any executables with valid .conf files and build map
                if (! filesExe.isEmpty() && (filesExe.count() == filesConf.count())) {
                    foreach (QString exe, filesExe) {
                        extDetails m_ext;

                        QString iconName = "";
                        foreach (QString name, filesIcons) {
                            if (name.contains(exe)) {
                                iconName = name;
                                break;
                            }
                        }
                        QIcon icon;
                        if (iconName != "") {
                            QString temp;
                            temp.append(m_Directory).append("/").append(iconName);
                            icon.addFile(temp);
                        } else {
                            icon = QIcon::fromTheme("plugins");
                        }

                        QString confFileName;
                        QString tooltip = "";
                        bool runDetached = false;
                        confFileName.append(m_Directory).append("/").append(exe).append(".conf");
                        QFile confFile(confFileName);
                        if (confFile.exists()) {
                            if (confFile.open(QIODevice::ReadOnly) && confFile.isReadable()) {
                                QTextStream confTS = QTextStream(&confFile);
                                while (!confTS.atEnd()) {
                                    QString confLine = confTS.readLine();
                                    if (confLine.contains("tooltip=")) {
                                        tooltip = confLine.right(confLine.length() - (confLine.indexOf("=")) - 1);
                                    } else if (confLine.contains("runDetached=true")) {
                                        runDetached = true;
                                    }
                                }
                            } else qDebug() << QString("Can't access .conf file %1").arg(exe).append(".conf");
                        } else qDebug() << QString(".conf file %1 disappeared").arg(exe).append(".conf");

                        m_ext.tooltip = tooltip;
                        m_ext.icon = icon;
                        m_ext.detached = runDetached;
                        found->insert(exe, m_ext);

                        sucess = true;
                    }
                } else qDebug() << "No extensions found with valid .conf files";
            } else qDebug() << "No extensions found with .conf files";
        } else qDebug() << "No extensions found";
    } else qDebug() << "Could not access extensions directory";

    return sucess;
}

bool extensions::confValid(const QString &filePath)
{
    // Check that the passed extension .conf file contains a line starting
    // minimum_kstars_version=xx.yy.zz
    // and that the xx.yy.zz is no higher that the KSTARS_VERSION

    bool valid = false;

    QString confFileName;
    confFileName.append(m_Directory).append("/").append(filePath);
    QFile confFile(confFileName);
    if (confFile.exists()) {
        if (confFile.open(QIODevice::ReadOnly) && confFile.isReadable()) {
            QTextStream confTS = QTextStream(&confFile);
            while (!confTS.atEnd()) {
                QString confLine = confTS.readLine();
                if (confLine.contains("minimum_kstars_version=")) {
                    QString minVersion = confLine.right(confLine.length() - (confLine.indexOf("=")) - 1);
                    QStringList minVersionElements = minVersion.split(".");
                    if (minVersionElements.count() == 3) {
                        QList <int> minVersionElementInts;
                        foreach (QString element, minVersionElements) {
                            if (element.toInt() || element == "0") {
                                minVersionElementInts.append(element.toInt());
                            } else break;
                        }
                        if (minVersionElementInts.count() == minVersionElements.count()) {
                            QStringList KStarsVersionElements = QString(KSTARS_VERSION).split(".");
                            QList <int> KStarsVersionElementInts;
                            foreach (QString element, KStarsVersionElements) {
                                if (element.toInt() || element == "0") {
                                    KStarsVersionElementInts.append(element.toInt());
                                }
                            }
                            if (minVersionElementInts.at(0) <= KStarsVersionElementInts.at(0)) {
                                if (KStarsVersionElementInts.at(0) > minVersionElementInts.at(0)) {
                                    valid = true;
                                } else if (minVersionElementInts.at(1) <= KStarsVersionElementInts.at(1)) {
                                    if (KStarsVersionElementInts.at(1) > minVersionElementInts.at(1)) {
                                        valid = true;
                                    } else if (minVersionElementInts.at(2) <= KStarsVersionElementInts.at(2)) {
                                        valid = true;
                                    }
                                }
                            }

                            if (!valid) qDebug() << QString(".conf file %1 requires a minimum KStars version of %2").arg(filePath, minVersion);

                        } else qDebug() << QString(".conf file %1 does not contain a valid minimum_kstars_version string").append(filePath);
                    } else qDebug() << QString(".conf file %1 does not contain a valid minimum_kstars_version string").append(filePath);
                } else qDebug() << QString(".conf file %1 does not contain a valid minimum_kstars_version string").append(filePath);
            }
        } else qDebug() << QString("Can't access .conf file %1").arg(filePath);
    } else qDebug() << QString(".conf file %1 disappeared").arg(filePath);

    return valid;
}

QIcon extensions::getIcon(const QString &name)
{
    if (found->contains(name)) {
        extDetails m_ext = found->value("name");
        return m_ext.icon;
    } else return QIcon();
}

QString extensions::getTooltip(const QString &name)
{
    if (found->contains(name)) {
        extDetails m_ext = found->value(name);
        return m_ext.tooltip;
    } else return "";
}

void extensions::run(const QString &extension)
{
    QString processPath;
    processPath.append(QString("%1%2%3").arg(m_Directory, "/", extension));
    QStringList arguments;
    extensionProcess->setWorkingDirectory(m_Directory);
    extDetails m_ext = found->value(extension);
    if (m_ext.detached) {
        extensionProcess->startDetached(processPath, arguments);
    } else {
        extensionProcess->setProcessChannelMode(QProcess::MergedChannels);
        extensionProcess->start(processPath, arguments);
    }
    emit extensionStateChanged(Ekos::EXTENSION_START_REQUESTED);
}

void extensions::stop()
{
    emit extensionStateChanged(Ekos::EXTENSION_STOP_REQUESTED);
}

void extensions::kill()
{
    extensionProcess->kill();
}
