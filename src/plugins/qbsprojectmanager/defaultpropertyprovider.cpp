/****************************************************************************
**
** Copyright (C) 2016 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of Qt Creator.
**
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3 as published by the Free Software
** Foundation with exceptions as appearing in the file LICENSE.GPL3-EXCEPT
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-3.0.html.
**
****************************************************************************/

#include "defaultpropertyprovider.h"
#include "qbsconstants.h"

#include <coreplugin/messagemanager.h>
#include <projectexplorer/abi.h>
#include <projectexplorer/gcctoolchain.h>
#include <projectexplorer/kit.h>
#include <projectexplorer/kitinformation.h>
#include <projectexplorer/toolchain.h>
#include <projectexplorer/projectexplorerconstants.h>

#include <utils/hostosinfo.h>
#include <utils/qtcassert.h>

#include <qbs.h>

#include <ios/iosconstants.h>
#include <qnx/qnxconstants.h>
#include <winrt/winrtconstants.h>

#include <QDir>
#include <QFileInfo>
#include <QSettings>

namespace QbsProjectManager {
using namespace Constants;

namespace Internal {
using namespace ProjectExplorer::Constants;
using namespace Ios::Constants;
using namespace Qnx::Constants;
using namespace WinRt::Internal::Constants;

static QString extractToolchainPrefix(QString *compilerName)
{
    QString prefix;
    const QStringList candidates = { QLatin1String("g++"), QLatin1String("clang++"),
                                     QLatin1String("gcc"), QLatin1String("clang") };
    foreach (const QString &candidate, candidates) {
        const QString suffix = Utils::HostOsInfo::withExecutableSuffix(QLatin1Char('-')
                                                                       + candidate);
        if (compilerName->endsWith(suffix)) {
            const int idx = compilerName->lastIndexOf(QLatin1Char('-')) + 1;
            prefix = compilerName->left(idx);
            compilerName->remove(0, idx);
        }
    }
    return prefix;
}

static QStringList targetOSList(const ProjectExplorer::Abi &abi, const ProjectExplorer::Kit *k)
{
    const Core::Id device = ProjectExplorer::DeviceTypeKitInformation::deviceTypeId(k);
    QStringList os;
    switch (abi.os()) {
    case ProjectExplorer::Abi::WindowsOS:
        if (device == WINRT_DEVICE_TYPE_LOCAL ||
                device == WINRT_DEVICE_TYPE_PHONE ||
                device == WINRT_DEVICE_TYPE_EMULATOR) {
            os << QLatin1String("winrt");
        } else if (abi.osFlavor() == ProjectExplorer::Abi::WindowsCEFlavor) {
            os << QLatin1String("windowsce");
        }
        os << QLatin1String("windows");
        break;
    case ProjectExplorer::Abi::DarwinOS:
        if (device == DESKTOP_DEVICE_TYPE)
            os << QLatin1String("macos") << QLatin1String("osx");
        else if (device == IOS_DEVICE_TYPE)
            os << QLatin1String("ios");
        else if (device == IOS_SIMULATOR_TYPE)
            os << QLatin1String("ios-simulator") << QLatin1String("ios");
        os << QLatin1String("darwin") << QLatin1String("bsd") << QLatin1String("unix");
        break;
    case ProjectExplorer::Abi::LinuxOS:
        if (abi.osFlavor() == ProjectExplorer::Abi::AndroidLinuxFlavor)
            os << QLatin1String("android");
        os << QLatin1String("linux") << QLatin1String("unix");
        break;
    case ProjectExplorer::Abi::BsdOS:
        switch (abi.osFlavor()) {
        case ProjectExplorer::Abi::FreeBsdFlavor:
            os << QLatin1String("freebsd");
            break;
        case ProjectExplorer::Abi::NetBsdFlavor:
            os << QLatin1String("netbsd");
            break;
        case ProjectExplorer::Abi::OpenBsdFlavor:
            os << QLatin1String("openbsd");
            break;
        default:
            break;
        }
        os << QLatin1String("bsd") << QLatin1String("unix");
        break;
    case ProjectExplorer::Abi::UnixOS:
        if (device == QNX_QNX_OS_TYPE)
            os << QLatin1String("qnx");
        else if (abi.osFlavor() == ProjectExplorer::Abi::SolarisUnixFlavor)
            os << QLatin1String("solaris");
        os << QLatin1String("unix");
        break;
    default:
        break;
    }
    return os;
}

static QStringList toolchainList(const ProjectExplorer::ToolChain *tc)
{
    QStringList list;
    if (tc->typeId() == ProjectExplorer::Constants::CLANG_TOOLCHAIN_TYPEID)
        list << QLatin1String("clang") << QLatin1String("llvm") << QLatin1String("gcc");
    else if (tc->typeId() == ProjectExplorer::Constants::GCC_TOOLCHAIN_TYPEID)
        list << QLatin1String("gcc"); // TODO: Detect llvm-gcc
    else if (tc->typeId() == ProjectExplorer::Constants::MINGW_TOOLCHAIN_TYPEID)
        list << QLatin1String("mingw") << QLatin1String("gcc");
    else if (tc->typeId() == ProjectExplorer::Constants::MSVC_TOOLCHAIN_TYPEID)
        list << QLatin1String("msvc");
    return list;
}

QVariantMap DefaultPropertyProvider::properties(const ProjectExplorer::Kit *k,
                                                const QVariantMap &defaultData) const
{
    QTC_ASSERT(k, return defaultData);
    QVariantMap data = autoGeneratedProperties(k, defaultData);
    const QVariantMap customProperties = k->value(Core::Id(QBS_PROPERTIES_KEY_FOR_KITS)).toMap();
    for (QVariantMap::ConstIterator it = customProperties.constBegin();
         it != customProperties.constEnd(); ++it) {
        data.insert(it.key(), it.value());
    }
    return data;
}

QVariantMap DefaultPropertyProvider::autoGeneratedProperties(const ProjectExplorer::Kit *k,
                                                             const QVariantMap &defaultData) const
{
    QVariantMap data = defaultData;

    const QString sysroot = ProjectExplorer::SysRootKitInformation::sysRoot(k).toUserOutput();
    if (ProjectExplorer::SysRootKitInformation::hasSysRoot(k))
        data.insert(QLatin1String(QBS_SYSROOT), sysroot);

    ProjectExplorer::ToolChain *tcC
            = ProjectExplorer::ToolChainKitInformation::toolChain(k, ProjectExplorer::ToolChain::Language::C);
    ProjectExplorer::ToolChain *tcCxx
            = ProjectExplorer::ToolChainKitInformation::toolChain(k, ProjectExplorer::ToolChain::Language::Cxx);
    if (!tcC && !tcCxx)
        return data;

    ProjectExplorer::ToolChain *mainTc = tcCxx ? tcCxx : tcC;

    ProjectExplorer::Abi targetAbi = mainTc->targetAbi();
    if (targetAbi.architecture() != ProjectExplorer::Abi::UnknownArchitecture) {
        QString architecture = ProjectExplorer::Abi::toString(targetAbi.architecture());

        // We have to be conservative tacking on suffixes to arch names because an arch that is
        // already 64-bit may get an incorrect name as a result (i.e. Itanium)
        if (targetAbi.wordWidth() == 64) {
            switch (targetAbi.architecture()) {
            case ProjectExplorer::Abi::X86Architecture:
                architecture.append(QLatin1Char('_'));
                // fall through
            case ProjectExplorer::Abi::ArmArchitecture:
            case ProjectExplorer::Abi::MipsArchitecture:
            case ProjectExplorer::Abi::PowerPCArchitecture:
                architecture.append(QString::number(targetAbi.wordWidth()));
                break;
            default:
                break;
            }
        } else if (targetAbi.architecture() == ProjectExplorer::Abi::ArmArchitecture &&
                   targetAbi.os() == ProjectExplorer::Abi::DarwinOS) {
            architecture.append(QLatin1String("v7"));
        }

        data.insert(QLatin1String(QBS_ARCHITECTURE), qbs::canonicalArchitecture(architecture));
    }

    QStringList targetOS = targetOSList(targetAbi, k);
    if (!targetOS.isEmpty())
        data.insert(QLatin1String(QBS_TARGETOS), targetOS);

    QStringList toolchain = toolchainList(mainTc);
    if (!toolchain.isEmpty())
        data.insert(QLatin1String(QBS_TOOLCHAIN), toolchain);

    if (targetAbi.os() == ProjectExplorer::Abi::DarwinOS) {
        // Set Xcode SDK name and version - required by Qbs if a sysroot is present
        // Ideally this would be done in a better way...
        const QRegExp sdkNameRe(QLatin1String("(macosx|iphoneos|iphonesimulator)([0-9]+\\.[0-9]+)"));
        const QRegExp sdkVersionRe(QLatin1String("([0-9]+\\.[0-9]+)"));
        QDir sysrootdir(sysroot);
        const QSettings sdkSettings(sysrootdir.absoluteFilePath(QLatin1String("SDKSettings.plist")), QSettings::NativeFormat);
        const QString sdkName(sdkSettings.value(QLatin1String("CanonicalName")).toString());
        const QString sdkVersion(sdkSettings.value(QLatin1String("Version")).toString());
        if (sdkNameRe.exactMatch(sdkName) && sdkVersionRe.exactMatch(sdkVersion)) {
            for (int i = 3; i > 0; --i)
                sysrootdir.cdUp();
            data.insert(QLatin1String(CPP_PLATFORMPATH), sysrootdir.absolutePath());
            data.insert(QLatin1String(CPP_XCODESDKNAME), sdkName);
            data.insert(QLatin1String(CPP_XCODESDKVERSION), sdkVersion);
        }
    }

    Utils::FileName cCompilerPath;
    if (tcC)
        cCompilerPath = tcC->compilerCommand();

    Utils::FileName cxxCompilerPath;
    if (tcCxx)
        cxxCompilerPath = tcCxx->compilerCommand();

    const QFileInfo cFileInfo = cCompilerPath.toFileInfo();
    const QFileInfo cxxFileInfo = cxxCompilerPath.toFileInfo();
    QString cCompilerName = cFileInfo.fileName();
    QString cxxCompilerName = cxxFileInfo.fileName();
    const QString cToolchainPrefix = extractToolchainPrefix(&cCompilerName);
    const QString cxxToolchainPrefix = extractToolchainPrefix(&cCompilerName);

    QFileInfo mainFileInfo;
    QString mainCompilerName;
    QString mainToolchainPrefix;
    if (tcCxx) {
        mainFileInfo = cxxFileInfo;
        mainCompilerName = cxxCompilerName;
        mainToolchainPrefix = cxxToolchainPrefix;
    } else {
        mainFileInfo = cFileInfo;
        mainCompilerName = cCompilerName;
        mainToolchainPrefix = cToolchainPrefix;
    }

    if (!mainToolchainPrefix.isEmpty())
        data.insert(QLatin1String(CPP_TOOLCHAINPREFIX), mainToolchainPrefix);

    if (toolchain.contains(QLatin1String("msvc"))) {
        data.insert(QLatin1String(CPP_COMPILERNAME), mainCompilerName);
    } else {
        data.insert(QLatin1String(CPP_COMPILERNAME), cCompilerName);
        data.insert(QLatin1String(CPP_CXXCOMPILERNAME), cxxCompilerName);
    }

    if (tcC && tcCxx && cFileInfo.absolutePath() != cxxFileInfo.absolutePath()) {
        Core::MessageManager::write(tr("C and C++ compiler paths differ. C compiler may not work."),
                                    Core::MessageManager::ModeSwitch);
    }
    data.insert(QLatin1String(CPP_TOOLCHAINPATH), mainFileInfo.absolutePath());

    if (ProjectExplorer::GccToolChain *gcc = dynamic_cast<ProjectExplorer::GccToolChain *>(mainTc)) {
        data.insert(QLatin1String(CPP_PLATFORMCOMMONCOMPILERFLAGS), gcc->platformCodeGenFlags());
        data.insert(QLatin1String(CPP_PLATFORMLINKERFLAGS), gcc->platformLinkerFlags());
    }

    return data;
}

} // namespace Internal
} // namespace QbsProjectManager
