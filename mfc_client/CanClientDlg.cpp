#include "pch.h"
#include "framework.h"
#include "CanClient.h"
#include "CanClientDlg.h"
#include "afxdialogex.h"
#include <fstream>
#include "CameraSettingsDlg.h"


// JSON
#include <nlohmann/json.hpp>
using json = nlohmann::json;

#include <pylon/ParameterIncludes.h>
using namespace Pylon;
using namespace GenApi;

// OpenCV
#include <opencv2/opencv.hpp>
#ifdef _DEBUG
#pragma comment(lib, "opencv_world4120d.lib") // (버전에 맞게 수정)
#else
#pragma comment(lib, "opencv_world4120.lib") // (버전에 맞게 수정)
#endif
using namespace cv;

// GDI+
#include <gdiplus.h>
// GDI+ Token은 CanClientDlg 멤버(m_gdiplusToken)로 이동

#define WM_CAPTURE_COMPLETE (WM_USER + 100)

// UTF-8 -> CString
static CString Utf8ToCStr(const std::string& s)
{
    if (s.empty()) return CString();
    int wlen = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    CString w;
    LPWSTR buf = w.GetBuffer(wlen);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), buf, wlen);
    w.ReleaseBuffer(wlen);
    return w;
}

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

// --- Message Map ---
BEGIN_MESSAGE_MAP(CCanClientDlg, CDialogEx)
    // ...
    ON_BN_CLICKED(IDC_BTN_SETTINGS, &CCanClientDlg::OnBnClickedBtnSettings)
    ON_WM_DESTROY()
    // [FIX] ON_WM_CTLCOLOR() 제거
    // ...
END_MESSAGE_MAP()
// --- Constructor ---
CCanClientDlg::CCanClientDlg(CWnd* pParent)
    : CDialogEx(IDD_CANCLIENT_DIALOG, pParent)
    , m_bMotionDetect(FALSE)
    , m_bCaptureInProgress(false)
    , m_strServerIP(_T("127.0.0.1")) // [FIX] public 멤버도 초기화 가능
    , m_nUploadPort(8080)             // [FIX] public 멤버도 초기화 가능
    , m_nRequestPort(8081)            // [FIX] public 멤버도 초기화 가능
    , m_productCounter(1011)
    , m_gdiplusToken(0)
    , m_timerId(0)
    , m_wsaInitialized(false)
    , m_evtShutdown(NULL)
    , m_pCaptureThread(nullptr)
    , m_bTimerBusy(false) // [FIX] 타이머 플래그 초기화
    , m_dTopFps(-1.0)
    , m_dTopExposure(-1.0)
    , m_dTopGain(-1.0)
    , m_dSideFps(-1.0)
    , m_dSideExposure(-1.0)
    , m_dSideGain(-1.0)
{
    m_hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);
    // [FIX] 브러시 관련 코드 모두 제거
}

// --- Destructor ---
CCanClientDlg::~CCanClientDlg() noexcept
{
    // [FIX] 브러시 관련 코드 모두 제거
}


void CCanClientDlg::DoDataExchange(CDataExchange* pDX)
{
    CDialogEx::DoDataExchange(pDX);
    DDX_Control(pDX, IDC_LIST_HISTORY, m_historyList);
    DDX_Control(pDX, IDC_CHECK_MOTION, m_checkMotionDetect);
    DDX_Check(pDX, IDC_CHECK_MOTION, m_bMotionDetect);
}

// --- GDI+ Initialization ---
void CCanClientDlg::InitGDIPlus()
{
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    Gdiplus::GdiplusStartup(&m_gdiplusToken, &gdiplusStartupInput, NULL);
    AddLog(L"[INFO] GDI+ 초기화 완료.");
}

void CCanClientDlg::ShutdownGDIPlus()
{
    if (m_gdiplusToken != 0)
    {
        Gdiplus::GdiplusShutdown(m_gdiplusToken);
        AddLog(L"[INFO] GDI+ 종료 완료.");
    }
}

void CCanClientDlg::InitHistoryList()
{
    // 리스트 컨트롤(m_historyList)의 스타일을 설정합니다.
    m_historyList.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

    // 리스트 컨트롤에 컬럼(열)을 추가합니다.
    // (LoadHistoryFromFile / AddToHistory 함수에서 사용하는 순서와 일치시킵니다)
    m_historyList.InsertColumn(0, _T("제품 ID"), LVCFMT_LEFT, 100);
    m_historyList.InsertColumn(1, _T("결함 유형"), LVCFMT_LEFT, 100);
    m_historyList.InsertColumn(2, _T("상세 내용"), LVCFMT_LEFT, 200);
    m_historyList.InsertColumn(3, _T("시간"), LVCFMT_LEFT, 150);
}
// --- OnInitDialog ---
BOOL CCanClientDlg::OnInitDialog()
{
    CDialogEx::OnInitDialog();
    // ...
    InitGDIPlus();
    m_evtShutdown = CreateEvent(NULL, TRUE, FALSE, NULL);
    // ... (WSAStartup) ...
    InitHistoryList();
    ClearCurrentResult();

    // [FIX] LoadAppSettings(); 호출 제거 (CanClient.cpp로 이동됨)
    // [FIX] LoadHistoryFromFile(); 호출 추가
    LoadHistoryFromFile();

    try {
        PylonInitialize();
        ScanPylonDevices();
        if (!OpenAssignedCameras()) { AddLog(L"[WARNING] 초기 카메라 열기 실패."); }
        else {
            if (m_camTop.IsOpen()) ApplyAdvancedCameraSettings(m_camTop, m_dTopFps, m_dTopExposure, m_dTopGain);
            if (m_camSide.IsOpen()) ApplyAdvancedCameraSettings(m_camSide, m_dSideFps, m_dSideExposure, m_dSideGain);
        }
        m_converter.OutputPixelFormat = PixelType_BGR8packed;
        m_converter.OutputBitAlignment = OutputBitAlignment_MsbAligned;
        m_timerId = SetTimer(1, 33, nullptr);
    }
    catch (const GenericException& e) { CString msg(e.GetDescription()); AfxMessageBox(msg); AddLog(CString(L"[ERROR] Pylon 초기화 실패: ") + msg); }
    catch (...) { AddLog(L"[ERROR] Pylon 초기화 중 알 수 없는 오류."); AfxMessageBox(L"Pylon 초기화 중 알 수 없는 오류 발생."); }

    return TRUE;
}

