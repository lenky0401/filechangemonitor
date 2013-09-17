#include <string>
#include <QString>
#include <QDebug>
#include "FileChangeMonitor.h"

int main(int argc, char *argv[])
{
    std::vector<std::string> *changePath;
    FileChangeMonitor mMonitor;

    mMonitor.init();
    mMonitor.setWatchRootDiskDir("/home/lenky/test/");
    while (1) {
        if ((changePath = mMonitor.EventLoop()) != NULL) {

            std::vector<std::string>::iterator iter;
            for (iter = changePath->begin(); iter != changePath->end(); ++ iter) {
                qDebug() << std2QString(((std::string)*iter));
            }

        }
    }
}
