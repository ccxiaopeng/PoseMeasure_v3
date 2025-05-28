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
int                     g_hCamera = -1;             //�豸���
BYTE*                   g_pRawBuffer = NULL;        //raw����
BYTE*                   g_pMono8Buffer = NULL;        //��������ݻ�����
BYTE*                   g_pROIBuffer = NULL;       //ROIͼ�����ݻ�����
tSdkFrameHead           g_tFrameHead;               //ͼ��֡ͷ��Ϣ
tSdkCameraCapbility     g_tCapability;              //�豸������Ϣ

int                     g_SaveParameter_num = 0;    //���������
int                     g_SaveImage_type = 0;       //����ͼ���ʽ

Width_Height            g_W_H_INFO;             //��ʾ���嵽��С��ͼ���С
BYTE*                   g_readBuf = NULL;       //������ʾ������
int                     g_read_fps = 0;         //ͳ�ƶ�ȡ֡��
int                     g_disply_fps = 0;       //ͳ����ʾ֡��
int                     g_disply_fps_target = 4;       //�趨��ʾ֡��
INT64                   g_timestamp_disply_front = 0;    //ǰһ��ʾ֡ʱ�������λ0.1ms    
INT64                   g_timestamp = 0;          //��ǰ֡ʱ�������λ0.1ms

QString                 g_captureROI_path = "";
bool                    g_captureROI_flag = false; //�Ƿ�ɼ�������ROIͼ��

        
MainWindow::MainWindow(QWidget* parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindowClass), m_scene(nullptr), m_image_item(nullptr)
{

    if (init_SDK() == -1) {
        status = 0;
        return;
    }

    m_disply_fps = 4;   //Ĭ����ʾ֡��4fps

    ui->setupUi(this);
    m_scene = new QGraphicsScene(this);
    ui->gvMain->setScene(m_scene);

    //���ö�ʱ����ÿ��ˢ��һ�����״̬��
    m_timer = new QTimer(this);
    connect(m_timer, SIGNAL(timeout()), this, SLOT(camera_statues()));
    m_timer->start(1000);

    //���ͼ���ȡ�߳�
    m_thread = new CaptureThread(this);
    connect(m_thread, SIGNAL(captured(QImage)),
        this, SLOT(Image_process(QImage)), Qt::BlockingQueuedConnection);

    //���״̬����ʾ֡��UI
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


/*------ �رճ����� ------*/

void MainWindow::closeEvent(QCloseEvent* e)
{

    //linux ��Ҫ��
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
        //�������ʼ�����ͷ���Դ��
        CameraUnInit(g_hCamera);
        g_hCamera = -1;
    }

    QMainWindow::closeEvent(e);
}


/*------ ���״̬�� ------*/

void MainWindow::camera_statues()
{
    m_camera_statuesFps->setText(QString("Capture fps: %1  Display fps :%2").
        arg(QString::number(g_read_fps, 'f', 2)).arg(QString::number(g_disply_fps, 'f', 2)));
    g_read_fps = 0;
    g_disply_fps = 0;
}


/*------ ͼ����ʾˢ�´��� ------*/

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

    // ��ȡgvMain����ʾ�����С
    QSize viewSize = ui->gvMain->viewport()->size();
    // ����������ͼ������Ӧ���ڣ������ݺ��
    QImage scaledImg = img.scaled(viewSize, Qt::KeepAspectRatio, Qt::FastTransformation);

    m_image_item = m_scene->addPixmap(QPixmap::fromImage(scaledImg));
    m_scene->setSceneRect(0, 0, scaledImg.width(), scaledImg.height());

    g_disply_fps++;
}


/*------ SDK�ȳ�ʼ������------*/