// --- Camera Functions ---
void CCanClientDlg::ScanPylonDevices()
{
    try {
        m_availableDevices.clear();
        CTlFactory& factory = CTlFactory::GetInstance();
        factory.EnumerateDevices(m_availableDevices);
        CString msg;
        msg.Format(L"[INFO] Pylon 카메라 %zu대 발견", m_availableDevices.size());
        AddLog(msg);
        for (size_t i = 0; i < m_availableDevices.size(); ++i) {
            AddLog(CString(L"  - ") + CString(m_availableDevices[i].GetFriendlyName().c_str()) + L" (" + CString(m_availableDevices[i].GetSerialNumber().c_str()) + L")");
        }
    }
    catch (const GenericException& e) { AddLog(CString(L"[ERROR] Pylon 스캔 실패: ") + CString(e.GetDescription())); }
    catch (...) { AddLog(L"[ERROR] Pylon 스캔 중 알 수 없는 오류."); }
}

bool CCanClientDlg::OpenAssignedCameras()
{
    AddLog(L"[DEBUG] OpenAssignedCameras 시작...");
    CloseAllCameras();

    CTlFactory& factory = CTlFactory::GetInstance();
    bool bSuccessTop = false;
    bool bSuccessSide = false;

    if (!m_topCamSerial.IsEmpty()) {
        AddLog(L"[DEBUG] TOP 카메라 열기 시도: " + m_topCamSerial);
        try {
            Pylon::CDeviceInfo topDevInfo;
            topDevInfo.SetSerialNumber(Pylon::String_t(CT2A(m_topCamSerial)));
            m_camTop.Attach(factory.CreateDevice(topDevInfo));
            m_camTop.Open();
            m_camTop.StartGrabbing(GrabStrategy_LatestImageOnly);
            AddLog(L"[INFO] TOP 카메라 (" + m_topCamSerial + L") 열기 성공.");
            bSuccessTop = true;
        }
        catch (const GenericException& e) { AddLog(CString(L"[ERROR] TOP 카메라 열기 실패: ") + CString(e.GetDescription())); try { if (m_camTop.IsPylonDeviceAttached()) m_camTop.DestroyDevice(); } catch (...) {} }
        catch (...) { AddLog(CString(L"[ERROR] TOP 카메라 열기 중 알 수 없는 예외 발생.")); try { if (m_camTop.IsPylonDeviceAttached()) m_camTop.DestroyDevice(); } catch (...) {} }
    }
    else { AddLog(L"[INFO] TOP 카메라가 할당되지 않았습니다."); }

    if (!m_sideCamSerial.IsEmpty() && m_sideCamSerial != m_topCamSerial) {
        AddLog(L"[DEBUG] SIDE 카메라 열기 시도: " + m_sideCamSerial);
        try {
            Pylon::CDeviceInfo sideDevInfo;
            sideDevInfo.SetSerialNumber(Pylon::String_t(CT2A(m_sideCamSerial)));
            m_camSide.Attach(factory.CreateDevice(sideDevInfo));
            m_camSide.Open();
            m_camSide.StartGrabbing(GrabStrategy_LatestImageOnly);
            AddLog(L"[INFO] SIDE 카메라 (" + m_sideCamSerial + L") 열기 성공.");
            bSuccessSide = true;
        }
        catch (const GenericException& e) { AddLog(CString(L"[ERROR] SIDE 카메라 열기 실패: ") + CString(e.GetDescription())); try { if (m_camSide.IsPylonDeviceAttached()) m_camSide.DestroyDevice(); } catch (...) {} }
        catch (...) { AddLog(CString(L"[ERROR] SIDE 카메라 열기 중 알 수 없는 예외 발생.")); try { if (m_camSide.IsPylonDeviceAttached()) m_camSide.DestroyDevice(); } catch (...) {} }
    }
    else if (m_sideCamSerial == m_topCamSerial && !m_topCamSerial.IsEmpty()) {
        AddLog(L"[WARNING] SIDE 카메라가 TOP과 동일하여 열지 않습니다.");
    }
    else { AddLog(L"[INFO] SIDE 카메라가 할당되지 않았습니다."); }

    AddLog(L"[DEBUG] OpenAssignedCameras 종료.");
    return bSuccessTop || bSuccessSide;
}

