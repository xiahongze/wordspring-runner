#pragma once

#include <QString>
#include <stdexcept>

class WordspringError : public std::runtime_error {
public:
    WordspringError(const QString &code, const QString &message)
        : std::runtime_error(message.toStdString()), code(code), message(message) {}
    QString code;
    QString message;
};
