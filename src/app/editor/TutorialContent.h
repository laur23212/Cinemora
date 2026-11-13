#pragma once

#include <QObject>
#include <QString>

// Dafin tutorial: hands Tutorial.qml the text of the bundled tutorial.json (resources/tutorial,
// compiled in as :/tutorial/tutorial.json). Read here, like the workspace presets, rather than
// from QML, so a failure comes back as a readable message.
class TutorialContent : public QObject
{
    Q_OBJECT

public:
    explicit TutorialContent(QObject *parent = nullptr);

    // The file's text; empty on failure, with the reason in lastError().
    Q_INVOKABLE QString readJson();
    Q_INVOKABLE QString lastError() const { return m_error; }

private:
    QString m_error;
};
