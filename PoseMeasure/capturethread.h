#pragma once

#include <QThread>
#include <QImage>


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

    void captureROI();

    bool quit;


signals:
    void captured(QImage img);
private:
    bool pause_status;


    QVector<QRgb> grayColourTable;
    QVector<QRgb> ColourTable;

public slots:

};