void CCanClientDlg::CloseAllCameras()
{
    AddLog(L"[DEBUG] CloseAllCameras 시작...");
    try { if (m_camTop.IsGrabbing()) m_camTop.StopGrabbing(); }
    catch (...) { /* ignore */ }
    try { if (m_camTop.IsOpen()) m_camTop.Close(); }
    catch (...) { /* ignore */ }
    try { if (m_camTop.IsPylonDeviceAttached()) m_camTop.DetachDevice(); }
    catch (...) { /* ignore */ }
    try { if (m_camSide.IsGrabbing()) m_camSide.StopGrabbing(); }
    catch (...) { /* ignore */ }
    try { if (m_camSide.IsOpen()) m_camSide.Close(); }
    catch (...) { /* ignore */ }
    try { if (m_camSide.IsPylonDeviceAttached()) m_camSide.DetachDevice(); }
    catch (...) { /* ignore */ }
    AddLog(L"[INFO] 모든 카메라 닫기 시도 완료.");
}
// --- Timer (Preview & Motion Detection) ---
void CCanClientDlg::OnTimer(UINT_PTR nIDEvent)
{
    // [FIX] Crash 방지
    if (!GetSafeHwnd())
    {
        return;
    }

    // [FIX] 타이머 중복 실행 방지
    if (nIDEvent == 1 && m_bTimerBusy)
    {
        return;
    }
    if (nIDEvent == 1)
    {
        m_bTimerBusy = true;
    }

    // 타이머 ID가 1이 아니거나, 캡처 진행 중이면 반환
    if (nIDEvent != 1 || m_bCaptureInProgress) {
        if (nIDEvent != 1) CDialogEx::OnTimer(nIDEvent);

        if (nIDEvent == 1) m_bTimerBusy = false; // [FIX] 반환 전 플래그 해제
        return;
    }

    UpdateData(TRUE); // m_bMotionDetect 값 업데이트

    try {
        CGrabResultPtr grabTop, grabSide;
        bool bMotionTop = false, bMotionSide = false;
        cv::Mat currentMatTop, currentMatSide;

        if (m_camTop.IsGrabbing()) {
            if (m_camTop.RetrieveResult(50, grabTop, TimeoutHandling_Return) && grabTop->GrabSucceeded()) {
                ConvertPylonBufferToMat(m_pylonImage, grabTop, currentMatTop);
                DrawImageBufferToCtrl((uint8_t*)m_pylonImage.GetBuffer(), (int)grabTop->GetWidth(), (int)grabTop->GetHeight(), GetDlgItem(IDC_CAM_TOP));

                if (m_bMotionDetect) {
                    bMotionTop = DetectMotion(currentMatTop, _T("TOP"));
                }
            }
            else {
                ClearPictureControl(GetDlgItem(IDC_CAM_TOP));
            }
        }
        else {
            ClearPictureControl(GetDlgItem(IDC_CAM_TOP));
        }

        if (m_camSide.IsGrabbing()) {
            if (m_camSide.RetrieveResult(50, grabSide, TimeoutHandling_Return) && grabSide->GrabSucceeded()) {
                ConvertPylonBufferToMat(m_pylonImage, grabSide, currentMatSide);
                DrawImageBufferToCtrl((uint8_t*)m_pylonImage.GetBuffer(), (int)grabSide->GetWidth(), (int)grabSide->GetHeight(), GetDlgItem(IDC_CAM_FRONT));

                if (m_bMotionDetect) {
                    bMotionSide = DetectMotion(currentMatSide, _T("SIDE"));
                }
            }
            else {
                ClearPictureControl(GetDlgItem(IDC_CAM_FRONT));
            }
        }
        else {
            ClearPictureControl(GetDlgItem(IDC_CAM_FRONT));
        }

        if (m_bMotionDetect && (bMotionTop || bMotionSide)) {
            AddLog(L"[Motion] 모션 감지됨. 캡처 시작...");
            TriggerCapture(m_camTop.IsGrabbing(), m_camSide.IsGrabbing());
        }
    }
    catch (...) { /* Log retrieve errors occasionally */ }

    CDialogEx::OnTimer(nIDEvent);

    // [FIX] 모든 작업 후 플래그 해제
    if (nIDEvent == 1)
    {
        m_bTimerBusy = false;
    }
}

// --- Motion Detection ---
void CCanClientDlg::ConvertPylonBufferToMat(CPylonImage& pylonImg, CGrabResultPtr& grabResult, cv::Mat& outMat)
{
    try {
        m_converter.Convert(pylonImg, grabResult);
        const uint8_t* buf = (uint8_t*)pylonImg.GetBuffer();
        int w = (int)grabResult->GetWidth();
        int h = (int)grabResult->GetHeight();
        cv::Mat bgrFrame(h, w, CV_8UC3, (void*)buf);
        cv::cvtColor(bgrFrame, outMat, cv::COLOR_BGR2GRAY);
        cv::GaussianBlur(outMat, outMat, cv::Size(21, 21), 0);
    }
    catch (...) { AddLog(L"[ERROR] ConvertPylonBufferToMat 실패"); }
}

