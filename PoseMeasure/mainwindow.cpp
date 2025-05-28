#include <QFileDialog>
#include <QGraphicsPixmapItem>
#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "capturethread.h"
#include <stdio.h>

#ifdef _WIN64
#pragma comment(lib,"MVCAMSDK_X64.lib")
#else
#pragma comment(lib,"MVCAMSDK.lib")
#endif

//SDK
int                     g_hCamera = -1;             //设备句柄
BYTE*                   g_pRawBuffer = NULL;        //raw数据
BYTE*                   g_pMono8Buffer = NULL;        //处理后数据缓存区
BYTE*                   g_pROIBuffer = NULL;       //ROI图像数据缓存区
tSdkFrameHead           g_tFrameHead;               //图像帧头信息
tSdkCameraCapbility     g_tCapability;              //设备描述信息

int                     g_SaveParameter_num = 0;    //保存参数组
int                     g_SaveImage_type = 0;       //保存图像格式

Width_Height            g_W_H_INFO;             //显示画板到大小和图像大小
BYTE*                   g_readBuf = NULL;       //画板显示数据区
int                     g_read_fps = 0;         //统计读取帧率
int                     g_disply_fps = 0;       //统计显示帧率
int                     g_disply_fps_target = 4;       //设定显示帧率
INT64                   g_timestamp_disply_front = 0;    //前一显示帧时间戳，单位0.1ms    
INT64                   g_timestamp = 0;          //当前帧时间戳，单位0.1ms

