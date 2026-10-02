#include <QCoreApplication>
#include <QPluginLoader>
#include <QDebug>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    for (const auto &path : {QStringLiteral(WORDSPRING_RUNNER_PLUGIN), QStringLiteral(WORDSPRING_KCM_PLUGIN)}) {
        QPluginLoader loader(path);
        if (!loader.load()) {
            qCritical() << "Cannot load" << path << loader.errorString();
            return 1;
        }
        if (!loader.instance()) {
            qCritical() << "Cannot create factory" << path << loader.errorString();
            return 1;
        }
    }
    return 0;
}
