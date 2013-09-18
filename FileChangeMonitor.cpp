/***************************************************************************
 *   Copyright (C) 2013~2013 by Lenky                                      *
 *   lenky0401@gmail.com                                                   *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.              *
 ***************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/inotify.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/inotify.h>
#include <signal.h>
#include <dirent.h>
#include <QMap>
#include <QString>
#include <QDebug>
#include <QDir>

#include "FileChangeMonitor.h"

#define MAX_FAIL_COUNT 10

#define EVENT_SIZE  (sizeof(struct inotify_event))
#define BUF_LEN     (1024 * (EVENT_SIZE + 16))
#define CARE_EVENT_MASK (IN_MODIFY | IN_ATTRIB | IN_MOVED_FROM | IN_MOVED_TO | \
    IN_CREATE | IN_DELETE | IN_DELETE_SELF | IN_MOVE_SELF | IN_DONT_FOLLOW)

FileChangeMonitor::FileChangeMonitor()
{
    mDisableWatchEventTime = 0;
    mChangePathHaveOut = false;
}

FileChangeMonitor::~FileChangeMonitor()
{
    free(mRootDiskDirPathName);
    close(mFileMonitor);
}

bool FileChangeMonitor::init()
{
    if ((mFileMonitor = inotify_init()) == -1)
        return false;

    //设置描述符为非阻塞，这个很重要，因为后面对该描述符会进行循环read
    fcntl(mFileMonitor, F_SETFL, O_NONBLOCK);

    return true;
}

int FileChangeMonitor::isWatchedKuaiPanDir(const char *kuaiPanDirPathName)
{
    QHashIterator<int, QString> iter(mWatchKuaiPanDirHash);

    while (iter.hasNext()) {
        iter.next();
        if (iter.value() == kuaiPanDirPathName)
            return iter.key();
    }

    return -1;
}

bool FileChangeMonitor::addWatchDiskDir(const char *diskDirPathName)
{
    int wd;
    QDir watchDir;
    char *buf;
    int pathLen;
    char *p;
    char *kuaiPanDirPathName;

    pathLen = strlen(diskDirPathName);
    if ((buf = (char *)malloc(pathLen + 2)) == NULL) {
        perror("malloc error");
        return false;
    }

    memset(buf, 0, pathLen + 2);
    memcpy(buf, diskDirPathName, pathLen);

    watchDir = QDir(buf);
    if (!watchDir.exists()) {
        free(buf);
        return false;
    }

    if (buf[pathLen - 1] != '/')
        buf[pathLen] = '/';

    if ((p = strstr(buf, mRootDiskDirPathName)) == NULL) {
        free(buf);
        return false;
    }

    kuaiPanDirPathName = buf + strlen(mRootDiskDirPathName);
    //不考虑已加入监控的情况，直接重复加入，以免遗漏
    //if (isWatchedKuaiPanDir(kuaiPanDirPathName) != -1) {
    //    free(buf);
    //    return true;
    //}

    wd = inotify_add_watch(mFileMonitor, buf, CARE_EVENT_MASK);
    if (wd == -1) {
        perror("inotify_add_watch error");
        free(buf);
        return false;
    }

//    qDebug() << buf;
//    qDebug() << (buf + strlen(mRootDiskDirPathName) - 1);
    mWatchKuaiPanDirHash.insert(wd, kuaiPanDirPathName);

    free(buf);
    return true;
}

bool FileChangeMonitor::setWatchRootDiskDir(const char *rootDiskDirPathName)
{
    int pathLen;

    pathLen = strlen(rootDiskDirPathName);
    if ((mRootDiskDirPathName = (char *)malloc(pathLen + 2)) == NULL) {
        perror("malloc error");
        return false;
    }

    memset(mRootDiskDirPathName, 0, pathLen + 2);
    memcpy(mRootDiskDirPathName, rootDiskDirPathName, pathLen);

    if (mRootDiskDirPathName[pathLen - 1] == '/')
        mRootDiskDirPathName[pathLen - 1] = '\0';

    return scanAndAddWatchDiskDirRecursive(rootDiskDirPathName);
}

bool FileChangeMonitor::scanAndAddWatchDiskDirRecursive(const char *diskDirPathName)
{
    QDir watchDir;
    QFileInfoList list;
    QFileInfoList::Iterator iter;

    watchDir = QDir(diskDirPathName);
    if (!watchDir.exists()) {
        mDelayAddWatch.insert(diskDirPathName, 1);
        return false;
    }

    if (!addWatchDiskDir(diskDirPathName)) {
        mDelayAddWatch.insert(diskDirPathName, 1);
        return false;
    }

    watchDir.setFilter(QDir::Dirs | QDir::Hidden);
    list = watchDir.entryInfoList();
    for (iter = list.begin(); iter != list.end(); ++ iter) {
        if (iter->isDir() && "." != iter->fileName() && ".." != iter->fileName()) {

            scanAndAddWatchDiskDirRecursive(Q2stdstring(iter->absoluteFilePath()).data());
        }
    }

    return true;

}

//过滤文件/文件夹，即对用户指定不做同步的文件/文件夹进行事件忽略，返回true则忽略
bool FileChangeMonitor::FilterExcludeFileAndPath(inotify_event *event)
{
    int wd;
    int len;
    QString name;
    QString totalDiskPath;
    std::vector<std::string>::iterator iter;

    wd = event->wd;
    len = event->len;

    if (len == 0 || mWatchKuaiPanDirHash.contains(wd) == false)
        return true;

    name = event->name;
    totalDiskPath = mRootDiskDirPathName + mWatchKuaiPanDirHash[wd] + name;
    if (event->mask & IN_ISDIR) {
        totalDiskPath += "/";
    }
//    qDebug() << totalDiskPath;

    //过滤一个特定路径不同步
    //std::vector<std::string> mExcludeFolderPath;
    for (iter = mExcludeFolderPath.begin(); iter != mExcludeFolderPath.end(); ++ iter) {
        if (totalDiskPath.startsWith(std2QString(((std::string)*iter))) == true)
            return true;
    }

    if (event->mask & IN_ISDIR) {
        //过滤所有路径下文件夹前缀不同步
        //std::vector<std::string> mExcludeFolderPrefix;
        for (iter = mExcludeFolderPrefix.begin(); iter != mExcludeFolderPrefix.end(); ++ iter) {
            if (name.startsWith(std2QString(((std::string)*iter))) == true)
                return true;
        }

        //过滤所有路径下文件夹后缀不同步
        //std::vector<std::string> mExcludeFolderPostfix;
        for (iter = mExcludeFolderPostfix.begin(); iter != mExcludeFolderPostfix.end(); ++ iter) {
            if (name.endsWith(std2QString(((std::string)*iter))) == true)
                return true;
        }
    } else {
        //过滤所有路径下文件前缀不同步
        //std::vector<std::string> mExcludeFilePrefix;
        for (iter = mExcludeFilePrefix.begin(); iter != mExcludeFilePrefix.end(); ++ iter) {
            if (name.startsWith(std2QString(((std::string)*iter))) == true)
                return true;
        }

        //过滤所有路径下文件后缀不同步
        //std::vector<std::string> mExcludeFilePostfix;
        for (iter = mExcludeFilePostfix.begin(); iter != mExcludeFilePostfix.end(); ++ iter) {
            if (name.endsWith(std2QString(((std::string)*iter))) == true)
                return true;
        }

    }

    //过滤所有路径下的文件和文件夹名称不同步
    //std::vector<std::string> mExcludeFileName;
    for (iter = mExcludeFileName.begin(); iter != mExcludeFileName.end(); ++ iter) {
        if (name == std2QString(((std::string)*iter)))
            return true;
    }

    return false;
}

void FileChangeMonitor::delayAddWatchDiskDir()
{
    QHashIterator<QString, int> iter(mDelayAddWatch);
    while (iter.hasNext()) {
        iter.next();

        QString totalDiskPath = iter.key();
        int failCount = iter.value();

        if (failCount > MAX_FAIL_COUNT) {
            mDelayAddWatch.remove(totalDiskPath);
            continue;
        }

        if (!addWatchDiskDir(Q2stdstring(totalDiskPath).data())) {
            mDelayAddWatch.insert(totalDiskPath, ++ failCount);
        } else {
            mDelayAddWatch.remove(totalDiskPath);
        }
    }
}

void FileChangeMonitor::addWatchDiskDir(inotify_event *event)
{
    QString totalDiskPath;

    if (event->mask & IN_ISDIR == 0 || event->len == 0 || mWatchKuaiPanDirHash.contains(event->wd) == false)
        return;

    totalDiskPath = mRootDiskDirPathName + mWatchKuaiPanDirHash[event->wd] + event->name + "/";

//    qDebug() << totalDiskPath;
    scanAndAddWatchDiskDirRecursive(Q2stdstring(totalDiskPath).data());
}

void FileChangeMonitor::removeWatchDiskDir(inotify_event *event)
{
    int wd;
    QString kuaiPanDirPathName;

    if (event->mask & IN_ISDIR == 0 || event->len == 0 || mWatchKuaiPanDirHash.contains(event->wd) == false)
        return;

    kuaiPanDirPathName = mWatchKuaiPanDirHash[event->wd] + event->name + "/";

    if ((wd = isWatchedKuaiPanDir(Q2stdstring(kuaiPanDirPathName).data())) == -1)
        return;

    inotify_rm_watch(mFileMonitor, wd);
    //不考虑inotify_rm_watch失败情况，直接删除，以便后续加入进来
    mWatchKuaiPanDirHash.remove(wd);
}

void FileChangeMonitor::removeWatchDiskDirSelf(inotify_event *event)
{
    //len must be zero
    if (event->mask & IN_ISDIR == 0 || event->len != 0 || mWatchKuaiPanDirHash.contains(event->wd) == false)
        return;

    //文件夹被删除了，没法做递归了，不过inotify本身应该会针对每个监控做事件触发
    inotify_rm_watch(mFileMonitor, event->wd);
    //不考虑inotify_rm_watch失败情况，直接删除，以便后续加入进来
    mWatchKuaiPanDirHash.remove(event->wd);
}

//处理事件，成功处理返回true
bool FileChangeMonitor::dealWithEvent(inotify_event *event)
{
    if (event->mask & IN_IGNORED) {

        //qDebug() << "event->wd: " << event->wd;
        //qDebug() << "event->mask: " << event->mask;
        //qDebug() << "event->cookie: " << event->cookie;
        //qDebug() << "event->len: " << event->len;
        //qDebug() << "event->name: " << event->name;

        return false;

    } else if ((event->mask & IN_MOVED_FROM) || (event->mask & IN_DELETE)) {
        if (event->mask & IN_ISDIR) {
            removeWatchDiskDir(event);
        }
    } else if ((event->mask & IN_DELETE_SELF) || (event->mask & IN_MOVE_SELF)) {
        if (event->mask & IN_ISDIR) {
            removeWatchDiskDirSelf(event);
        }
    } else if ((event->mask & IN_CREATE) || (event->mask & IN_MOVED_TO)) {
        if (event->mask & IN_ISDIR) {
            addWatchDiskDir(event);
        }
    } else if ((event->mask & IN_MODIFY) || (event->mask & IN_ATTRIB)) {
        //Ok, Do Nothing

    } else {
        return false;
    }


    return true;
}

//将事件对应文件/文件夹加入到mChangePath
void FileChangeMonitor::addEvent2ChangePath(inotify_event *event)
{
    QString totalKuaiPanPath;
    std::string totalKuaiPanPathString;

    if (event->len == 0 || mWatchKuaiPanDirHash.contains(event->wd) == false)
        return;

    totalKuaiPanPath = mWatchKuaiPanDirHash[event->wd] + event->name;
    if (event->mask & IN_ISDIR) {
        totalKuaiPanPath += "/";
    }

    totalKuaiPanPathString = Q2stdstring(totalKuaiPanPath);
    //这个查找，如果外部并不关心是否重复的话，可以去掉，以提高效率
    if (std::find(mChangePath.begin(), mChangePath.end(), totalKuaiPanPathString) != mChangePath.end())
        return;

    mChangePath.push_back(totalKuaiPanPathString);
}


void FileChangeMonitor::discardAllEvent()
{
    char buffer[BUF_LEN];
    int length;

    while((length = read(mFileMonitor, buffer, BUF_LEN)) > 0)
        ;
}

std::vector<std::string>* FileChangeMonitor::EventLoop()
{
    char buffer[BUF_LEN];
    fd_set watchSet;
    int length, i;
    int ret;
    struct timeval timeout;

    FD_ZERO(&watchSet);
    FD_SET(mFileMonitor, &watchSet);

    //这个timeout值不可随意改动，除非你知道你自己在做什么
    timeout.tv_sec = 1;
    timeout.tv_usec = 0;

    if (mChangePathHaveOut == true) {
        mChangePathHaveOut = false;
        mChangePath.clear();
    }

    ret = select(FD_SETSIZE, &watchSet, NULL, NULL, &timeout);

    if (mDisableWatchEventTime == (time_t)-1) {
        discardAllEvent();
        return NULL;

    } else if (mDisableWatchEventTime != (time_t)0) {
        if (mDisableWatchEventTime > (time_t)time(NULL)) {
            discardAllEvent();
            return NULL;
        } else
            mDisableWatchEventTime = 0;
    }

    if (ret > 0) {
        if (FD_ISSET(mFileMonitor, &watchSet)) {
            do {
                i = 0;
                length = read(mFileMonitor, buffer, BUF_LEN);

                while (i < length) {
                    struct inotify_event *event = (struct inotify_event *) &buffer[i];
                    i += EVENT_SIZE + event->len;

                    if (FilterExcludeFileAndPath(event))
                        continue;

                    if (dealWithEvent(event))
                        addEvent2ChangePath(event);
                }
            } while (length > 0);
        }
    } else if (((ret == -1 && errno == EINTR) || ret == 0) && mChangePath.size() > 0) {
        //如果在一个timeout内有事件发生，那么不将列表导出
        //只有在：1，发生信号，即：(ret == -1 && errno == EINTR)；
        //2，在一个timeout内没有事件发生，即ret == 0
        //才将列表导出，这样做的原因是，留个timeout时间的缓存，避免事件频繁导出，比如在做大量文件拷贝时，
        //也许就只是在文件拷贝完的最后才做一次导出，提高效率。
        if (mDelayAddWatch.size() > 0)
            delayAddWatchDiskDir();
        mChangePathHaveOut = true;
        return &mChangePath;
    }

    return NULL;
}

