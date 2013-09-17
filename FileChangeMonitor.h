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

#ifndef __FILE_CHANGE_MONITOR_H__
#define __FILE_CHANGE_MONITOR_H__

#include <QRegExp>
#include <QHash>
#include <QMap>

#include <QObject>
#include <sys/inotify.h>

#include <vector>

// QString and std::string converter
#define std2QString(x)      QString(QString::fromLocal8Bit(x.c_str()))
#define Q2stdstring(x)      std::string((const char *)x.toLocal8Bit())

class FileChangeMonitor
{
public:
    explicit FileChangeMonitor();
    virtual ~FileChangeMonitor();

    bool init();

public:
    //设置网盘根目录，所有带Disk的路径变量，针对的是相对本地磁盘根目录
    //所有带KuaiPan的路径变量，针对的是相对快盘根目录
    bool setWatchRootDiskDir(const char *rootDiskDirPathName);
    //主循环，返回NULL则表示没有事件发生，否则是对应的变化文件列表
    std::vector<std::string> * EventLoop();

    //停止监控
    void disableWatchEvent() {
        mDisableWatchEventTime = (time_t)-1;
    }
    //停止监控3秒，即3秒后重新开始监控
    void disableWatchEventThreeSec() {
        mDisableWatchEventTime = time(NULL);
        if (mDisableWatchEventTime != (time_t)-1)
            mDisableWatchEventTime += 3;
        else {
            //如果调用time()失败，那么必须设置mDisableWatchEventTime为0，防止监控功能失效
            mDisableWatchEventTime = 0;
        }
    }
    //把停止监控期间的所有事件丢弃
    void discardAllEvent();
private:
    //停止监控的时间，
    //1，(time_t)-1：一直停止监控
    //2，(time_t)0：处于启用监控状态
    //3，其他值(time_t)t：从现在到t时刻处于停止监控状态，t时刻后处于开启监控状态，
    time_t mDisableWatchEventTime;

public:
    void setExcludeFolderPath(const std::vector<std::string>& excludeFolderPath) {
        this->mExcludeFolderPath = excludeFolderPath;
    }

    void setExcludeFolderPrefix(const std::vector<std::string>& excludeFolderPrefix) {
        this->mExcludeFolderPrefix = excludeFolderPrefix;
    }

    void setExcludeFolderPostfix(const std::vector<std::string>& excludeFolderPostfix) {
        this->mExcludeFolderPostfix = excludeFolderPostfix;
    }

    void setExcludeFilePrefix(const std::vector<std::string>& excludeFilePrefix) {
        this->mExcludeFilePrefix = excludeFilePrefix;
    }

    void setExcludeFilePostfix(const std::vector<std::string>& excludeFilePostfix) {
        this->mExcludeFilePostfix = excludeFilePostfix;
    }

    void setExcludeFileName(const std::vector<std::string>& excludeFileName) {
        this->mExcludeFileName = excludeFileName;
    }

private:
    //递归扫描
    bool scanAndAddWatchDiskDirRecursive(const char *diskDirPathName);
    //调用inotify_add_watch系统接口，完成加入到监控
    bool addWatchDiskDir(const char *diskDirPathName);
    //判断指定的路径是否已经加入监控，返回-1为没有，否则为对应的监控描述发wd
    int isWatchedKuaiPanDir(const char *kuaiPanDirPathName);

    //过滤文件/文件夹，即对用户指定不做同步的文件/文件夹进行事件忽略，返回true则忽略
    bool FilterExcludeFileAndPath(inotify_event *event);

    //处理事件，成功处理返回true
    bool dealWithEvent(inotify_event *event);

    //将事件对应文件/文件夹加入到mChangePath
    void addEvent2ChangePath(inotify_event *event);

    void delayAddWatchDiskDir();
    void addWatchDiskDir(inotify_event *event);
    void removeWatchDiskDirSelf(inotify_event *event);
    void removeWatchDiskDir(inotify_event *event);

private:
    //inotify文件描述符
    int mFileMonitor;

    //网盘根目录，磁盘目录路径，不带后缀/，即如果用户设置/home/lenky/test文件夹为根目录，
    //那么mRootDiskDirPathName里存的是“/home/lenky/test”，而不是“/home/lenky/test/”
    char *mRootDiskDirPathName;
    //存放<监控描述符wd, 网盘绝对路径kuaiPanDir>数据，kuaiPanDir是相对网盘根目录而言的，
    //比如用户设置/home/lenky/test文件夹为根目录，那么/home/lenky/test文件夹的子文件夹a，
    //在这里的kuaiPanDir就是/a/
    QHash<int, QString> mWatchKuaiPanDirHash;

    //存放<磁盘目录diskDirPath, 失败次数failCount>数据
    //在做文件夹拷贝时，这边在获取到事件时，即有可能是文件夹还未拷贝完成，因此导致watchDir.exists()失败，
    //所以这里需要做一个缓存，后续会尝试再次加入监控，尝试最大次数为MAX_FAIL_COUNT
    QHash<QString, int> mDelayAddWatch;

    //是否已经把发生变化的文件传出去了，这是一个布尔标记值
    bool mChangePathHaveOut;
    std::vector<std::string> mChangePath;

    //过滤一个特定路径不同步
    std::vector<std::string> mExcludeFolderPath;
    //过滤所有路径下文件夹前后缀不同步
    std::vector<std::string> mExcludeFolderPrefix;
    std::vector<std::string> mExcludeFolderPostfix;
    //过滤所有路径下文件前后缀不同步
    std::vector<std::string> mExcludeFilePrefix;
    std::vector<std::string> mExcludeFilePostfix;
    //过滤所有路径下的文件和文件夹名称不同步
    std::vector<std::string> mExcludeFileName;

};

#endif // __FILE_CHANGE_MONITOR_H__ZZ
