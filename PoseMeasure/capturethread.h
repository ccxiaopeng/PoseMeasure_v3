#pragma once

#include <QThread>
#include <QImage>

// 前向声明
class SaveThread;
class MemoryPool;


class CaptureThread : public QThread
{
    Q_OBJECT
public:
    explicit CaptureThread(QObject* parent = 0);

public:
    void run();
    void stream();
    void pause();
    void stop();

    // 设置异步保存系统
    void setSaveThread(SaveThread* saveThread);
    void setMemoryPool(MemoryPool* memoryPool);

    void captureROI(int cameraIndex = 0);
    // 新的异步保存ROI方法
    void captureROIAsync(int cameraIndex);

    bool quit;


signals:
    void captured(QImage img, int cameraIndex);
private:
    bool pause_status;
    
    // 异步保存系统
    SaveThread* m_saveThread;
    MemoryPool* m_memoryPool;

    QVector<QRgb> grayColourTable;
    QVector<QRgb> ColourTable;

public slots:

};