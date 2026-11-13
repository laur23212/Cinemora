#include "TutorialContent.h"

#include <QFile>

TutorialContent::TutorialContent(QObject *parent)
    : QObject(parent)
{
}

QString TutorialContent::readJson()
{
    m_error.clear();
    QFile file(QStringLiteral(":/tutorial/tutorial.json"));
    if (!file.open(QIODevice::ReadOnly)) {
        m_error = QStringLiteral("cannot open :/tutorial/tutorial.json (%1)").arg(file.errorString());
        return {};
    }
    return QString::fromUtf8(file.readAll());
}
