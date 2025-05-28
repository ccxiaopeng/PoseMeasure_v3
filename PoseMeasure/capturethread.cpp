#include "capturethread.h"
#include "mainwindow.h"
#include <QDebug>

#include <windows.h>
#include "CameraApi.h"

//SDKʹ��
extern int                  g_hCamera;          //�豸���
extern BYTE*                g_pRawBuffer;       //raw����
extern BYTE*                g_pMono8Buffer;     //��������ݻ�����
extern BYTE*                g_pROIBuffer;       //ROIͼ�����ݻ�����
extern tSdkFrameHead        g_tFrameHead;       //ͼ��֡ͷ��Ϣ
extern tSdkCameraCapbility  g_tCapability;      //�豸������Ϣ


extern Width_Height         g_W_H_INFO;         //��ʾ���嵽��С��ͼ���С
extern BYTE*                g_readBuf;          //��ʾ����buffer
extern int                  g_read_fps;         //ͳ��֡��
extern int                  g_disply_fps;       //ͳ����ʾ֡��
extern int                  g_disply_fps_target;       //�趨��ʾ֡��
extern int                  g_SaveImage_type;   //����ͼ���ʽ
extern INT64                g_timestamp_disply_front;    //ǰһ��ʾ֡ʱ���    
extern INT64                g_timestamp;          //��ǰ֡ʱ���

extern QString              g_captureROI_path;
extern bool                 g_captureROI_flag; //�Ƿ�ɼ�������ROIͼ��

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
                //��ȡ��ǰ֡��ʱ�������λ0.1����
                g_timestamp = g_tFrameHead.uiTimeStamp;

                CameraImageProcess(g_hCamera,g_pRawBuffer,g_pMono8Buffer,&g_tFrameHead);
                CameraReleaseImageBuffer(g_hCamera,g_pRawBuffer);
                
                if ((g_timestamp - g_timestamp_disply_front) / 10000.0 > 1.0 / g_disply_fps_target)
                {
                    memcpy(g_readBuf, g_pMono8Buffer, g_W_H_INFO.buffer_size);

                    if (quit) break;
                    QImage img(g_readBuf, g_W_H_INFO.sensor_width, g_W_H_INFO.sensor_height, QImage::Format_Indexed8);
                    img.setColorTable(grayColourTable);
                    emit captured(img);     //�����źţ�֪ͨ���̸߳�����ʾ
                    g_timestamp_disply_front = g_timestamp; // ������ʾʱ���
                }

                if (g_captureROI_flag) //�����Ҫ�ɼ�ROIͼ��
                {
                    captureROI();
                }

                g_read_fps++; //ͳ��ץȡ֡��

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
    // ���Ƶ�ǰ�����Ҷ�ͼ�����ݵ���ʱ������
    BYTE* pbImgBuffer = (BYTE*)malloc(g_W_H_INFO.buffer_size);
    if (pbImgBuffer == nullptr || g_pMono8Buffer == nullptr) return;
    memcpy(pbImgBuffer, g_pMono8Buffer, g_W_H_INFO.buffer_size);
    
    int width = g_W_H_INFO.sensor_width;
    int height = g_W_H_INFO.sensor_height;
    int ROI_w = 512; // ROI���
    int ROI_h = 512; // ROI�߶�
    int ROI_x = (width - ROI_w) / 2; // ROI���Ͻ�x����
    int ROI_y = (height - ROI_h) / 2; // ROI���Ͻ�y����

    std::vector<BYTE> ROI;
    ROI.resize(ROI_w * ROI_h);
    for (int r = 0; r < ROI_h; r++) {
        memcpy(&ROI[r * ROI_w], &pbImgBuffer[(ROI_y + r) * width + ROI_x], ROI_w);
    }

    //�����ļ�·�����ļ���
    char                filename[512] = { 0 };
    char* dir;
    QByteArray tmp = g_captureROI_path.toLatin1();
    dir = tmp.data();
    sprintf_s(filename, sizeof(filename), "%s/X%I64d", dir, g_timestamp);

    //����ROIͼ��
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
//    // ���Ƶ�ǰ�����Ҷ�ͼ�����ݵ���ʱ������
//    int width = g_W_H_INFO.sensor_width;
//    int height = g_W_H_INFO.sensor_height;
//    BYTE* pbImgBuffer = (BYTE*)malloc(g_W_H_INFO.buffer_size);
//    memcpy(pbImgBuffer, g_pMono8Buffer, g_W_H_INFO.buffer_size);
//
//    // 1. ����ÿһ�У��� 0 �� width-1��������ֵ��
//    std::vector<double> colSums(width, 0.0);
//    for (int col = 0; col < width; col++) {
//        double sum = 0.0;
//        for (int row = 0; row < height; row++) {
//            sum += pbImgBuffer[row * width + col];
//        }
//        colSums[col] = sum;
//    }
//    // 2. �ҵ������к�С�� (��С�к� + 30000) ����������������ƽ��ֵ
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
//    // 3. ���� ROI �� x ���꣺��ƽ��λ�ü�ȥ 580�������Ʋ�С�� 0
//    int ROI_x = std::max(x - 580, 0);
//
//    // 4. �ҳ��� x ���У���һ������ֵ���� 250 ��������
//    int firstRow = -1;
//    for (int row = 0; row < height; row++) {
//        if (pbImgBuffer[row * width + x] > 250) {
//            firstRow = row;
//            break;
//        }
//    }
//    // ROI_y ȡ��һ��������������������ȥ 1040�����Ϊ 0
//    int ROI_y = (firstRow != -1) ? std::max(firstRow - 1040, 0) : 0;
//
//    // 5. �����趨��ROI �Ŀ�Ⱥ͸߶ȷֱ�Ϊ��
//    //    ROI_w = min(1160, width - ROI_x)
//    //    ROI_h = min(1060, height - ROI_y)
//    int ROI_w = std::min(1160, width - ROI_x);
//    int ROI_h = std::min(1060, height - ROI_y);
//
//    // 6. ��ȡ ROI ͼ�񣨸��� ROI �������ݵ�һ�� vector �У�
//    std::vector<BYTE> ROI;
//    ROI.resize(ROI_w * ROI_h);
//    for (int r = 0; r < ROI_h; r++) {
//        memcpy(&ROI[r * ROI_w], &pbImgBuffer[(ROI_y + r) * width + ROI_x], ROI_w);
//    }
//
//    // 7. ���� ROI ��������������ֵ�� (50, 255) ֮�������λ�ú�ֵ
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
//    // ���������Ϣ
//    qDebug() << "ROI extracted at (" << ROI_x << "," << ROI_y << "), size: (" << ROI_w << "x" << ROI_h << ")";
//    qDebug() << "Valid pixels count:" << xs.size();
//
//    free(pbImgBuffer);
//}