int MainWindow::init_SDK()
{

    int                     iCameraCounts = 4;
    int                     iStatus = -1;
    tSdkCameraDevInfo       tCameraEnumList[4];

    //sdk��ʼ��  0 English 1����
    CameraSdkInit(1);

    //ö���豸���������豸�б�
    CameraEnumerateDevice(tCameraEnumList, &iCameraCounts);

    //û�������豸
    if (iCameraCounts == 0) {
        return -1;
    }

    //�����ʼ������ʼ���ɹ��󣬲��ܵ����κ����������صĲ����ӿ�
    iStatus = CameraInit(&tCameraEnumList[0], -1, -1, &g_hCamera);

    //��ʼ��ʧ��
    if (iStatus != CAMERA_STATUS_SUCCESS) {
        return -1;
    }
    //�����������������ṹ�塣�ýṹ���а�������������õĸ��ֲ����ķ�Χ��Ϣ����������غ����Ĳ���
    CameraGetCapability(g_hCamera, &g_tCapability);

    g_pMono8Buffer = (unsigned char*)malloc(g_tCapability.sResolutionRange.iHeightMax * g_tCapability.sResolutionRange.iWidthMax);
    g_readBuf = (unsigned char*)malloc(g_tCapability.sResolutionRange.iHeightMax * g_tCapability.sResolutionRange.iWidthMax);

    //��SDK���빤��ģʽ����ʼ��������������͵�ͼ�����ݡ�
    //�����ǰ����Ǵ���ģʽ������Ҫ���յ�����֡�Ժ�Ż����ͼ��
    CameraPlay(g_hCamera);


    //����ͼ����������ʽ����ɫ�ڰ׶�֧��RGB24λ
    if (g_tCapability.sIspCapacity.bMonoSensor) {
        CameraSetIspOutFormat(g_hCamera, CAMERA_MEDIA_TYPE_MONO8);
    }
    else {
        CameraSetIspOutFormat(g_hCamera, CAMERA_MEDIA_TYPE_RGB8);
    }
    return 0;
}

/*------ QT�����ʼ�� ------*/

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

    //�������Ĵ���ģʽ��
    CameraGetTriggerMode(hCamera, &pbySnapMode);

    //��������Ĵ���ģʽ��0��ʾ�����ɼ�ģʽ��1��ʾ�������ģʽ��2��ʾӲ������ģʽ��
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

    BOOL            AEstate = FALSE;    //Ĭ���ֶ��ع�
    int             pbyAeTarget;
    double          pfExposureTime;
    int             pusAnalogGain;
    BOOL            FlickEnable = FALSE;
    int             piFrequencySel;
    double	        m_fExpLineTime = 0;//��ǰ�����ع�ʱ�䣬��λΪus
    tSdkExpose* SdkExpose = &pCameraInfo->sExposeDesc;

    //�������Ĭ���ֶ��ع�ģʽ��
    CameraSetAeState(hCamera, false);

    // ���ó�ʼ���Ĭ���ع�ʱ��Ϊ10��s
    CameraSetExposureTime(hCamera, 10);

    // ���ó�ʼ���Ĭ��ģ������Ϊ��Сֵ
    CameraSetAnalogGain(hCamera, 6);
    
    //��������ǰ���ع�ģʽ��
    //CameraGetAeState(hCamera, &AEstate);

    //����Զ��ع������Ŀ��ֵ��
    CameraGetAeTarget(hCamera, &pbyAeTarget);

    //����Զ��ع�ʱ��Ƶ�����ܵ�ʹ��״̬��
    CameraGetAntiFlick(hCamera, &FlickEnable);

    //���������ع�ʱ�䡣
    CameraGetExposureTime(hCamera, &pfExposureTime);

    //���ͼ���źŵ�ģ������ֵ��
    CameraGetAnalogGain(hCamera, &pusAnalogGain);

    //����Զ��ع�ʱ����Ƶ����Ƶ��ѡ��
    CameraGetLightFrequency(hCamera, &piFrequencySel);

    /*
        ���һ�е��ع�ʱ�䡣����CMOS�����������ع�
        �ĵ�λ�ǰ�����������ģ���ˣ��ع�ʱ�䲢������΢��
        ���������ɵ������ǻᰴ��������ȡ�ᡣ���������
        ���þ��Ƿ���CMOS����ع�һ�ж�Ӧ��ʱ�䡣
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

/*------ QT���水ť�Ȳ��� ------*/

//���ѡ�����������
void MainWindow::on_comboBox_camera_select_activated(int index)
{

}

//����ֵ����
void MainWindow::on_spinBox_gain_valueChanged(int value)
{
    int             pusAnalogGain = 0;

    CameraSetAnalogGain(g_hCamera, value);
    CameraGetAnalogGain(g_hCamera, &pusAnalogGain);
    ui->spinBox_gain->setValue(pusAnalogGain);
}

//�ع�ʱ������
void MainWindow::on_doubleSpinBox_exposure_time_valueChanged(double value)
{
    double          m_fExpTime = 0;     //��ǰ���ع�ʱ�䣬��λΪus

    /*
    �����ع�ʱ�䡣��λΪ΢�롣����CMOS�����������ع�
    �ĵ�λ�ǰ�����������ģ���ˣ��ع�ʱ�䲢������΢��
    ���������ɵ������ǻᰴ��������ȡ�ᡣ�ڵ���
    �������趨�ع�ʱ��󣬽����ٵ���CameraGetExposureTime
    �����ʵ���趨��ֵ��
    */
    CameraSetExposureTime(g_hCamera, value);
    CameraGetExposureTime(g_hCamera, &m_fExpTime);
    ui->doubleSpinBox_exposure_time->setValue(m_fExpTime);
}

