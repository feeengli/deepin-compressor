/*
* Copyright (C) 2019 ~ 2020 Uniontech Software Technology Co.,Ltd.
*
* Author:     chendu <chendu@uniontech.com>
*
* Maintainer: chendu <chendu@uniontech.com>
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#include "calculatesizethread.h"

#include <QElapsedTimer>
#include <QFileInfo>
#include <QMutex>
#include <QThreadPool>
#include <QtConcurrent/QtConcurrent>

static QMutex mutex; // 静态全局变量只在定义该变量的源文件内有效

CalculateSizeThread::CalculateSizeThread(QStringList sfiles, QString strArchiveFullPath, QList<FileEntry> listAddEntry, CompressOptions stOptions, QObject *parent)
    : QThread(parent)
    , m_files(sfiles)
    , m_strArchiveFullPath(strArchiveFullPath)
    , m_listAddEntry(listAddEntry)
    , m_stOptions(stOptions)
{
    qRegisterMetaType<QList<FileEntry>>("QList<FileEntry>");
    qRegisterMetaType<CompressOptions>("CompressOptions");
}

void CalculateSizeThread::set_thread_stop(bool thread_stop)
{
    m_thread_stop = thread_stop;
}

void CalculateSizeThread::run()
{
    QElapsedTimer time1;
    time1.start();
    foreach (QString file, m_files) {
        if (m_thread_stop) {
            return;
        }

        QFileInfo fileInfo(file);

        FileEntry entry;
        entry.strFullPath = fileInfo.filePath();    // 文件全路径
        entry.strFileName = fileInfo.fileName();    // 文件名
        entry.isDirectory = fileInfo.isDir();   // 是否是文件夹
        entry.qSize = fileInfo.size();   // 大小
        entry.uLastModifiedTime = fileInfo.lastModified().toTime_t();   // 最后一次修改时间

        // 待压缩文件已经不存在
        if (!fileInfo.exists()) {
            if (fileInfo.isSymLink()) {
                emit signalError(tr("The original file of %1 does not exist, please check and try again").arg(fileInfo.filePath()), fileInfo.filePath());
            } else {
                emit signalError(tr("%1 does not exist on the disk, please check and try again").arg(fileInfo.filePath()), fileInfo.filePath());
            }

            set_thread_stop(true);
            return;
        }

        // 待压缩文件不可读
        if (!fileInfo.isReadable()) {
            emit signalError(tr("You do not have permission to compress %1").arg(fileInfo.filePath()), fileInfo.filePath());

            set_thread_stop(true);
            return;
        }

        if (!entry.isDirectory) {  // 如果为文件，直接获取大小
            mutex.lock();
            m_qTotalSize += entry.qSize;
            m_listEntry << entry;
            mutex.unlock();
        } else {    // 如果是文件夹，递归获取所有子文件大小总和
            mutex.lock();
            m_listEntry << entry;
            mutex.unlock();
            QtConcurrent::run(this, &CalculateSizeThread::ConstructAddOptionsByThread, file);
        }
    }

    // 等待线程池结束
    QThreadPool::globalInstance()->waitForDone();
    if (m_thread_stop) {
        return;
    }

    m_stOptions.qTotalSize = m_qTotalSize;
    emit signalFinishCalculateSize(m_qTotalSize, m_strArchiveFullPath, m_listAddEntry, m_stOptions, m_listEntry);
    qInfo() << QString("计算大小线程结束，耗时:%1ms，文件总大小:%2B").arg(time1.elapsed()).arg(m_qTotalSize);
}

void CalculateSizeThread::ConstructAddOptionsByThread(const QString &path)
{
    QDir dir(path);
    if (!dir.exists())
        return;
    // 获得文件夹中的文件列表
    QFileInfoList list = dir.entryInfoList(QDir::AllEntries | QDir::System
                                           | QDir::NoDotAndDotDot | QDir::Hidden);

    for (int i = 0; i < list.count(); ++i) {
        if (m_thread_stop) {
            return;
        }

        QFileInfo fileInfo = list.at(i);

        FileEntry entry;
        entry.strFullPath = fileInfo.filePath();    // 文件全路径
        entry.strFileName = fileInfo.fileName();    // 文件名
        entry.isDirectory = fileInfo.isDir();   // 是否是文件夹
        entry.qSize = fileInfo.size();   // 大小
        entry.uLastModifiedTime = fileInfo.lastModified().toTime_t();   // 最后一次修改时间

        // 待压缩文件已经不存在
        if (!fileInfo.exists()) {
            if (fileInfo.isSymLink()) {
                emit signalError(tr("The original file of %1 does not exist, please check and try again").arg(fileInfo.filePath()), fileInfo.filePath());
            } else {
                emit signalError(tr("%1 does not exist on the disk, please check and try again").arg(fileInfo.filePath()), fileInfo.filePath());
            }

            set_thread_stop(true);
            return;
        }

        // 待压缩文件不可读
        if (!fileInfo.isReadable()) {
            emit signalError(tr("You do not have permission to compress %1").arg(fileInfo.filePath()), fileInfo.filePath());

            set_thread_stop(true);
            return;
        }

        if (entry.isDirectory) {
            mutex.lock();
            m_listEntry << entry;
            mutex.unlock();
            // 如果是文件夹 则将此文件夹放入线程池中进行计算
            QtConcurrent::run(this, &CalculateSizeThread::ConstructAddOptionsByThread, entry.strFullPath);
        } else {
            mutex.lock();
            // 如果是文件则直接计算大小
            m_qTotalSize += entry.qSize;
            m_listEntry << entry;
            mutex.unlock();
        }
    }
}