bool CCanClientDlg::DetectMotion(cv::Mat& processedFrame, CString role)
{
    // [NEW] Actual motion detection logic
    try {
        if (processedFrame.empty()) { AddLog(L"[ERROR] DetectMotion: 입력 프레임 비어있음."); return false; }

        cv::Mat frameDelta, thresh;
        cv::Mat* pPrevFrame = (role == _T("TOP")) ? &m_prevFrameTop : &m_prevFrameSide;

        if (!pPrevFrame) { AddLog(L"[ERROR] DetectMotion: 이전 프레임 포인터 오류."); return false; }

        if (pPrevFrame->empty()) {
            *pPrevFrame = processedFrame.clone();
            // AddLog(L"[DEBUG] DetectMotion: 첫 프레임 저장 (" + role + L")"); // Reduce log noise
            return false;
        }

        if (pPrevFrame->size() != processedFrame.size() || pPrevFrame->type() != processedFrame.type()) {
            AddLog(L"[WARNING] DetectMotion: 프레임 크기/타입 불일치. 이전 프레임 리셋 (" + role + L")");
            *pPrevFrame = processedFrame.clone();
            return false;
        }

        cv::absdiff(*pPrevFrame, processedFrame, frameDelta);
        cv::threshold(frameDelta, thresh, 25, 255, cv::THRESH_BINARY);
        cv::dilate(thresh, thresh, cv::Mat(), cv::Point(-1, -1), 2);

        std::vector<std::vector<cv::Point>> contours;
        cv::findContours(thresh, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

        bool motionDetected = false;
        for (const auto& contour : contours) {
            if (cv::contourArea(contour) > 500) { // Threshold for motion area
                motionDetected = true;
                break;
            }
        }
        *pPrevFrame = processedFrame.clone();
        return motionDetected;
    }
    catch (const cv::Exception& cvEx) {
        CString errMsg; errMsg.Format(L"[ERROR] DetectMotion OpenCV Exception: %hs", cvEx.what()); AddLog(errMsg); return false;
    }
    catch (...) { AddLog(L"[ERROR] DetectMotion 알 수 없는 오류."); return false; }
}

// --- Drawing Helpers ---
void CCanClientDlg::DrawImageBufferToCtrl(const uint8_t* data, int width, int height, CWnd* pWnd)
{
    if (!pWnd || !pWnd->GetSafeHwnd() || !data || width <= 0 || height <= 0) return;
    CClientDC dc(pWnd);
    CRect rc; pWnd->GetClientRect(&rc);
    const double srcAR = static_cast<double>(width) / static_cast<double>(height);
    const double dstAR = static_cast<double>(rc.Width()) / static_cast<double>(rc.Height());
    int drawW, drawH, drawX, drawY;
    if (srcAR > dstAR) {
        drawW = rc.Width(); drawH = static_cast<int>(drawW / srcAR); drawX = 0; drawY = (rc.Height() - drawH) / 2;
    }
    else {
        drawH = rc.Height(); drawW = static_cast<int>(drawH * srcAR); drawX = (rc.Width() - drawW) / 2; drawY = 0;
    }

    // [FIX] Windows 기본 배경색으로 칠합니다.
    dc.FillRect(rc, CBrush::FromHandle(GetSysColorBrush(COLOR_BTNFACE)));

    int oldMode = SetStretchBltMode(dc.GetSafeHdc(), HALFTONE);
    SetBrushOrgEx(dc.GetSafeHdc(), 0, 0, nullptr);
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height; // Top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 24; // BGR8
    bmi.bmiHeader.biCompression = BI_RGB;
    StretchDIBits(dc.GetSafeHdc(), drawX, drawY, drawW, drawH, 0, 0, width, height, data, &bmi, DIB_RGB_COLORS, SRCCOPY);
    SetStretchBltMode(dc.GetSafeHdc(), oldMode);
}

void CCanClientDlg::ClearPictureControl(CWnd* pWnd)
{
    if (pWnd && pWnd->GetSafeHwnd()) {
        CClientDC dc(pWnd); CRect rc; pWnd->GetClientRect(&rc);

        // [FIX] Windows 기본 배경색으로 칠합니다.
        dc.FillRect(rc, CBrush::FromHandle(GetSysColorBrush(COLOR_BTNFACE)));
    }
}

// --- Capture Logic ---
void CCanClientDlg::OnBnClickedBtnStart()
{
    UpdateData(TRUE);
    if (m_bMotionDetect) { AddLog(L"[INFO] 모션 감지 중 수동 촬영 비활성화됨."); return; }
    if (m_bCaptureInProgress) { AddLog(L"[WARNING] 이미 캡처 진행 중."); return; }
    AddLog(L"[Manual] 수동 캡처 시작...");
    TriggerCapture(m_camTop.IsOpen(), m_camSide.IsOpen());
}

void CCanClientDlg::TriggerCapture(bool bUseTop, bool bUseSide)
{
    if (!bUseTop && !bUseSide) { AddLog(L"[ERROR] 캡처할 카메라 없음."); return; }

    m_bCaptureInProgress = true;
    GetDlgItem(IDC_BTN_START)->EnableWindow(FALSE);
    m_checkMotionDetect.EnableWindow(FALSE);
    GetDlgItem(IDC_BTN_SETTINGS)->EnableWindow(FALSE);

    if (m_evtShutdown) ResetEvent(m_evtShutdown); // Reset before starting

    CaptureThreadParams* pParams = new CaptureThreadParams{ this, bUseTop, bUseSide };
    // [NEW] Store thread handle if needed for explicit waiting
    m_pCaptureThread = AfxBeginThread(CaptureWorkThread, pParams, THREAD_PRIORITY_NORMAL, 0, 0, nullptr);
    if (!m_pCaptureThread) {
        AddLog(L"[ERROR] 캡처 스레드 생성 실패.");
        // Re-enable UI if thread failed to start
        m_bCaptureInProgress = false;
        if (!m_bMotionDetect) GetDlgItem(IDC_BTN_START)->EnableWindow(TRUE);
        m_checkMotionDetect.EnableWindow(TRUE);
        GetDlgItem(IDC_BTN_SETTINGS)->EnableWindow(TRUE);
        delete pParams; // Clean up params
    }
}

LRESULT CCanClientDlg::OnCaptureComplete(WPARAM wParam, LPARAM lParam)
{
    m_bCaptureInProgress = false;
    m_pCaptureThread = nullptr; // Clear thread pointer
    UpdateData(TRUE);
    if (!m_bMotionDetect) { GetDlgItem(IDC_BTN_START)->EnableWindow(TRUE); }
    m_checkMotionDetect.EnableWindow(TRUE);
    GetDlgItem(IDC_BTN_SETTINGS)->EnableWindow(TRUE);
    AddLog(L"[INFO] 캡처/전송 작업 완료.");

    InspectionResult* pResult = (InspectionResult*)lParam;
    if (pResult) {
        if (pResult->defectType == _T("CAPTURE_FAIL")) { AddLog(L"[ERROR] 카메라 캡처 실패."); }
        else { UpdateCurrentResult(*pResult); AddToHistory(*pResult); AddLog(L"[SUCCESS] 검사 완료 및 결과 표시"); }
        delete pResult;
    }
    else {
        InspectionResult errResult;
        errResult.productId = GenerateProductId();
        errResult.timestamp = GetCurrentTimestamp();
        errResult.defectType = _T("에러");
        errResult.defectDetail = (wParam == 0) ? _T("서버 응답 없음") : Utf8ToCStr((char*)wParam);
        UpdateCurrentResult(errResult); AddToHistory(errResult);
        if (wParam != 0) { delete[](char*)wParam; }
        AddLog(L"[ERROR] 서버 응답 처리 실패.");
    }
    return 0;
}

UINT CCanClientDlg::CaptureWorkThread(LPVOID pParam)
{
    CaptureThreadParams* pParams = (CaptureThreadParams*)pParam;
    if (!pParams || !pParams->pDlg) { if (pParams) delete pParams; return 1; }
    pParams->pDlg->ProcessCapture(pParams->bUseTop, pParams->bUseSide);
    delete pParams;
    return 0;
}

void CCanClientDlg::ProcessCapture(bool bUseTop, bool bUseSide)
{
    if (m_timerId) { KillTimer(m_timerId); m_timerId = 0; }

    try {
        if (WaitForSingleObject(m_evtShutdown, 0) == WAIT_OBJECT_0) { AddLog(L"[THREAD] 캡처 스레드 종료 (시작 시)."); goto ThreadEnd; }

        CGrabResultPtr grabTop, grabSide;
        std::vector<unsigned char> bufTop, bufSide;
        std::string topResponse, sideResponse;
        bool topCaptureSuccess = false;
        bool sideCaptureSuccess = false;

        // Capture TOP
        if (bUseTop && m_camTop.IsGrabbing()) {
            if (m_camTop.RetrieveResult(800, grabTop, TimeoutHandling_ThrowException) && grabTop->GrabSucceeded()) {
                CPylonImage imgTop; m_converter.Convert(imgTop, grabTop);
                cv::Mat frameTop((int)grabTop->GetHeight(), (int)grabTop->GetWidth(), CV_8UC3, (void*)imgTop.GetBuffer());
                std::vector<int> params = { cv::IMWRITE_JPEG_QUALITY, 90 };
                cv::imencode(".jpg", frameTop, bufTop, params);
                SendImageToServer(bufTop, _T("TOP"), topResponse);
                topCaptureSuccess = !bufTop.empty();
            }
            else { AddLog(L"[WARNING] TOP 카메라 캡처 실패 (RetrieveResult)"); }
        }

        if (WaitForSingleObject(m_evtShutdown, 0) == WAIT_OBJECT_0) { AddLog(L"[THREAD] 캡처 스레드 종료 (TOP 캡처 후)."); goto ThreadEnd; }

        // Capture SIDE
        if (bUseSide && m_camSide.IsGrabbing()) {
            if (m_camSide.RetrieveResult(800, grabSide, TimeoutHandling_ThrowException) && grabSide->GrabSucceeded()) {
                CPylonImage imgSide; m_converter.Convert(imgSide, grabSide);
                cv::Mat frameSide((int)grabSide->GetHeight(), (int)grabSide->GetWidth(), CV_8UC3, (void*)imgSide.GetBuffer());
                std::vector<int> params = { cv::IMWRITE_JPEG_QUALITY, 90 };
                cv::imencode(".jpg", frameSide, bufSide, params);
                SendImageToServer(bufSide, _T("SIDE"), sideResponse);
                sideCaptureSuccess = !bufSide.empty();

                InspectionResult* pResult = new InspectionResult();
                pResult->productId = GenerateProductId();
                pResult->timestamp = GetCurrentTimestamp();
                if (ParseJsonResponse(sideResponse, *pResult)) { if (GetSafeHwnd()) PostMessage(WM_CAPTURE_COMPLETE, 1, (LPARAM)pResult); }
                else { char* pStr = new char[sideResponse.length() + 1]; strcpy_s(pStr, sideResponse.length() + 1, sideResponse.c_str()); if (GetSafeHwnd()) PostMessage(WM_CAPTURE_COMPLETE, (WPARAM)pStr, (LPARAM)pResult); }
                goto ThreadEnd;

            }
            else { AddLog(L"[WARNING] SIDE 카메라 캡처 실패 (RetrieveResult)"); goto CaptureFail; }
        }
        else if (bUseTop && !bUseSide) { // Only TOP
            InspectionResult* pResult = new InspectionResult();
            pResult->productId = GenerateProductId();
            pResult->timestamp = GetCurrentTimestamp();
            if (ParseJsonResponse(topResponse, *pResult)) { if (GetSafeHwnd()) PostMessage(WM_CAPTURE_COMPLETE, 1, (LPARAM)pResult); }
            else { char* pStr = new char[topResponse.length() + 1]; strcpy_s(pStr, topResponse.length() + 1, topResponse.c_str()); if (GetSafeHwnd()) PostMessage(WM_CAPTURE_COMPLETE, (WPARAM)pStr, (LPARAM)pResult); }
            goto ThreadEnd;
        }
        else {
            goto CaptureFail;
        }
    }
    catch (const GenericException& e) { CString msg(e.GetDescription()); AddLog(L"[ERROR] 스레드 카메라 에러: " + msg); goto CaptureFail; }
    catch (const cv::Exception& cvEx) { CString msg; msg.Format(L"[ERROR] 스레드 OpenCV 에러: %hs", cvEx.what()); AddLog(msg); goto CaptureFail; }
    catch (...) { AddLog(L"[ERROR] 스레드 알 수 없는 에러."); goto CaptureFail; }

CaptureFail:
    {
        InspectionResult* pResult = new InspectionResult();
        pResult->defectType = _T("CAPTURE_FAIL");
        pResult->productId = GenerateProductId();
        pResult->timestamp = GetCurrentTimestamp();
        if (GetSafeHwnd()) PostMessage(WM_CAPTURE_COMPLETE, 0, (LPARAM)pResult);
    }

ThreadEnd:
    if (GetSafeHwnd() && ::IsWindow(GetSafeHwnd())) {
        m_timerId = SetTimer(1, 33, nullptr);
    }
}

// --- Network Functions ---
bool CCanClientDlg::SendImageToServer(const std::vector<unsigned char>& imgBuffer, CString role, std::string& response)
{
    AddLog(L"[DEBUG] SendImageToServer 시작. Role: " + role);
    if (imgBuffer.empty()) { AddLog(L"[ERROR] 이미지 버퍼 비어있음"); return false; }

    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) { AddLog(L"[ERROR] 업로드 소켓 생성 실패"); return false; }

    sockaddr_in serverAddr = {};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons((u_short)m_nUploadPort);
    if (inet_pton(AF_INET, CT2A(m_strServerIP), &serverAddr.sin_addr) != 1) {
        AddLog(L"[ERROR] inet_pton 실패 (업로드 서버 IP)"); closesocket(sock); return false;
    }

    if (connect(sock, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        int err = WSAGetLastError(); CString errMsg; errMsg.Format(L"[ERROR] 업로드 서버(%s:%d) 연결 실패 (WSA: %d)", (LPCTSTR)m_strServerIP, m_nUploadPort, err); AddLog(errMsg); closesocket(sock); return false;
    }

    int fileSize = static_cast<int>(imgBuffer.size());
    int netSize = htonl(fileSize);
    if (send(sock, (char*)&netSize, sizeof(netSize), 0) != sizeof(netSize)) { AddLog(L"[ERROR] 이미지 길이 전송 실패"); closesocket(sock); return false; }
    int totalSent = 0;
    while (totalSent < fileSize) {
        int chunk = min(64 * 1024, fileSize - totalSent);
        int sent = send(sock, (const char*)imgBuffer.data() + totalSent, chunk, 0);
        if (sent <= 0) { AddLog(L"[ERROR] 이미지 데이터 전송 실패"); closesocket(sock); return false; }
        totalSent += sent;
    }

    // [FIX] C2352 Error: CString::Format is not static. Create a temporary CString object.
    CString logMsg;
    logMsg.Format(L"[INFO] 이미지 전송 완료 (%d bytes)", fileSize);
    AddLog(logMsg);

    char recvBuf[4096] = { 0 }; int recvLen = recv(sock, recvBuf, sizeof(recvBuf) - 1, 0);
    if (recvLen > 0) { recvBuf[recvLen] = '\0'; response = std::string(recvBuf); AddLog(L"[INFO] 서버 응답 수신: " + Utf8ToCStr(response)); }
    else { response.clear(); AddLog(L"[WARNING] 서버 응답 없음"); }

    closesocket(sock);
    return true;
}

// [MODIFIED] Local Image Preview Logic
void CCanClientDlg::OnDblclkListHistory(NMHDR* pNMHDR, LRESULT* pResult)
{
    LPNMITEMACTIVATE pNMItemActivate = reinterpret_cast<LPNMITEMACTIVATE>(pNMHDR);
    *pResult = 0;
    if (pNMItemActivate->iItem != -1 && pNMItemActivate->iItem < m_historyList.GetItemCount()) {
        CString productID = m_historyList.GetItemText(pNMItemActivate->iItem, 0);
        AddLog(L"[UI] 이력 더블클릭: " + productID);

        CString imgFolderPath = _T("C:\\InspectionImages\\");
        CString pathTop, pathSide;
        pathTop.Format(_T("%s%s_TOP.jpg"), (LPCTSTR)imgFolderPath, (LPCTSTR)productID);
        pathSide.Format(_T("%s%s_SIDE.jpg"), (LPCTSTR)imgFolderPath, (LPCTSTR)productID);

        CFileStatus status;
        bool bGotTop = CFile::GetStatus(pathTop, status);
        bool bGotSide = CFile::GetStatus(pathSide, status);

        if (bGotTop || bGotSide) {
            CPreviewDlg dlg(this);
            dlg.SetImagePaths(bGotTop ? pathTop : _T(""), bGotSide ? pathSide : _T(""));
            dlg.DoModal();
        }
        else {
            AddLog(L"[ERROR] 로컬에서 이미지를 찾을 수 없습니다: " + pathTop + L" or " + pathSide);
            AfxMessageBox(L"로컬(C:\\InspectionImages\\)에서 해당 이미지를 찾을 수 없습니다.");
        }
    }
}

// --- JSON Parsing ---
bool CCanClientDlg::ParseJsonResponse(const std::string& jsonStr, InspectionResult& result)
{
    if (jsonStr.empty()) { AddLog(L"[ERROR] JSON 문자열 비어있음"); return false; }
    try {
        auto j = json::parse(jsonStr);
        result.defectType = j.contains("result") ? Utf8ToCStr(j["result"].get<std::string>()) : _T("파싱오류");
        result.defectDetail = j.contains("reason") ? Utf8ToCStr(j["reason"].get<std::string>()) : _T("");
        if (result.defectDetail == _T("N/A")) result.defectDetail = _T("");
        return true;
    }
    catch (json::parse_error& e) { CStringA errMsg; errMsg.Format("[ERROR] JSON 파싱 실패: %s (JSON: %s)", e.what(), jsonStr.c_str()); AddLog(CString(errMsg)); return false; }
    catch (...) { AddLog(L"[ERROR] JSON 파싱 중 알 수 없는 오류."); return false; }
}

// --- UI Update Helpers ---
void CCanClientDlg::UpdateCurrentResult(const InspectionResult& result)
{
    SetDlgItemText(IDC_STATIC_PRODUCT_ID, result.productId);
    SetDlgItemText(IDC_STATIC_DEFECT_TYPE, result.defectType);
    SetDlgItemText(IDC_STATIC_DEFECT_DETAIL, (result.defectDetail.IsEmpty() || result.defectType == _T("정상")) ? _T("-") : result.defectDetail);
}

void CCanClientDlg::ClearCurrentResult()
{
    SetDlgItemText(IDC_STATIC_PRODUCT_ID, _T("-"));
    SetDlgItemText(IDC_STATIC_DEFECT_TYPE, _T("-"));
    SetDlgItemText(IDC_STATIC_DEFECT_DETAIL, _T("-"));
}


void CCanClientDlg::UpdateStatistics()
{
    int total = 0, normal = 0, defect = 0;
    CString today = CTime::GetCurrentTime().Format(_T("%Y-%m-%d"));
    for (const auto& rec : m_history) {
        if (rec.timestamp.Left(10) == today) {
            total++;
            if (rec.defectType == _T("정상")) normal++;
            else if (rec.defectType != _T("에러") && rec.defectType != _T("파싱오류") && rec.defectType != _T("CAPTURE_FAIL")) defect++;
        }
    }
    double ratio = (total > 0) ? (normal * 100.0 / total) : 0.0;
    CString strToday, strOkNg, strRate;
    strToday.Format(_T("오늘 검사량 : %d개"), total);
    strOkNg.Format(_T("정상 : %d개 / 불량 : %d개"), normal, defect);
    strRate.Format(_T("정상 비율 : %.1f%%"), ratio);
    SetDlgItemText(IDC_STATIC_TODAY_CNT, strToday);
    SetDlgItemText(IDC_STATIC_OK_NG, strOkNg);
    SetDlgItemText(IDC_STATIC_RATE, strRate);
}

// --- Utility Functions ---
CString CCanClientDlg::GenerateProductId()
{
    LONG newId = InterlockedIncrement(&m_productCounter);
    CString productId;
    productId.Format(_T("CK%04ld"), newId);
    return productId;
}

CString CCanClientDlg::GetCurrentTimestamp()
{
    return CTime::GetCurrentTime().Format(_T("%Y-%m-%d %H:%M:%S"));
}
// --- Settings & History File I/O ---
// [FIX] AddToHistory에서 SaveHistoryToFile 호출부가 올바른지 확인
void CCanClientDlg::AddToHistory(const InspectionResult& result)
{
    if (m_historyList.GetSafeHwnd()) {
        m_history.push_back(result);
        int idx = m_historyList.InsertItem(m_historyList.GetItemCount(), result.productId);
        m_historyList.SetItemText(idx, 1, result.defectType);
        m_historyList.SetItemText(idx, 2, result.defectDetail.IsEmpty() ? _T("-") : result.defectDetail);
        m_historyList.SetItemText(idx, 3, result.timestamp);

        SaveHistoryToFile(); // [FIX] CanClientDlg.h에 선언되어 있어야 함

        m_historyList.EnsureVisible(idx, FALSE);
        UpdateStatistics(); // [FIX] CanClientDlg.h에 선언되어 있어야 함
    }
    else { AddLog(L"[ERROR] AddToHistory: List control 핸들 오류."); }
}

// [FIX] Load/SaveHistoryToFile 구현부 (주석 처리되었던 것을 복구)
void CCanClientDlg::SaveHistoryToFile()
{
    CString folder = _T("C:\\CanClient"); CreateDirectory(folder, NULL);
    CString filePath = folder + _T("\\history.txt");
    CStdioFile file;
    if (!file.Open(filePath, CFile::modeCreate | CFile::modeWrite | CFile::typeText)) { AddLog(L"[ERROR] 히스토리 파일 저장 실패: " + filePath); return; }
    for (const auto& rec : m_history) {
        CString line;
        line.Format(_T("%s|%s|%s|%s\n"),
            rec.productId, rec.defectType,
            (rec.defectDetail.IsEmpty() ? _T("-") : rec.defectDetail),
            rec.timestamp);
        file.WriteString(line);
    }
    file.Close();
}

void CCanClientDlg::LoadHistoryFromFile()
{
    if (!m_historyList.GetSafeHwnd()) { AddLog(L"[ERROR] LoadHistoryFromFile: List control 핸들 오류."); return; }
    m_historyList.DeleteAllItems();
    m_history.clear();

    CString filePath = _T("C:\\CanClient\\history.txt");
    CStdioFile file;
    if (!file.Open(filePath, CFile::modeRead | CFile::typeText | CFile::shareDenyWrite)) { AddLog(L"[WARNING] 히스토리 파일 읽기 실패: " + filePath); return; }

    CString line;
    long maxId = 1011;

    while (file.ReadString(line)) {
        line.Trim(); if (line.IsEmpty()) continue;
        int cur = 0, count = 0;
        CString parts[4];
        while (count < 4) {
            int next = line.Find(_T('|'), cur);
            if (next == -1) {
                if (count == 3) parts[count++] = line.Mid(cur);
                break;
            }
            parts[count++] = line.Mid(cur, next - cur);
            cur = next + 1;
        }

        if (count == 4) {
            InspectionResult rec;
            rec.productId = parts[0].Trim();
            rec.defectType = parts[1].Trim();
            rec.defectDetail = parts[2].Trim();
            rec.timestamp = parts[3].Trim();
            if (rec.defectDetail == _T("-")) rec.defectDetail.Empty();

            if (!rec.productId.IsEmpty() && rec.productId.Left(2).CompareNoCase(_T("CK")) == 0) {
                m_history.push_back(rec);
                int idx = m_historyList.InsertItem(m_historyList.GetItemCount(), rec.productId);
                m_historyList.SetItemText(idx, 1, rec.defectType);
                m_historyList.SetItemText(idx, 2, rec.defectDetail.IsEmpty() ? _T("-") : rec.defectDetail);
                m_historyList.SetItemText(idx, 3, rec.timestamp);

                CString numStr = rec.productId.Mid(2);
                long num = _ttol(numStr);
                if (num > maxId) maxId = num;
            }
            else { AddLog(L"[WARNING] 히스토리 로드 중 잘못된 형식의 라인: " + line); }
        }
        else { AddLog(L"[WARNING] 히스토리 로드 중 잘못된 구분자 수: " + line); }
    }
    m_productCounter = maxId;
    file.Close();
    UpdateStatistics();
    if (m_historyList.GetItemCount() > 0) m_historyList.EnsureVisible(m_historyList.GetItemCount() - 1, FALSE);
    AddLog(L"[INFO] 히스토리 로드 완료.");
}

// --- Settings Dialog ---
void CCanClientDlg::OnBnClickedBtnSettings()
{
    AddLog(L"[INFO] 설정 창 열기...");
    ScanPylonDevices();

    // Pass current values AND camera pointers to the dialog
    m_settingsDlg.m_availableDevices = m_availableDevices;
    m_settingsDlg.m_currentTopSerial = m_topCamSerial;
    m_settingsDlg.m_currentSideSerial = m_sideCamSerial;
    m_settingsDlg.m_strServerIP = m_strServerIP;
    m_settingsDlg.m_nUploadPort = m_nUploadPort;
    m_settingsDlg.m_nRequestPort = m_nRequestPort;
    m_settingsDlg.m_pCamTop = m_camTop.IsOpen() ? &m_camTop : nullptr; // Pass pointer only if open
    m_settingsDlg.m_pCamSide = m_camSide.IsOpen() ? &m_camSide : nullptr;

    if (m_settingsDlg.DoModal() == IDOK)
    {
        // Retrieve basic settings
        m_topCamSerial = m_settingsDlg.m_selectedTopSerial;
        m_sideCamSerial = m_settingsDlg.m_selectedSideSerial;
        m_strServerIP = m_settingsDlg.m_strServerIP;
        m_nUploadPort = m_settingsDlg.m_nUploadPort;
        m_nRequestPort = m_settingsDlg.m_nRequestPort;

        // Retrieve advanced settings (saved in m_settingsDlg members by its OnOK)
        CString sAppliedRole = m_settingsDlg.m_sSelectedCamRole;
        double dFps = m_settingsDlg.m_dFps;
        double dExposure = m_settingsDlg.m_dExposure;
        double dGain = m_settingsDlg.m_dGain;

        CString logMsg;
        logMsg.Format(L"[INFO] 기본 설정 변경됨: TOP=%s, SIDE=%s, IP=%s, Upload=%d, Request=%d",
            (LPCTSTR)m_topCamSerial, (LPCTSTR)m_sideCamSerial, (LPCTSTR)m_strServerIP, m_nUploadPort, m_nRequestPort);
        AddLog(logMsg);
        logMsg.Format(L"[INFO] 고급 설정 값 (%s 카메라 기준): FPS=%.1f, Exposure=%.0f, Gain=%.1f",
            (LPCTSTR)sAppliedRole, dFps, dExposure, dGain);
        AddLog(logMsg);


        AddLog(L"[INFO] 카메라 다시 여는 중...");
        bool bReopened = OpenAssignedCameras(); // Reopen with potentially new serials

        // Apply advanced settings to the correct camera AFTER reopening
        if (bReopened) {
            if (sAppliedRole == _T("TOP") && m_camTop.IsOpen()) {
                ApplyAdvancedCameraSettings(m_camTop, dFps, dExposure, dGain);
                m_dTopFps = dFps; m_dTopExposure = dExposure; m_dTopGain = dGain; // Store applied values
            }
            else if (sAppliedRole == _T("SIDE") && m_camSide.IsOpen()) {
                ApplyAdvancedCameraSettings(m_camSide, dFps, dExposure, dGain);
                m_dSideFps = dFps; m_dSideExposure = dExposure; m_dSideGain = dGain; // Store applied values
            }
        }
        else {
            AfxMessageBox(L"카메라 재연결 실패. 고급 설정이 적용되지 않았을 수 있습니다.");
        }


        m_prevFrameTop.release(); // Reset motion detection
        m_prevFrameSide.release();

        //SaveAppSettings(); // Save basic AND advanced settings
    }
    else {
        AddLog(L"[INFO] 설정 변경 취소됨.");
    }
}

// --- [NEW] Apply Advanced Camera Settings ---
bool CCanClientDlg::ApplyAdvancedCameraSettings(CInstantCamera& cam, double fps, double exposure, double gain)
{
    if (!cam.IsOpen()) {
        AddLog(L"[ERROR] ApplyAdvancedCameraSettings: 카메라가 열려있지 않음.");
        return false;
    }

    bool bWasGrabbing = cam.IsGrabbing();
    try {
        if (bWasGrabbing) cam.StopGrabbing(); // Stop grabbing before changing parameters

        CString camSerial(cam.GetDeviceInfo().GetSerialNumber().c_str());
        AddLog(L"[INFO] 고급 설정 적용 시작 (" + camSerial + L")...");

        bool bSuccess = true;
        // Apply FPS (AcquisitionFrameRate) - Usually requires AcquisitionFrameRateEnable == true first
        try {
            CBooleanParameter(cam.GetNodeMap(), "AcquisitionFrameRateEnable").SetValue(true);
            if (!SetPylonFloatValue(cam, "AcquisitionFrameRate", fps)) bSuccess = false;
        }
        catch (const GenericException&) { AddLog(L"[WARNING] AcquisitionFrameRateEnable 설정 불가. FPS 적용 건너뜀."); } // Some cameras might not have enable

        // Apply Exposure (ExposureTime)
        if (!SetPylonFloatValue(cam, "ExposureTime", exposure)) bSuccess = false;

        // Apply Gain
        if (!SetPylonFloatValue(cam, "Gain", gain)) bSuccess = false;

        if (bWasGrabbing) cam.StartGrabbing(GrabStrategy_LatestImageOnly); // Restart grabbing if it was active
        AddLog(L"[INFO] 고급 설정 적용 완료 (" + camSerial + L"). 성공 여부: " + (bSuccess ? L"성공" : L"일부 실패"));
        return bSuccess;

    }
    catch (const GenericException& e) {
        AddLog(CString(L"[ERROR] ApplyAdvancedCameraSettings 실패: ") + CString(e.GetDescription()));
        if (bWasGrabbing && !cam.IsGrabbing()) {
            try { cam.StartGrabbing(GrabStrategy_LatestImageOnly); }
            catch (...) {} // Try to restart grabbing on error
        }
        return false;
    }
    catch (...) {
        AddLog(L"[ERROR] ApplyAdvancedCameraSettings 중 알 수 없는 오류.");
        if (bWasGrabbing && !cam.IsGrabbing()) {
            try { cam.StartGrabbing(GrabStrategy_LatestImageOnly); }
            catch (...) {}
        }
        return false;
    }
}

// --- [NEW] Pylon Parameter Helper ---
bool CCanClientDlg::SetPylonFloatValue(CInstantCamera& cam, const char* paramName, double value)
{
    if (!cam.IsOpen() || value < 0) return false; // Ignore if value wasn't loaded/set
    try {
        INodeMap& nodemap = cam.GetNodeMap();
        CFloatParameter param(nodemap, paramName);
        if (param.IsValid() && IsWritable(param)) {
            double minVal = param.GetMin();
            double maxVal = param.GetMax();
            double clampedValue = max(minVal, min(maxVal, value)); // Clamp value to valid range
            param.SetValue(clampedValue);
            CString msg; msg.Format(L"  - %hs 설정: %.2f (범위: %.2f-%.2f)", paramName, clampedValue, minVal, maxVal); AddLog(msg);
            return true;
        }
        else { CString msg; msg.Format(L"[WARNING] 파라미터 '%hs'를 쓰거나 찾을 수 없음.", paramName); AddLog(msg); }
    }
    catch (const GenericException& e) { CString msg; msg.Format(L"[ERROR] 파라미터 '%hs' 설정 실패: %s", paramName, e.GetDescription()); AddLog(msg); }
    catch (...) { CString msg; msg.Format(L"[ERROR] 파라미터 '%hs' 설정 중 알 수 없는 오류.", paramName); AddLog(msg); }
    return false;
}


// --- Logging ---
void CCanClientDlg::AddLog(const CString& msg) { OutputDebugString(msg + L"\n"); }
// --- Shutdown ---
void CCanClientDlg::OnDestroy()
{
    AddLog(L"[INFO] 프로그램 종료 시작...");
    // ... (m_evtShutdown, KillTimer, CloseAllCameras 등은 동일) ...
    ShutdownGDIPlus();

    // [FIX] SaveAppSettings(); 호출 제거
    // SaveAppSettings(); 

    CDialogEx::OnDestroy();
}