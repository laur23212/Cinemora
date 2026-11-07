// Runs tests/qml/barreorder/*.qml against the bar reordering components (see that file).
#include <QQmlEngine>
#include <QtQuickTest>

class Setup : public QObject
{
    Q_OBJECT
public slots:
    void qmlEngineAvailable(QQmlEngine *engine) { engine->addImportPath(QStringLiteral(BARREORDER_IMPORTS)); }
};

QUICK_TEST_MAIN_WITH_SETUP(barreorder, Setup)
#include "tst_barreorder.moc"
