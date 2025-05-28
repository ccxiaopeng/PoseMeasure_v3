#include "capturethread.h"
#include "mainwindow.h"
#include <QDebug>

#include <windows.h>
#include "CameraApi.h"

//SDK使用
extern int                  g_hCamera[2];        //设备句柄
extern BYTE*                g_pRawBuffer[2];     //raw数据
extern BYTE*                g_pMono8Buffer[2];   //单色数据缓冲区
extern BYTE*                g_pROIBuffer[2];     //ROI图像数据缓冲区
extern tSdkFrameHead        g_tFrameHead[2];     //图像帧头信息
extern tSdkCameraCapbility  g_tCapability[2];    //设备描述信息


extern Width_Height         g_W_H_INFO[2];       //显示窗口到实际图像大小
extern BYTE*                g_readBuf[2];        //显示数据buffer
extern int                  g_read_fps[2];       //统计帧率
extern int                  g_disply_fps[2];     //统计显示帧率
extern int                  g_disply_fps_target; //设定显示帧率
extern int                  g_SaveImage_type;    //保存图像格式
extern INT64                g_timestamp_disply_front[2]; //前一显示帧时间戳    
extern INT64                g_timestamp[2];      //当前帧时间戳
extern int                  g_cameraCount;       //实际检测到的相机数量

extern QString              g_captureROI_path;
extern bool                 g_captureROI_flag; //是否采集并保存ROI图像

CaptureThread::CaptureThread(QObject* parent) :
    QThread(parent)
{
    pause_status = true;
    quit = false;

    for (int i = 0; i < 256; i++)
    {
        grayColourTable.append(qRgb(i, i, i));
    }
}

void CaptureThread::run()
{
    forever
    {
        if (!pause_status)
        {
            if (quit) break;
            
            // 处理所有相机的图像采集
            for (int i = 0; i < g_cameraCount; i++) {
                if (CameraGetImageBuffer(g_hCamera[i], &g_tFrameHead[i], &g_pRawBuffer[i], 100) == CAMERA_STATUS_SUCCESS)
                {
                    //获取当前帧的时间戳，单位0.1毫秒
                    g_timestamp[i] = g_tFrameHead[i].uiTimeStamp;

                    CameraImageProcess(g_hCamera[i], g_pRawBuffer[i], g_pMono8Buffer[i], &g_tFrameHead[i]);
                    CameraReleaseImageBuffer(g_hCamera[i], g_pRawBuffer[i]);
                    
                    if ((g_timestamp[i] - g_timestamp_disply_front[i]) / 10000.0 > 1.0 / g_disply_fps_target)
                    {
                        memcpy(g_readBuf[i], g_pMono8Buffer[i], g_W_H_INFO[i].buffer_size);

                        if (quit) break;
                        QImage img(g_readBuf[i], g_W_H_INFO[i].sensor_width, g_W_H_INFO[i].sensor_height, QImage::Format_Indexed8);
                        img.setColorTable(grayColourTable);
                        emit captured(img, i);     //发射信号，通知主线程更新显示
                        g_timestamp_disply_front[i] = g_timestamp[i]; // 更新显示时间戳
                    }

                    if (g_captureROI_flag) //如果需要采集ROI图像
                    {
                        captureROI(i);
                    }

                    g_read_fps[i]++; //统计抓取帧率
                }
            }
        }
        else usleep(1000);
        
        if (quit) break;
    }
}

void CaptureThread::stream()
{
    pause_status = false;
}

void CaptureThread::pause()
{
    pause_status = true;
}

void CaptureThread::stop()
{
    pause_status = false;
    quit = true;
}


void CaptureThread::captureROI(int cameraIndex)
{
    if (cameraIndex < 0 || cameraIndex >= g_cameraCount) return;
    
    // 复制当前相机灰度图像数据到临时缓冲区
    BYTE* pbImgBuffer = (BYTE*)malloc(g_W_H_INFO[cameraIndex].buffer_size);
    if (pbImgBuffer == nullptr || g_pMono8Buffer[cameraIndex] == nullptr) return;
    memcpy(pbImgBuffer, g_pMono8Buffer[cameraIndex], g_W_H_INFO[cameraIndex].buffer_size);
    
    int width = g_W_H_INFO[cameraIndex].sensor_width;
    int height = g_W_H_INFO[cameraIndex].sensor_height;
    int ROI_w = 512; // ROI宽度
    int ROI_h = 512; // ROI高度
    int ROI_x = (width - ROI_w) / 2; // ROI左上角x坐标
    int ROI_y = (height - ROI_h) / 2; // ROI左上角y坐标

    std::vector<BYTE> ROI;
    ROI.resize(ROI_w * ROI_h);
    for (int r = 0; r < ROI_h; r++) {
        memcpy(&ROI[r * ROI_w], &pbImgBuffer[(ROI_y + r) * width + ROI_x], ROI_w);
    }

    //构造文件路径和文件名
    char filename[512] = { 0 };
    char* dir;
    QByteArray tmp = g_captureROI_path.toLatin1();
    dir = tmp.data();
    sprintf_s(filename, sizeof(filename), "%s/Camera%d_X%I64d", dir, cameraIndex + 1, g_timestamp[cameraIndex]);

    //保存ROI图像
    QImage imgROI(&ROI[0], ROI_w, ROI_h, QImage::Format_Indexed8);
    imgROI.setColorTable(grayColourTable);
    imgROI.save(QString::fromLatin1(filename) + ".bmp", "BMP");

    free(pbImgBuffer);
}