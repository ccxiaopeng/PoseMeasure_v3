#include "capturethread.h"
#include "mainwindow.h"
#include <QDebug>

#include <windows.h>
#include "CameraApi.h"

//SDK使用
extern int                  g_hCamera;          //设备句柄
extern BYTE*                g_pRawBuffer;       //raw数据
extern BYTE*                g_pMono8Buffer;     //处理后数据缓存区
extern BYTE*                g_pROIBuffer;       //ROI图像数据缓存区
extern tSdkFrameHead        g_tFrameHead;       //图像帧头信息
extern tSdkCameraCapbility  g_tCapability;      //设备描述信息


extern Width_Height         g_W_H_INFO;         //显示画板到大小和图像大小
extern BYTE*                g_readBuf;          //显示数据buffer
extern int                  g_read_fps;         //统计帧率
extern int                  g_disply_fps;       //统计显示帧率
extern int                  g_disply_fps_target;       //设定显示帧率
extern int                  g_SaveImage_type;   //保存图像格式
extern INT64                g_timestamp_disply_front;    //前一显示帧时间戳    
extern INT64                g_timestamp;          //当前帧时间戳

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
            if (CameraGetImageBuffer(g_hCamera,&g_tFrameHead,&g_pRawBuffer,2000) == CAMERA_STATUS_SUCCESS)
            {
                //获取当前帧的时间戳，单位0.1毫秒
                g_timestamp = g_tFrameHead.uiTimeStamp;

                CameraImageProcess(g_hCamera,g_pRawBuffer,g_pMono8Buffer,&g_tFrameHead);
                CameraReleaseImageBuffer(g_hCamera,g_pRawBuffer);
                
                if ((g_timestamp - g_timestamp_disply_front) / 10000.0 > 1.0 / g_disply_fps_target)
                {
                    memcpy(g_readBuf, g_pMono8Buffer, g_W_H_INFO.buffer_size);

                    if (quit) break;
                    QImage img(g_readBuf, g_W_H_INFO.sensor_width, g_W_H_INFO.sensor_height, QImage::Format_Indexed8);
                    img.setColorTable(grayColourTable);
                    emit captured(img);     //发送信号，通知主线程更新显示
                    g_timestamp_disply_front = g_timestamp; // 更新显示时间戳
                }

                if (g_captureROI_flag) //如果需要采集ROI图像
                {
                    captureROI();
                }

                g_read_fps++; //统计抓取帧率

            }
            else   
            {
               printf("timeout \n");
               usleep(1000);
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


void CaptureThread::captureROI()
{
    // 复制当前处理后灰度图像数据到临时缓冲区
    BYTE* pbImgBuffer = (BYTE*)malloc(g_W_H_INFO.buffer_size);
    if (pbImgBuffer == nullptr || g_pMono8Buffer == nullptr) return;
    memcpy(pbImgBuffer, g_pMono8Buffer, g_W_H_INFO.buffer_size);
    
    int width = g_W_H_INFO.sensor_width;
    int height = g_W_H_INFO.sensor_height;
    int ROI_w = 512; // ROI宽度
    int ROI_h = 512; // ROI高度
    int ROI_x = (width - ROI_w) / 2; // ROI左上角x坐标
    int ROI_y = (height - ROI_h) / 2; // ROI左上角y坐标

    std::vector<BYTE> ROI;
    ROI.resize(ROI_w * ROI_h);
    for (int r = 0; r < ROI_h; r++) {
        memcpy(&ROI[r * ROI_w], &pbImgBuffer[(ROI_y + r) * width + ROI_x], ROI_w);
    }

    //创建文件路径和文件名
    char                filename[512] = { 0 };
    char* dir;
    QByteArray tmp = g_captureROI_path.toLatin1();
    dir = tmp.data();
    sprintf_s(filename, sizeof(filename), "%s/X%I64d", dir, g_timestamp);

    //保存ROI图像
    QImage imgROI(&ROI[0], ROI_w, ROI_h, QImage::Format_Indexed8);
    imgROI.setColorTable(grayColourTable);
    imgROI.save(QString::fromLatin1(filename) + ".bmp", "BMP");

    free(pbImgBuffer);
}

//#include <vector>
//#include <algorithm>
//#include <cmath>

//void CaptureThread::captureROI()
//{
//    // 复制当前处理后灰度图像数据到临时缓冲区
//    int width = g_W_H_INFO.sensor_width;
//    int height = g_W_H_INFO.sensor_height;
//    BYTE* pbImgBuffer = (BYTE*)malloc(g_W_H_INFO.buffer_size);
//    memcpy(pbImgBuffer, g_pMono8Buffer, g_W_H_INFO.buffer_size);
//
//    // 1. 计算每一列（从 0 到 width-1）的像素值和
//    std::vector<double> colSums(width, 0.0);
//    for (int col = 0; col < width; col++) {
//        double sum = 0.0;
//        for (int row = 0; row < height; row++) {
//            sum += pbImgBuffer[row * width + col];
//        }
//        colSums[col] = sum;
//    }
//    // 2. 找到所有列和小于 (最小列和 + 30000) 的列索引，并计算平均值
//    double sumXMin = *std::min_element(colSums.begin(), colSums.end());
//    std::vector<int> candidateCols;
//    for (int i = 0; i < width; i++) {
//        if (colSums[i] < sumXMin + 30000)
//            candidateCols.push_back(i);
//    }
//    double candidateMean = 0.0;
//    if (!candidateCols.empty()) {
//        int sumCandidates = 0;
//        for (int col : candidateCols) {
//            sumCandidates += col;
//        }
//        candidateMean = static_cast<double>(sumCandidates) / candidateCols.size();
//    }
//    int x = static_cast<int>(std::round(candidateMean));
//
//    // 3. 计算 ROI 的 x 坐标：将平均位置减去 580，并限制不小于 0
//    int ROI_x = std::max(x - 580, 0);
//
//    // 4. 找出第 x 列中，第一个像素值大于 250 的行索引
//    int firstRow = -1;
//    for (int row = 0; row < height; row++) {
//        if (pbImgBuffer[row * width + x] > 250) {
//            firstRow = row;
//            break;
//        }
//    }
//    // ROI_y 取第一个满足条件的行索引减去 1040，最低为 0
//    int ROI_y = (firstRow != -1) ? std::max(firstRow - 1040, 0) : 0;
//
//    // 5. 根据设定，ROI 的宽度和高度分别为：
//    //    ROI_w = min(1160, width - ROI_x)
//    //    ROI_h = min(1060, height - ROI_y)
//    int ROI_w = std::min(1160, width - ROI_x);
//    int ROI_h = std::min(1060, height - ROI_y);
//
//    // 6. 提取 ROI 图像（复制 ROI 区域数据到一个 vector 中）
//    std::vector<BYTE> ROI;
//    ROI.resize(ROI_w * ROI_h);
//    for (int r = 0; r < ROI_h; r++) {
//        memcpy(&ROI[r * ROI_w], &pbImgBuffer[(ROI_y + r) * width + ROI_x], ROI_w);
//    }
//
//    // 7. 计算 ROI 区域内满足像素值在 (50, 255) 之间的像素位置和值
//    std::vector<int> xs, ys;
//    std::vector<double> vs;
//    for (int r = 0; r < ROI_h; r++) {
//        for (int c = 0; c < ROI_w; c++) {
//            BYTE pixel = ROI[r * ROI_w + c];
//            if (pixel > 50 && pixel < 255) {
//                xs.push_back(c);
//                ys.push_back(r);
//                vs.push_back(static_cast<double>(pixel));
//            }
//        }
//    }
//
//    // 输出调试信息
//    qDebug() << "ROI extracted at (" << ROI_x << "," << ROI_y << "), size: (" << ROI_w << "x" << ROI_h << ")";
//    qDebug() << "Valid pixels count:" << xs.size();
//
//    free(pbImgBuffer);
//}