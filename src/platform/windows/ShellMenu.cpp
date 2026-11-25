#include "platform/ShellMenu.h"

#include <QCoreApplication>
#include <QDir>
#include <QSettings>

// HKEY_CURRENT_USER\Software\Classes merges into HKEY_CLASSES_ROOT, so no administrator rights
// are needed. SystemFileAssociations applies whichever app opens the file type.
static const QStringList Types{".stl", ".3mf"};
static const QString Verb = "shell/MirdainGwaith";
// The submenu's entries, shared by both file types through ExtendedSubCommandsKey.
static const QString Printers = "MirdainForward.GwaithPrinters";

void ShellMenu::update(const QList<Printer> &printers)
{
    QSettings classes("HKEY_CURRENT_USER\\Software\\Classes", QSettings::NativeFormat);
    classes.remove(Printers);
    for (const auto &type : Types) classes.remove("SystemFileAssociations/" + type + "/" + Verb);
    if (printers.isEmpty()) return;

    const auto exe = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
    for (const auto &type : Types) {
        const auto verb = "SystemFileAssociations/" + type + "/" + Verb;
        classes.setValue(verb + "/MUIVerb", "Send to Gwaith");
        classes.setValue(verb + "/Icon", exe + ",0");
        classes.setValue(verb + "/ExtendedSubCommandsKey", Printers);
    }
    // Explorer orders the entries by key name, so the keys are zero-padded to keep this order.
    // Each selected file launches Forward once.
    for (qsizetype index = 0; index < printers.size(); ++index) {
        const auto entry = QString("%1/shell/printer%2").arg(Printers).arg(index, 3, 10, QChar('0'));
        classes.setValue(entry + "/MUIVerb", printers[index].name);
        // One pass fills all four, so Explorer's own %1 (the file) is left as it is.
        classes.setValue(entry + "/command/.", QString("\"%1\" %2 %3 \"%4\"")
            .arg(exe, QString(Argument), printers[index].id, QStringLiteral("%1")));
    }
}