//�����ɼ�ģʽ
void MainWindow::on_radioButton_collect_clicked(bool checked)
{
    ui->radioButton_collect->setChecked(true);
    if (checked)
    {
        //�������Ĵ���ģʽ��
        CameraSetTriggerMode(g_hCamera, 0);

        ui->radioButton_collect->setChecked(true);
        ui->software_trigger_once_button->setEnabled(false);
    }
}

//����ģʽ
void MainWindow::on_radioButton_software_trigger_clicked(bool checked)
{
    ui->radioButton_software_trigger->setChecked(true);
    if (checked)
    {
        //�������Ĵ���ģʽ��
        CameraSetTriggerMode(g_hCamera, 1);

        //��������Ĵ���ģʽ��0��ʾ�����ɼ�ģʽ��1��ʾ�������ģʽ��2��ʾӲ������ģʽ��
        ui->radioButton_software_trigger->setChecked(true);
        ui->software_trigger_once_button->setEnabled(true);
    }
}

//����һ�β���
void MainWindow::on_software_trigger_once_button_clicked()
{
    //ִ��һ��������ִ�к󣬻ᴥ����CameraSetTriggerCountָ����֡����
    CameraSoftTrigger(g_hCamera);
}

//����ͼƬ·������
void MainWindow::on_pushButton_snap_path_released()
{
    QFileDialog* openFilePath = new QFileDialog(this, "Select Folder", "");     //��һ��Ŀ¼ѡ��Ի���
    openFilePath->setFileMode(QFileDialog::Directory);
    if (openFilePath->exec() == QDialog::Accepted)
    {
        QString path = openFilePath->selectedFiles()[0];

        ui->snap_path_lineEdit->setText(path);
    }
    delete openFilePath;

}

//����ͼƬ��ťȷ��
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

    //CameraSnapToBufferץ��һ��ͼ�񱣴浽buffer��
    // !!!!!!ע�⣺CameraSnapToBuffer ���л��ֱ������գ��ٶȽ�������ʵʱ��������CameraGetImageBuffer����ȡͼ���߻ص�������
    if (CameraSnapToBuffer(g_hCamera, &tFrameHead, &pbyBuffer, 1000) == CAMERA_STATUS_SUCCESS)
    {
        pbImgBuffer = (unsigned char*)malloc(g_tCapability.sResolutionRange.iHeightMax * g_tCapability.sResolutionRange.iWidthMax);

        /*
        ����õ����ԭʼ���ͼ�����ݽ��д������ӱ��Ͷȡ�
        ��ɫ�����У��������ȴ���Ч�������õ�RGB888
        ��ʽ��ͼ�����ݡ�
        */
        CameraImageProcess(g_hCamera, pbyBuffer, pbImgBuffer, &tFrameHead);

        //��ͼ�񻺳��������ݱ����ͼƬ�ļ���
        CameraSaveImage(g_hCamera, filename, pbImgBuffer, &tFrameHead, FILE_BMP_8BIT, 100);
        //�ͷ���CameraGetImageBuffer��õĻ�������
        CameraReleaseImageBuffer(g_hCamera, pbImgBuffer);
        free(pbImgBuffer);
    }
}

//�ɼ�ROIͼ��·������
void MainWindow::on_pushButton_captureROI_path_released()
{
    QFileDialog* openFilePath = new QFileDialog(this, "Select Folder", "");     //��һ��Ŀ¼ѡ��Ի���
    openFilePath->setFileMode(QFileDialog::Directory);
    if (openFilePath->exec() == QDialog::Accepted)
    {
        g_captureROI_path = openFilePath->selectedFiles()[0];

        ui->captureROI_path_lineEdit->setText(g_captureROI_path);
    }
    delete openFilePath;
}

//�ɼ�ROIͼ����������
void MainWindow::on_spinBox_captureROI_num_valueChanged(int value)
{
}

//�ɼ�ROIͼ��ʼ��ť
void MainWindow::on_pushButton_captureROI_start_released()
{
    if (g_captureROI_path.isEmpty()) {
        QMessageBox::warning(this, "Warning", "Please set the save path first!");
        return;
    }
    g_captureROI_flag = true;
}

//�ɼ�ROIͼ��ֹͣ��ť
void MainWindow::on_pushButton_captureROI_stop_released()
{
    g_captureROI_flag = false;
}