QString                 g_captureROI_path = "";
bool                    g_captureROI_flag = false; //是否采集并保存ROI图像

        
MainWindow::MainWindow(QWidget* parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindowClass), m_scene(nullptr), m_image_item(nullptr)
{

    if (init_SDK() == -1) {
        status = 0;
        return;
    }

    m_disply_fps = 4;   //默认显示帧率4fps

    ui->setupUi(this);
    m_scene = new QGraphicsScene(this);
    ui->gvMain->setScene(m_scene);

    //设置定时器，每秒刷新一次相机状态栏
    m_timer = new QTimer(this);
    connect(m_timer, SIGNAL(timeout()), this, SLOT(camera_statues()));
    m_timer->start(1000);

    //相机图像获取线程
    m_thread = new CaptureThread(this);
    connect(m_thread, SIGNAL(captured(QImage)),
        this, SLOT(Image_process(QImage)), Qt::BlockingQueuedConnection);

    //相机状态栏显示帧率UI
    m_camera_statuesFps = new QLabel(this);
    m_camera_statuesFps->setAlignment(Qt::AlignHCenter);
    ui->statusBar->addWidget(m_camera_statuesFps);

    GUI_init_parameter(g_hCamera, &g_tCapability);

    m_thread->start();
    m_thread->stream();
    status = 1;
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::changeEvent(QEvent* e)
{
    QMainWindow::changeEvent(e);
    switch (e->type()) {
    case QEvent::LanguageChange:
        ui->retranslateUi(this);
        break;
    default:
        break;
    }
}


/*------ 关闭程序处理 ------*/

void MainWindow::closeEvent(QCloseEvent* e)
{

    //linux 需要打开
        //CameraSetTriggerMode(g_hCamera, 0);

    m_thread->stop();
    while (!m_thread->wait(100))
    {
        QCoreApplication::processEvents();
    }

    if (g_readBuf != NULL) {
        free(g_readBuf);
        g_readBuf = NULL;
    }

    if (g_pMono8Buffer != NULL) {
        free(g_pMono8Buffer);
        g_pMono8Buffer = NULL;
    }

    if (g_hCamera > 0) {
        //相机反初始化。释放资源。
        CameraUnInit(g_hCamera);
        g_hCamera = -1;
    }

    QMainWindow::closeEvent(e);
}


/*------ 相机状态栏 ------*/

void MainWindow::camera_statues()
{
    m_camera_statuesFps->setText(QString("Capture fps: %1  Display fps :%2").
        arg(QString::number(g_read_fps, 'f', 2)).arg(QString::number(g_disply_fps, 'f', 2)));
    g_read_fps = 0;
    g_disply_fps = 0;
}


/*------ 图像显示刷新处理 ------*/

void MainWindow::Image_process(QImage img)
{
    if (m_thread->quit)
    {
        return;
    }

    if (m_image_item)
    {
        m_scene->removeItem(m_image_item);
        delete m_image_item;
        m_image_item = 0;
    }

    // 获取gvMain的显示区域大小
    QSize viewSize = ui->gvMain->viewport()->size();
    // 按比例缩放图像以适应窗口，保持纵横比
    QImage scaledImg = img.scaled(viewSize, Qt::KeepAspectRatio, Qt::FastTransformation);

    m_image_item = m_scene->addPixmap(QPixmap::fromImage(scaledImg));
    m_scene->setSceneRect(0, 0, scaledImg.width(), scaledImg.height());

    g_disply_fps++;
}


/*------ SDK等初始化操作------*/

int MainWindow::init_SDK()
{

    int                     iCameraCounts = 4;
    int                     iStatus = -1;
    tSdkCameraDevInfo       tCameraEnumList[4];

    //sdk初始化  0 English 1中文
    CameraSdkInit(1);

    //枚举设备，并建立设备列表
    CameraEnumerateDevice(tCameraEnumList, &iCameraCounts);

    //没有连接设备
    if (iCameraCounts == 0) {
        return -1;
    }

    //相机初始化。初始化成功后，才能调用任何其他相机相关的操作接口
    iStatus = CameraInit(&tCameraEnumList[0], -1, -1, &g_hCamera);

    //初始化失败
    if (iStatus != CAMERA_STATUS_SUCCESS) {
        return -1;
    }
    //获得相机的特性描述结构体。该结构体中包含了相机可设置的各种参数的范围信息。决定了相关函数的参数
    CameraGetCapability(g_hCamera, &g_tCapability);

    g_pMono8Buffer = (unsigned char*)malloc(g_tCapability.sResolutionRange.iHeightMax * g_tCapability.sResolutionRange.iWidthMax);
    g_readBuf = (unsigned char*)malloc(g_tCapability.sResolutionRange.iHeightMax * g_tCapability.sResolutionRange.iWidthMax);

    //让SDK进入工作模式，开始接收来自相机发送的图像数据。
    //如果当前相机是触发模式，则需要接收到触发帧以后才会更新图像。
    CameraPlay(g_hCamera);


    //设置图像处理的输出格式，彩色黑白都支持RGB24位
    if (g_tCapability.sIspCapacity.bMonoSensor) {
        CameraSetIspOutFormat(g_hCamera, CAMERA_MEDIA_TYPE_MONO8);
    }
    else {
        CameraSetIspOutFormat(g_hCamera, CAMERA_MEDIA_TYPE_RGB8);
    }
    return 0;
}

/*------ QT界面初始化 ------*/

int  MainWindow::GUI_init_parameter(int hCamera, tSdkCameraCapbility* pCameraInfo)
{
    GUI_init_exposure(hCamera, pCameraInfo);
    GUI_init_Trigger(hCamera, pCameraInfo);

    ui->snap_path_lineEdit->setText(QString("./"));

    g_SaveImage_type = 3;

    return 0;
}


int  MainWindow::GUI_init_Trigger(int hCamera, tSdkCameraCapbility* pCameraInfo)
{
    int  pbySnapMode;
    int StrobeMode = 0;
    int  uPolarity = 0;

    //获得相机的触发模式。
    CameraGetTriggerMode(hCamera, &pbySnapMode);

    //设置相机的触发模式。0表示连续采集模式；1表示软件触发模式；2表示硬件触发模式。
    switch (pbySnapMode) {
    case 0:
        ui->radioButton_collect->setChecked(true);
        ui->software_trigger_once_button->setEnabled(false);

        break;
    case 1:
        ui->radioButton_software_trigger->setChecked(true);
        ui->software_trigger_once_button->setEnabled(true);

        break;

    default:
        ui->radioButton_collect->setChecked(true);
        ui->software_trigger_once_button->setEnabled(false);
        break;
    }

    return 1;
}

int  MainWindow::GUI_init_exposure(int hCamera, tSdkCameraCapbility* pCameraInfo)
{

    BOOL            AEstate = FALSE;    //默认手动曝光
    int             pbyAeTarget;
    double          pfExposureTime;
    int             pusAnalogGain;
    BOOL            FlickEnable = FALSE;
    int             piFrequencySel;
    double	        m_fExpLineTime = 0;//当前的行曝光时间，单位为us
    tSdkExpose* SdkExpose = &pCameraInfo->sExposeDesc;

    //设置相机默认手动曝光模式。
    CameraSetAeState(hCamera, false);

    // 设置初始相机默认曝光时间为10μs
    CameraSetExposureTime(hCamera, 10);

    // 设置初始相机默认模拟增益为最小值
    CameraSetAnalogGain(hCamera, 6);
    
    //获得相机当前的曝光模式。
    //CameraGetAeState(hCamera, &AEstate);

    //获得自动曝光的亮度目标值。
    CameraGetAeTarget(hCamera, &pbyAeTarget);

    //获得自动曝光时抗频闪功能的使能状态。
    CameraGetAntiFlick(hCamera, &FlickEnable);

    //获得相机的曝光时间。
    CameraGetExposureTime(hCamera, &pfExposureTime);

    //获得图像信号的模拟增益值。
    CameraGetAnalogGain(hCamera, &pusAnalogGain);

    //获得自动曝光时，消频闪的频率选择。
    CameraGetLightFrequency(hCamera, &piFrequencySel);

    /*
        获得一行的曝光时间。对于CMOS传感器，其曝光
        的单位是按照行来计算的，因此，曝光时间并不能在微秒
        级别连续可调。而是会按照整行来取舍。这个函数的
        作用就是返回CMOS相机曝光一行对应的时间。
    */
    CameraGetExposureLineTime(hCamera, &m_fExpLineTime);

    ui->spinBox_gain->setMinimum(SdkExpose->uiAnalogGainMin);
    ui->spinBox_gain->setMaximum(SdkExpose->uiAnalogGainMax);
    ui->spinBox_gain->setValue(pusAnalogGain);

    ui->doubleSpinBox_exposure_time->setMinimum(SdkExpose->uiExposeTimeMin);
    ui->doubleSpinBox_exposure_time->setMaximum(SdkExpose->uiExposeTimeMax);
    ui->doubleSpinBox_exposure_time->setValue(pfExposureTime);

    return 1;

}

/*------ QT界面按钮等操作 ------*/

//相机选择下拉框操作
void MainWindow::on_comboBox_camera_select_activated(int index)
{

}

//增益值设置
void MainWindow::on_spinBox_gain_valueChanged(int value)
{
    int             pusAnalogGain = 0;

    CameraSetAnalogGain(g_hCamera, value);
    CameraGetAnalogGain(g_hCamera, &pusAnalogGain);
    ui->spinBox_gain->setValue(pusAnalogGain);
}

//曝光时间设置
void MainWindow::on_doubleSpinBox_exposure_time_valueChanged(double value)
{
    double          m_fExpTime = 0;     //当前的曝光时间，单位为us

    /*
    设置曝光时间。单位为微秒。对于CMOS传感器，其曝光
    的单位是按照行来计算的，因此，曝光时间并不能在微秒
    级别连续可调。而是会按照整行来取舍。在调用
    本函数设定曝光时间后，建议再调用CameraGetExposureTime
    来获得实际设定的值。
    */
    CameraSetExposureTime(g_hCamera, value);
    CameraGetExposureTime(g_hCamera, &m_fExpTime);
    ui->doubleSpinBox_exposure_time->setValue(m_fExpTime);
}

//连续采集模式
void MainWindow::on_radioButton_collect_clicked(bool checked)
{
    ui->radioButton_collect->setChecked(true);
    if (checked)
    {
        //获得相机的触发模式。
        CameraSetTriggerMode(g_hCamera, 0);

        ui->radioButton_collect->setChecked(true);
        ui->software_trigger_once_button->setEnabled(false);
    }
}

//软触发模式
void MainWindow::on_radioButton_software_trigger_clicked(bool checked)
{
    ui->radioButton_software_trigger->setChecked(true);
    if (checked)
    {
        //获得相机的触发模式。
        CameraSetTriggerMode(g_hCamera, 1);

        //设置相机的触发模式。0表示连续采集模式；1表示软件触发模式；2表示硬件触发模式。
        ui->radioButton_software_trigger->setChecked(true);
        ui->software_trigger_once_button->setEnabled(true);
    }
}

//软触发一次操作
void MainWindow::on_software_trigger_once_button_clicked()
{
    //执行一次软触发。执行后，会触发由CameraSetTriggerCount指定的帧数。
    CameraSoftTrigger(g_hCamera);
}

//保存图片路径设置
void MainWindow::on_pushButton_snap_path_released()
{
    QFileDialog* openFilePath = new QFileDialog(this, "Select Folder", "");     //打开一个目录选择对话框
    openFilePath->setFileMode(QFileDialog::Directory);
    if (openFilePath->exec() == QDialog::Accepted)
    {
        QString path = openFilePath->selectedFiles()[0];

        ui->snap_path_lineEdit->setText(path);
    }
    delete openFilePath;

}

//保存图片按钮确认
void MainWindow::on_pushButton_snap_catch_released()
{
    tSdkFrameHead	tFrameHead;
    BYTE* pbyBuffer;
    BYTE* pbImgBuffer;
    char                filename[512] = { 0 };
    QString path = ui->snap_path_lineEdit->text();

    char* dir;
    QByteArray tmp = path.toLatin1();
    dir = tmp.data();

    sprintf_s(filename, sizeof(filename), "%s/test", dir);

    //CameraSnapToBuffer抓拍一张图像保存到buffer中
    // !!!!!!注意：CameraSnapToBuffer 会切换分辨率拍照，速度较慢。做实时处理，请用CameraGetImageBuffer函数取图或者回调函数。
    if (CameraSnapToBuffer(g_hCamera, &tFrameHead, &pbyBuffer, 1000) == CAMERA_STATUS_SUCCESS)
    {
        pbImgBuffer = (unsigned char*)malloc(g_tCapability.sResolutionRange.iHeightMax * g_tCapability.sResolutionRange.iWidthMax);

        /*
        将获得的相机原始输出图像数据进行处理，叠加饱和度、
        颜色增益和校正、降噪等处理效果，最后得到RGB888
        格式的图像数据。
        */
        CameraImageProcess(g_hCamera, pbyBuffer, pbImgBuffer, &tFrameHead);

        //将图像缓冲区的数据保存成图片文件。
        CameraSaveImage(g_hCamera, filename, pbImgBuffer, &tFrameHead, FILE_BMP_8BIT, 100);
        //释放由CameraGetImageBuffer获得的缓冲区。
        CameraReleaseImageBuffer(g_hCamera, pbImgBuffer);
        free(pbImgBuffer);
    }
}

//采集ROI图像路径设置
void MainWindow::on_pushButton_captureROI_path_released()
{
    QFileDialog* openFilePath = new QFileDialog(this, "Select Folder", "");     //打开一个目录选择对话框
    openFilePath->setFileMode(QFileDialog::Directory);
    if (openFilePath->exec() == QDialog::Accepted)
    {
        g_captureROI_path = openFilePath->selectedFiles()[0];

        ui->captureROI_path_lineEdit->setText(g_captureROI_path);
    }
    delete openFilePath;
}

//采集ROI图像数量设置
void MainWindow::on_spinBox_captureROI_num_valueChanged(int value)
{
}

//采集ROI图像开始按钮
void MainWindow::on_pushButton_captureROI_start_released()
{
    if (g_captureROI_path.isEmpty()) {
        QMessageBox::warning(this, "Warning", "Please set the save path first!");
        return;
    }
    g_captureROI_flag = true;
}

//采集ROI图像停止按钮
void MainWindow::on_pushButton_captureROI_stop_released()
{
    g_captureROI_flag = false;
}
