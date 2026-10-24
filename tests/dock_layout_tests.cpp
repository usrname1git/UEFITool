#include "../UEFITool/uefitool.h"

#include <QApplication>
#include <QDockWidget>
#include <QTemporaryDir>
#include <iostream>

static unsigned failures = 0;

static void expect(bool condition, const char* name)
{
    if (!condition) {
        std::cerr << "FAIL: " << name << '\n';
        failures++;
    }
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    app.setOrganizationName("UEFIToolTests");
    app.setApplicationName("DockLayout");
    QTemporaryDir settingsDir;
    if (!settingsDir.isValid())
        return 2;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, settingsDir.path());
    UEFITool window;
    window.show();
    app.processEvents();
    auto* action = window.findChild<QAction*>("actionLockDocks");
    expect(action && action->isCheckable(), "View menu contains checkable lock action");
    if (!action)
        return 1;
    const auto docks = window.findChildren<QDockWidget*>();
    expect(!docks.isEmpty(), "production window exposes docks");
    expect(!action->isChecked(), "layout is unlocked by default");
    action->trigger();
    expect(action->isChecked(), "real action signal locks layout");
    for (auto* dock : docks) {
        expect(!(dock->features() & QDockWidget::DockWidgetMovable), "locked dock cannot move");
        expect(!(dock->features() & QDockWidget::DockWidgetFloatable), "locked dock cannot float");
        expect(dock->features() & QDockWidget::DockWidgetClosable, "locking preserves close controls");
    }
    expect(QMetaObject::invokeMethod(&window, "writeSettings", Qt::DirectConnection), "settings saved through production slot");
    QSettings settings;
    expect(settings.value("mainWindow/docksLocked").toBool(), "lock preference persisted");
    action->trigger();
    expect(!action->isChecked(), "same action unlocks layout");
    window.readSettings();
    expect(action->isChecked(), "persisted lock restored");
    expect(QMetaObject::invokeMethod(&window, "resetDocks", Qt::DirectConnection), "explicit reset remains usable");
    expect(action->isChecked(), "reset does not silently unlock layout");
    action->trigger();
    const bool wayland = QGuiApplication::platformName().contains("wayland", Qt::CaseInsensitive);
    for (auto* dock : docks) {
        expect(dock->features() & QDockWidget::DockWidgetMovable, "unlocked dock is movable");
        expect(bool(dock->features() & QDockWidget::DockWidgetFloatable) == !wayland, "unlock respects platform float restriction");
    }
    expect(QMetaObject::invokeMethod(&window, "init", Qt::DirectConnection), "new-image initialization remains usable");
    expect(!action->isChecked(), "new-image initialization preserves unlock setting");
    if (!failures)
        std::cout << "Dock layout tests passed\n";
    return failures ? 1 : 0;
}
