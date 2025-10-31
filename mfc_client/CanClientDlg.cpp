#include "pch.h"
#include "framework.h"
#include "CanClient.h"
#include "CanClientDlg.h"
#include "afxdialogex.h"
#include <fstream>
#include "CameraSettingsDlg.h"

// [NEW] 로컬 폴더 생성
#include <shlobj.h> 
#pragma comment(lib, "shell32.lib") // for SHCreateDirectoryEx

// [NEW] GetHistoryFilePath 헬퍼에 필요
#include <shlwapi.h> // for PathRemoveFileSpec

// JSON
#include <nlohmann/json.hpp>
using json = nlohmann::json;

#include <pylon/ParameterIncludes.h>
using namespace Pylon;
using namespace GenApi;

// OpenCV
#include <opencv2/opencv.hpp>
#include <opencv2/imgcodecs.hpp> // [NEW] for cv::imwrite

#ifdef _DEBUG
#pragma comment(lib, "opencv_world4120d.lib") // (버전에 맞게 수정)
#else
#pragma comment(lib, "opencv_world4120.lib") // (버전에 맞게 수정)
#endif
using namespace cv;

// GDI+
#include <gdiplus.h>
// GDI+ Token은 CanClientDlg 멤버(m_gdiplusToken)로 이동

// [FIX] 사용되지 않는 WM_CAPTURE_COMPLETE (WM_USER + 100) 정의 삭제
// #define WM_CAPTURE_COMPLETE (WM_USER + 100) 

// [신규] 리소스 ID (resource.h에서 가져옴)
#define IDC_BTN_EXPORT_HISTORY 1039


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
    ON_BN_CLICKED(IDC_BTN_START, &CCanClientDlg::OnBnClickedBtnStart)
    ON_BN_CLICKED(IDC_BTN_SETTINGS, &CCanClientDlg::OnBnClickedBtnSettings)
    ON_WM_DESTROY()
    ON_MESSAGE(WM_APP_POSTINIT, &CCanClientDlg::OnPostInit) // <-- 추가
    ON_WM_TIMER()
    // [NEW] 신규 기능 핸들러 매핑
    ON_WM_CTLCOLOR()
    ON_BN_CLICKED(IDC_BTN_EXPORT_HISTORY, &CCanClientDlg::OnBnClickedBtnExportHistory) // [수정] ID 1039
    ON_BN_CLICKED(IDC_CHECK_MOTION, &CCanClientDlg::OnBnClickedCheckMotion) // [FIX] 무한 루프 수정

    // [FIX] OnCaptureComplete 핸들러 연결 추가 (핵심 수정)
    ON_MESSAGE(WM_APP_CAPTURE_COMPLETE, &CCanClientDlg::OnCaptureComplete)

    // ========================================================================
    // [NEW] 더블클릭 이벤트 핸들러 연결 (핵심 수정)
    // ========================================================================
    ON_NOTIFY(NM_DBLCLK, IDC_LIST_HISTORY, &CCanClientDlg::OnDblclkListHistory)

END_MESSAGE_MAP()

// --- Constructor ---
CCanClientDlg::CCanClientDlg(CWnd* pParent)
    : CDialogEx(IDD_CANCLIENT_DIALOG, pParent)
    , m_hIcon(nullptr)
    , m_bMotionDetect(FALSE)
    , m_bCaptureInProgress(false)
    , m_strServerIP(_T("127.0.0.1"))
    , m_nUploadPort(8080)
    , m_nRequestPort(8081)
    , m_productCounter(1011)
    , m_gdiplusToken(0)
    , m_timerId(0)
    , m_wsaInitialized(false)
    , m_evtShutdown(NULL)
    , m_pCaptureThread(nullptr)
    , m_bTimerBusy(false)
    , m_dTopFps(-1.0)
    , m_dTopExposure(-1.0)
    , m_dTopGain(-1.0)
    , m_dSideFps(-1.0)
    , m_dSideExposure(-1.0)
    , m_dSideGain(-1.0)
{
    // [NEW] 불량 알림용 빨간색 브러시 생성 (연한 빨강)
    m_brushRed.CreateSolidBrush(RGB(255, 220, 220));
}

// --- Destructor ---
CCanClientDlg::~CCanClientDlg() noexcept
{
    // [NEW] 브러시 리소스 해제
    m_brushRed.DeleteObject();
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
    m_hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);
    SetIcon(m_hIcon, TRUE);
    SetIcon(m_hIcon, FALSE);

    InitGDIPlus();

    // [수정] 네트워크(WSA) 초기화 코드를 추가합니다. (촬영/서버 통신 오류 해결)
    WSADATA wsaData;
    int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (iResult != 0) {
        AfxMessageBox(L"WSAStartup 실패!");
        m_wsaInitialized = false;
        return TRUE; // 또는 FALSE로 앱 종료
    }
    m_wsaInitialized = true;
    // --- WSAStartup 추가 끝 ---

    m_evtShutdown = CreateEvent(NULL, TRUE, FALSE, NULL);
    InitHistoryList();
    ClearCurrentResult();

    LoadHistoryFromFile();

    // Pylon/SetTimer 관련 코드는 OnPostInit으로 이동됨

    PostMessage(WM_APP_POSTINIT, 0, 0);
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

// ========================================================================
// [FIX] OnTimer 함수 로직 전면 수정 (UI 먹통 버그 수정)
// ========================================================================
void CCanClientDlg::OnTimer(UINT_PTR nIDEvent)
{
    // [FIX] 부모 클래스의 OnTimer를 *항상* 먼저 호출하여 UI 메시지 큐를 처리합니다.
    // 이것이 멈춤(Freeze) 현상을 해결하는 핵심입니다.
    CDialogEx::OnTimer(nIDEvent);

    // 우리의 타이머(ID=1)가 아니면, 여기서 종료
    if (nIDEvent != 1)
    {
        return;
    }

    // --- ID=1 타이머 로직 ---

    // 캡처 진행 중이거나, 타이머가 이미 바쁘면(이전 작업 미종료) 이번 틱은 무시
    if (!GetSafeHwnd() || m_bTimerBusy || m_bCaptureInProgress)
    {
        return;
    }
    m_bTimerBusy = true; // 작업 시작 플래그

    UpdateData(TRUE); // m_bMotionDetect 값 업데이트

    try {
        CGrabResultPtr grabTop, grabSide;
        bool bMotionTop = false, bMotionSide = false;
        cv::Mat currentMatTop, currentMatSide;

        if (m_camTop.IsGrabbing()) {
            if (m_camTop.RetrieveResult(0, grabTop, TimeoutHandling_Return) && grabTop->GrabSucceeded()) {
                ConvertPylonBufferToMat(m_pylonImage, grabTop, currentMatTop);
                DrawImageBufferToCtrl((uint8_t*)m_pylonImage.GetBuffer(), (int)grabTop->GetWidth(), (int)grabTop->GetHeight(), GetDlgItem(IDC_CAM_TOP));
                if (m_bMotionDetect) {
                    bMotionTop = DetectMotion(currentMatTop, _T("TOP"));
                }
            }
        }
        else {
            ClearPictureControl(GetDlgItem(IDC_CAM_TOP));
        }

        if (m_camSide.IsGrabbing()) {
            if (m_camSide.RetrieveResult(0, grabSide, TimeoutHandling_Return) && grabSide->GrabSucceeded()) {
                ConvertPylonBufferToMat(m_pylonImage, grabSide, currentMatSide);
                DrawImageBufferToCtrl((uint8_t*)m_pylonImage.GetBuffer(), (int)grabSide->GetWidth(), (int)grabSide->GetHeight(), GetDlgItem(IDC_CAM_FRONT));
                if (m_bMotionDetect) {
                    bMotionSide = DetectMotion(currentMatSide, _T("SIDE"));
                }
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
    catch (const GenericException& e) {
        CString msg(e.GetDescription());
        AddLog(L"[Timer] OnTimer CATCH EXCEPTION! " + msg);
    }
    catch (...) {
        AddLog(L"[Timer] OnTimer CATCH UNKNOWN EXCEPTION!");
    }

    m_bTimerBusy = false; // 작업 완료 플래그
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
    try {
        if (processedFrame.empty()) { AddLog(L"[ERROR] DetectMotion: 입력 프레임 비어있음."); return false; }

        cv::Mat frameDelta, thresh;
        cv::Mat* pPrevFrame = (role == _T("TOP")) ? &m_prevFrameTop : &m_prevFrameSide;

        if (!pPrevFrame) { AddLog(L"[ERROR] DetectMotion: 이전 프레임 포인터 오류."); return false; }

        if (pPrevFrame->empty()) {
            *pPrevFrame = processedFrame.clone();
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

// ========================================================================
// [NEW] 모션 감지 체크박스 클릭 시 (무한 루프 방지)
// ========================================================================
void CCanClientDlg::OnBnClickedCheckMotion()
{
    UpdateData(TRUE); // m_bMotionDetect 값 갱신

    if (m_bMotionDetect)
    {
        // 모션 감지 켬 -> 수동 시작 버튼 비활성화
        GetDlgItem(IDC_BTN_START)->EnableWindow(FALSE);
        AddLog(L"[INFO] 모션 감지 시작.");
    }
    else
    {
        // 모션 감지 끔 -> 수동 시작 버튼 활성화 (캡처 중이 아닐 때만)
        if (!m_bCaptureInProgress)
        {
            GetDlgItem(IDC_BTN_START)->EnableWindow(TRUE);
        }
        AddLog(L"[INFO] 모션 감지 중지.");
    }
}


// ========================================================================
// [FIX] TriggerCapture 수정 (체크박스 제어 제거)
// ========================================================================
void CCanClientDlg::TriggerCapture(bool bUseTop, bool bUseSide)
{
    if (!bUseTop && !bUseSide) { AddLog(L"[ERROR] 캡처할 카메라 없음."); return; }

    m_bCaptureInProgress = true;
    GetDlgItem(IDC_BTN_START)->EnableWindow(FALSE);
    // [FIX] m_checkMotionDetect->EnableWindow(FALSE); 제거 (무한 루프 원인)
    GetDlgItem(IDC_BTN_SETTINGS)->EnableWindow(FALSE);

    if (m_evtShutdown) ResetEvent(m_evtShutdown); // Reset before starting

    CaptureThreadParams* pParams = new CaptureThreadParams{ this, bUseTop, bUseSide };
    m_pCaptureThread = AfxBeginThread(CaptureWorkThread, pParams, THREAD_PRIORITY_NORMAL, 0, 0, nullptr);
    if (!m_pCaptureThread) {
        AddLog(L"[ERROR] 캡처 스레드 생성 실패.");
        // Re-enable UI if thread failed to start
        m_bCaptureInProgress = false;
        if (!m_bMotionDetect) GetDlgItem(IDC_BTN_START)->EnableWindow(TRUE);
        // [FIX] m_checkMotionDetect->EnableWindow(TRUE); 제거
        GetDlgItem(IDC_BTN_SETTINGS)->EnableWindow(TRUE);
        delete pParams; // Clean up params
    }
}

// ========================================================================
// [FIX] OnCaptureComplete 수정 (타이머 재시작 로직 추가)
// ========================================================================
LRESULT CCanClientDlg::OnCaptureComplete(WPARAM wParam, LPARAM lParam)
{
    m_bCaptureInProgress = false;
    m_pCaptureThread = nullptr; // Clear thread pointer
    UpdateData(TRUE); // m_bMotionDetect 최신 상태 확인

    // [FIX] 모션 감지가 켜져있으면 수동 시작 버튼은 계속 비활성화 상태 유지
    if (!m_bMotionDetect) {
        GetDlgItem(IDC_BTN_START)->EnableWindow(TRUE);
    }
    // [FIX] m_checkMotionDetect->EnableWindow(TRUE); 제거 (무한 루프 원인)
    GetDlgItem(IDC_BTN_SETTINGS)->EnableWindow(TRUE);
    AddLog(L"[INFO] 캡처/전송 작업 완료.");

    InspectionResult* pResult = (InspectionResult*)lParam;

    // pResult가 NULL이면(치명적 오류) 아무것도 하지 않고 반환
    if (!pResult) {
        AddLog(L"[FATAL] OnCaptureComplete pResult가 NULL입니다.");
        if (wParam != 0 && wParam != 1) { delete[](char*)wParam; } // pStr 메모리 누수 방지
        goto CaptureCompleteEnd;
    }

    // wParam == 0: CAPTURE_FAIL 또는 전송실패
    if (wParam == 0)
    {
        // pResult->defectType은 스레드에서 "CAPTURE_FAIL" 또는 "전송실패"로 이미 채워져 있음
        AddLog(L"[ERROR] 캡처 또는 전송 실패.");
    }
    // wParam == 1: 파싱 성공
    else if (wParam == 1)
    {
        AddLog(L"[SUCCESS] 서버 응답 파싱 성공.");
        // pResult에 ID, Time, DefectType, DefectDetail이 모두 채워져 있음
    }
    // wParam != 0 and != 1: 파싱 실패 (wParam = pStr)
    else
    {
        AddLog(L"[ERROR] 서버 응답 파싱 실패.");
        // pResult에 ID, Time만 있음. pStr(wParam)로 나머지 채우기
        pResult->defectType = _T("파싱오류");
        pResult->defectDetail = Utf8ToCStr((char*)wParam);
        delete[](char*)wParam; // pStr 메모리 해제
    }

    // [수정] 모든 분기(성공, 캡처실패, 파싱실패)에서 UI 갱신 및 이력 추가
    UpdateCurrentResult(*pResult);
    AddToHistory(*pResult);

    // pResult 메모리 해제
    delete pResult;

CaptureCompleteEnd:
    // [FIX] 타이머를 메인 스레드(여기)에서 안전하게 재시작
    if (GetSafeHwnd() && m_timerId == 0) // 타이머가 꺼져있을 때만
    {
        m_timerId = SetTimer(1, 100, nullptr);
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

// ========================================================================
// [FIX] ProcessCapture 수정 (ThreadEnd:에서 SetTimer 제거 및 PostMessage ID 수정)
// ========================================================================
void CCanClientDlg::ProcessCapture(bool bUseTop, bool bUseSide)
{
    if (m_timerId) { KillTimer(m_timerId); m_timerId = 0; }

    // [수정] 결과 객체와 ID/시간을 스레드 시작 시점에 생성
    InspectionResult* pResult = new InspectionResult();
    pResult->productId = GenerateProductId();
    pResult->timestamp = GetCurrentTimestamp();

    try {
        if (WaitForSingleObject(m_evtShutdown, 0) == WAIT_OBJECT_0) { AddLog(L"[THREAD] 캡처 스레드 종료 (시작 시)."); goto ThreadEnd; }

        CGrabResultPtr grabTop, grabSide;
        cv::Mat frameTop, frameSide;
        std::vector<unsigned char> bufTop, bufSide;
        std::string topResponse, sideResponse;
        bool bTopSentOK = false;

        // --- 1. Capture TOP ---
        if (bUseTop && m_camTop.IsGrabbing()) {
            if (m_camTop.RetrieveResult(800, grabTop, TimeoutHandling_ThrowException) && grabTop->GrabSucceeded()) {
                CPylonImage imgTop;
                m_converter.Convert(imgTop, grabTop);
                frameTop = cv::Mat((int)grabTop->GetHeight(), (int)grabTop->GetWidth(), CV_8UC3, (void*)imgTop.GetBuffer()).clone();
                SaveImageLocally(frameTop, _T("TOP"), pResult->productId);

                std::vector<int> params = { cv::IMWRITE_JPEG_QUALITY, 90 };
                cv::imencode(".jpg", frameTop, bufTop, params);

                // [수정] 서버 전송 실패 시 pResult에 "전송실패" 기록
                if (!SendImageToServer(bufTop, _T("TOP"), topResponse)) {
                    goto CaptureFail_Network;
                }
                bTopSentOK = true;
            }
            else {
                AddLog(L"[WARNING] TOP 카메라 캡처 실패 (RetrieveResult)");
                goto CaptureFail_Grab; // TOP 캡처 실패
            }
        }
        else {
            AddLog(L"[WARNING] TOP 카메라가 사용 설정되지 않았거나 Grabbing 상태가 아님.");
            goto CaptureFail_Grab; // TOP 캡처 실패
        }

        if (WaitForSingleObject(m_evtShutdown, 0) == WAIT_OBJECT_0) { AddLog(L"[THREAD] 캡처 스레드 종료 (TOP 캡처 후)."); goto ThreadEnd; }

        // --- 2. Capture SIDE ---
        if (bUseSide && m_camSide.IsGrabbing()) {
            if (m_camSide.RetrieveResult(800, grabSide, TimeoutHandling_ThrowException) && grabSide->GrabSucceeded()) {
                CPylonImage imgSide;
                m_converter.Convert(imgSide, grabSide);
                frameSide = cv::Mat((int)grabSide->GetHeight(), (int)grabSide->GetWidth(), CV_8UC3, (void*)imgSide.GetBuffer()).clone();
                SaveImageLocally(frameSide, _T("SIDE"), pResult->productId);

                std::vector<int> params = { cv::IMWRITE_JPEG_QUALITY, 90 };
                cv::imencode(".jpg", frameSide, bufSide, params);
                SendImageToServer(bufSide, _T("SIDE"), sideResponse); // sideResponse는 무시, 실패해도 됨
            }
            else {
                AddLog(L"[WARNING] SIDE 카메라 캡처 실패 (RetrieveResult) - 무시하고 계속");
            }
        }

        // --- 3. Process Result (Based on TOP Response) ---
        if (ParseJsonResponse(topResponse, *pResult)) {
            // 파싱 성공 (e.g., {"result":"정상"} or {"result":"에러",...})
            // [FIX] 올바른 메시지 ID (WM_APP_CAPTURE_COMPLETE) 사용
            if (GetSafeHwnd()) PostMessage(WM_APP_CAPTURE_COMPLETE, 1, (LPARAM)pResult);
        }
        else {
            // 파싱 실패 (e.g., "서버 응답 없음" or "<html>...</html>")
            char* pStr = new char[topResponse.length() + 1];
            strcpy_s(pStr, topResponse.length() + 1, topResponse.c_str());
            // [FIX] 올바른 메시지 ID (WM_APP_CAPTURE_COMPLETE) 사용
            if (GetSafeHwnd()) PostMessage(WM_APP_CAPTURE_COMPLETE, (WPARAM)pStr, (LPARAM)pResult);
        }
        goto ThreadEnd; // 정상 종료
    }
    catch (const GenericException& e) { CString msg(e.GetDescription()); AddLog(L"[ERROR] 스레드 카메라 에러: " + msg); goto CaptureFail_Grab; }
    catch (const cv::Exception& cvEx) { CString msg; msg.Format(L"[ERROR] 스레드 OpenCV 에러: %hs", cvEx.what()); AddLog(msg); goto CaptureFail_Grab; }
    catch (...) { AddLog(L"[ERROR] 스레드 알 수 없는 에러."); goto CaptureFail_Grab; }

CaptureFail_Network:
    {
        // [NEW] 서버 전송/연결 실패
        pResult->defectType = _T("전송실패");
        pResult->defectDetail = _T("서버 연결/전송 실패");
        // [FIX] 올바른 메시지 ID (WM_APP_CAPTURE_COMPLETE) 사용
        if (GetSafeHwnd()) PostMessage(WM_APP_CAPTURE_COMPLETE, 0, (LPARAM)pResult);
        goto ThreadEnd;
    }
CaptureFail_Grab:
    {
        // [수정] 카메라 Grab 실패
        pResult->defectType = _T("CAPTURE_FAIL");
        pResult->defectDetail = _T("카메라 Grab 실패");
        // [FIX] 올바른 메시지 ID (WM_APP_CAPTURE_COMPLETE) 사용
        if (GetSafeHwnd()) PostMessage(WM_APP_CAPTURE_COMPLETE, 0, (LPARAM)pResult);
    }

ThreadEnd:
    // [FIX] 작업 스레드에서 SetTimer 호출 제거 (UI 먹통 원인)
    // if (GetSafeHwnd() && ::IsWindow(GetSafeHwnd())) {
    //      m_timerId = SetTimer(1, 100, nullptr);
    // }
    return; // 스레드 종료
}


// --- Network Functions ---
// ========================================================================
// [MODIFIED] SendImageToServer (파싱 오류 수정)
// ========================================================================
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

    CString logMsg;
    logMsg.Format(L"[INFO] 이미지 전송 완료 (%d bytes)", fileSize);
    AddLog(logMsg);

    char recvBuf[4096] = { 0 }; int recvLen = recv(sock, recvBuf, sizeof(recvBuf) - 1, 0);

    // [MODIFIED] recv()가 0바이트(연결 종료) 또는 오류(-1)를 반환하면
    // false를 반환하여 "파싱 오류" 대신 "전송 실패"로 처리되도록 수정
    if (recvLen > 0) {
        recvBuf[recvLen] = '\0';
        response = std::string(recvBuf);
        AddLog(L"[INFO] 서버 응답 수신: " + Utf8ToCStr(response));
        closesocket(sock);
        return true;
    }
    else {
        response.clear();
        AddLog(L"[WARNING] 서버 응답 없음 (recvLen <= 0)");
        closesocket(sock);
        return false; // <-- 핵심 수정
    }
}

// ========================================================================
// [MODIFIED] 로컬 이미지 저장 헬퍼 (폴더 분리)
// ========================================================================
void CCanClientDlg::SaveImageLocally(const cv::Mat& frame, CString role, CString productId)
{
    if (frame.empty() || productId.IsEmpty()) return;

    try
    {
        CString folderPath;
        // [MODIFIED] role(TOP/SIDE)에 따라 하위 폴더 경로 생성
        folderPath.Format(_T("C:\\InspectionImages\\%s\\"), (LPCTSTR)role);

        // CString/Windows API를 사용하여 폴더 생성 (재귀적으로 생성)
        // (예: C:\InspectionImages\TOP\ 폴더가 없으면 자동으로 만듦)
        SHCreateDirectoryEx(NULL, folderPath, NULL);

        CString fileName;
        fileName.Format(_T("%s_%s.jpg"), (LPCTSTR)productId, (LPCTSTR)role);
        // [MODIFIED] (예: C:\InspectionImages\TOP\CK1012_TOP.jpg)
        CString filePath = folderPath + fileName;

        // cv::imwrite는 CString을 직접 지원하지 않으므로 std::string으로 변환
        // 유니코드(CString) -> 멀티바이트(std::string)
        CT2A ansiPath(filePath);
        std::string stdPath(ansiPath);

        std::vector<int> params = { cv::IMWRITE_JPEG_QUALITY, 95 };
        if (cv::imwrite(stdPath, frame, params)) {
            AddLog(L"[INFO] 로컬 이미지 저장 성공: " + filePath);
        }
        else {
            AddLog(L"[WARNING] 로컬 이미지 저장 실패: " + filePath);
        }
    }
    catch (const cv::Exception& cvEx) {
        CString msg; msg.Format(L"[ERROR] SaveImageLocally OpenCV 에러: %hs", cvEx.what()); AddLog(msg);
    }
    catch (...)
    {
        AddLog(L"[ERROR] SaveImageLocally 알 수 없는 에러.");
    }
}


// ========================================================================
// [MODIFIED] 로컬 이미지 미리보기 (폴더 분리)
// ========================================================================
void CCanClientDlg::OnDblclkListHistory(NMHDR* pNMHDR, LRESULT* pResult)
{
    LPNMITEMACTIVATE pNMItemActivate = reinterpret_cast<LPNMITEMACTIVATE>(pNMHDR);
    *pResult = 0;
    if (pNMItemActivate->iItem != -1 && pNMItemActivate->iItem < m_historyList.GetItemCount()) {
        CString productID = m_historyList.GetItemText(pNMItemActivate->iItem, 0);
        AddLog(L"[UI] 이력 더블클릭: " + productID);

        // [MODIFIED] SaveImageLocally에서 사용하는 분리된 폴더 경로와 일치시킴
        CString baseFolderPath = _T("C:\\InspectionImages\\");
        CString pathTop, pathSide;
        pathTop.Format(_T("%sTOP\\%s_TOP.jpg"), (LPCTSTR)baseFolderPath, (LPCTSTR)productID);
        pathSide.Format(_T("%sSIDE\\%s_SIDE.jpg"), (LPCTSTR)baseFolderPath, (LPCTSTR)productID);

        CFileStatus status;
        bool bGotTop = CFile::GetStatus(pathTop, status);
        bool bGotSide = CFile::GetStatus(pathSide, status);

        if (bGotTop || bGotSide) {
            // CPreviewDlg (IDD_PREVIEW_DLG)를 생성합니다.
            CPreviewDlg dlg(this);

            // [INFO] SetImagePaths가 CPreviewDlg 내부에서
            // bGotTop ? pathTop : "" 이미지를 -> IDC_IMG_LEFT 에
            // bGotSide ? pathSide : "" 이미지를 -> IDC_IMG_RIGHT 에
            // 로드하도록 약속되어 있습니다. (이 함수는 PreviewDlg.cpp에 구현되어 있음)
            dlg.SetImagePaths(bGotTop ? pathTop : _T(""), bGotSide ? pathSide : _T(""));

            // 다이얼로그를 모달(Modal)로 띄웁니다.
            dlg.DoModal();
        }
        else {
            AddLog(L"[ERROR] 로컬에서 이미지를 찾을 수 없습니다: " + pathTop + L" or " + pathSide);
            // [MODIFIED] 오류 메시지 경로 수정
            AfxMessageBox(L"로컬(C:\\InspectionImages\\TOP 또는 SIDE)에서 해당 이미지를 찾을 수 없습니다.");
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
// ========================================================================
// [FIX] UpdateCurrentResult 수정 (모션 캡처 중에는 팝업 대신 FlashWindow)
// ========================================================================
void CCanClientDlg::UpdateCurrentResult(const InspectionResult& result)
{
    SetDlgItemText(IDC_STATIC_PRODUCT_ID, result.productId);
    SetDlgItemText(IDC_STATIC_DEFECT_TYPE, result.defectType);
    SetDlgItemText(IDC_STATIC_DEFECT_DETAIL, (result.defectDetail.IsEmpty() || result.defectType == _T("정상")) ? _T("-") : result.defectDetail);

    // [수정] "정상"이 아닌 모든 경우를 불량/오류로 간주
    bool bIsDefect = (result.defectType != _T("정상") && !result.defectType.IsEmpty());

    // [NEW] 컨트롤 배경색 갱신 요청
    if (GetDlgItem(IDC_STATIC_DEFECT_TYPE))
    {
        GetDlgItem(IDC_STATIC_DEFECT_TYPE)->Invalidate();
    }

    // [NEW] 불량/오류 알림
    if (bIsDefect)
    {
        UpdateData(TRUE); // m_bMotionDetect 값 갱신
        if (m_bMotionDetect)
        {
            // 모션 캡처 중에는 무한 루프 방지를 위해 팝업 대신 작업 표시줄 깜박임
            FlashWindow(TRUE);
        }
        else
        {
            // 수동 캡처 시에는 팝업
            CString msg;
            msg.Format(L"!! 검사 오류/불량 감지 !!\n\n제품 ID: %s\n유형: %s\n상세: %s",
                (LPCTSTR)result.productId,
                (LPCTSTR)result.defectType,
                (LPCTSTR)(result.defectDetail.IsEmpty() ? _T("-") : result.defectDetail));
            AfxMessageBox(msg, MB_ICONWARNING | MB_OK);
        }
    }
}


void CCanClientDlg::ClearCurrentResult()
{
    SetDlgItemText(IDC_STATIC_PRODUCT_ID, _T("-"));
    SetDlgItemText(IDC_STATIC_DEFECT_TYPE, _T("-"));
    SetDlgItemText(IDC_STATIC_DEFECT_DETAIL, _T("-"));

    // [NEW] Clear 시에도 배경색 갱신
    if (GetDlgItem(IDC_STATIC_DEFECT_TYPE))
    {
        GetDlgItem(IDC_STATIC_DEFECT_TYPE)->Invalidate();
    }
}


void CCanClientDlg::UpdateStatistics()
{
    int total = 0, normal = 0, defect = 0;
    CString today = CTime::GetCurrentTime().Format(_T("%Y-%m-%d"));
    for (const auto& rec : m_history) {
        if (rec.timestamp.Left(10) == today) {
            total++;
            if (rec.defectType == _T("정상")) normal++;
            // [FIX] "전송실패"도 불량 통계에서 제외
            else if (rec.defectType != _T("에러") && rec.defectType != _T("파싱오류") && rec.defectType != _T("CAPTURE_FAIL") && rec.defectType != _T("전송실패")) defect++;
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

// [NEW] 히스토리 파일 경로를 동적으로 (EXE 파일 기준) 가져오는 헬퍼 함수
CString CCanClientDlg::GetHistoryFilePath()
{
    TCHAR szPath[MAX_PATH];
    // CanClient.exe 파일의 전체 경로를 가져옴
    GetModuleFileName(NULL, szPath, MAX_PATH);
    // ".exe" 파일명 부분을 제거하고 폴더 경로만 남김
    PathRemoveFileSpec(szPath);

    CString sPath(szPath);
    sPath += _T("\\history.txt"); // "실행폴더\history.txt"
    return sPath;
}

// --- Settings & History File I/O ---
void CCanClientDlg::AddToHistory(const InspectionResult& result)
{
    if (m_historyList.GetSafeHwnd()) {
        m_history.push_back(result);
        int idx = m_historyList.InsertItem(m_historyList.GetItemCount(), result.productId);
        m_historyList.SetItemText(idx, 1, result.defectType);
        m_historyList.SetItemText(idx, 2, result.defectDetail.IsEmpty() ? _T("-") : result.defectDetail);
        m_historyList.SetItemText(idx, 3, result.timestamp);

        SaveHistoryToFile();

        m_historyList.EnsureVisible(idx, FALSE);
        UpdateStatistics();
    }
    else { AddLog(L"[ERROR] AddToHistory: List control 핸들 오류."); }
}

void CCanClientDlg::SaveHistoryToFile()
{
    // [FIX] 하드코딩된 경로 대신 동적 경로 헬퍼 함수 사용
    // CString folder = _T("C:\\CanClient"); CreateDirectory(folder, NULL);
    // CString filePath = folder + _T("\\history.txt");
    CString filePath = GetHistoryFilePath();

    CStdioFile file;
    // [수정] CSV 호환을 위해 덮어쓰기 (UTF-16 LE BOM)
    if (!file.Open(filePath, CFile::modeCreate | CFile::modeWrite | CFile::typeText)) {
        AddLog(L"[ERROR] 히스토리 파일 저장 실패: " + filePath);
        return;
    }
    file.Write(L"\xFEFF", 1); // UTF-16 LE BOM

    for (const auto& rec : m_history) {
        CString line;
        // [NEW] CSV 호환을 위해 | 대신 , 사용 및 상세 내용의 " 처리
        CString detail = rec.defectDetail;
        detail.Replace(_T("\""), _T("\"\"")); // Escape quotes
        detail.Format(_T("\"%s\""), (LPCTSTR)detail); // Wrap in quotes

        line.Format(_T("%s,%s,%s,%s\n"),
            (LPCTSTR)rec.productId,
            (LPCTSTR)rec.defectType,
            (LPCTSTR)detail,
            (LPCTSTR)rec.timestamp);
        file.WriteString(line);
    }
    file.Close();
}

// ========================================================================
// [MODIFIED] LoadHistoryFromFile (BOM 버그 수정)
// ========================================================================
void CCanClientDlg::LoadHistoryFromFile()
{
    if (!m_historyList.GetSafeHwnd()) { AddLog(L"[ERROR] LoadHistoryFromFile: List control 핸들 오류."); return; }
    m_historyList.DeleteAllItems();
    m_history.clear();

    // [FIX] 하드코딩된 경로 대신 동적 경로 헬퍼 함수 사용
    CString filePath = GetHistoryFilePath();

    CStdioFile file;
    // [수정] CStdioFile은 BOM을 자동 처리 (읽기 모드)
    if (!file.Open(filePath, CFile::modeRead | CFile::typeText | CFile::shareDenyWrite)) {
        // [FIX] 로그 메시지에 올바른(동적) 경로가 표시됨
        AddLog(L"[WARNING] 히스토리 파일 읽기 실패: " + filePath);
        return;
    }

    CString line;
    long maxId = 1011;

    // [MODIFIED] BOM(ÿ) 버그를 수정하는 새 로직
    bool bIsFirstLine = true;
    while (file.ReadString(line))
    {
        if (bIsFirstLine)
        {
            bIsFirstLine = false;
            // CStdioFile이 BOM(0xFEFF)을 자동으로 처리하지 않고
            // 텍스트(ÿ)로 읽어오는 경우를 수동으로 처리합니다.
            if (line.GetLength() > 0 && line[0] == 0xFEFF)
            {
                line = line.Mid(1); // 첫 번째 BOM 문자 제거
            }
        }

        // BOM이 제거된 깨끗한 라인을 파서로 전달
        ProcessHistoryLine(line, maxId);
    }

    m_productCounter = maxId;
    file.Close();
    UpdateStatistics();
    if (m_historyList.GetItemCount() > 0) m_historyList.EnsureVisible(m_historyList.GetItemCount() - 1, FALSE);
    AddLog(L"[INFO] 히스토리 로드 완료: " + filePath); // [FIX] 로그에 올바른 경로 표시
}

// ========================================================================
// [FIX] ProcessHistoryLine 정의 (컴파일 오류 수정)
// ========================================================================
void CCanClientDlg::ProcessHistoryLine(CString line, long& maxId)
{
    line.Trim(); if (line.IsEmpty()) return;
    int cur = 0, count = 0;
    CString parts[4];

    // 간단한 CSV 파서 (따옴표 처리)
    for (int i = 0; i < 4; ++i)
    {
        if (cur >= line.GetLength()) break;

        CString token;
        if (line[cur] == _T('\"')) // 따옴표로 시작
        {
            cur++; // " skip
            int nextQuote = line.Find(_T('\"'), cur);
            while (nextQuote != -1 && nextQuote + 1 < line.GetLength() && line[nextQuote + 1] == _T('\"')) // "" (이중 따옴표)
            {
                nextQuote = line.Find(_T('\"'), nextQuote + 2);
            }

            if (nextQuote != -1)
            {
                token = line.Mid(cur, nextQuote - cur);
                token.Replace(_T("\"\""), _T("\"")); // "" -> "
                cur = nextQuote + 1; // " skip
                if (cur < line.GetLength() && line[cur] == _T(',')) cur++; // , skip
            }
            else
            {
                // 따옴표가 닫히지 않은 비정상 라인
                token = line.Mid(cur);
                cur = line.GetLength();
            }
        }
        else // 따옴표 없음
        {
            int nextComma = line.Find(_T(','), cur);
            if (nextComma == -1)
            {
                token = line.Mid(cur);
                cur = line.GetLength();
            }
            else
            {
                token = line.Mid(cur, nextComma - cur);
                cur = nextComma + 1;
            }
        }
        parts[count++] = token;
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


// --- Settings Dialog ---
// [수정] OnBnClickedBtnSettings 함수 전체 덮어쓰기 (Pylon 충돌 해결)
void CCanClientDlg::OnBnClickedBtnSettings()
{
    AddLog(L"[INFO] 설정 창 열기... (타이머 중지)");

    // [수정] 설정 창을 열기 전에 타이머를 멈춥니다.
    if (m_timerId) {
        KillTimer(m_timerId);
        m_timerId = 0;
    }

    ScanPylonDevices();

    m_settingsDlg.m_availableDevices = m_availableDevices;
    m_settingsDlg.m_currentTopSerial = m_topCamSerial;
    m_settingsDlg.m_currentSideSerial = m_sideCamSerial;
    m_settingsDlg.m_strServerIP = m_strServerIP;
    m_settingsDlg.m_nUploadPort = m_nUploadPort;
    m_settingsDlg.m_nRequestPort = m_nRequestPort;
    m_settingsDlg.m_pCamTop = m_camTop.IsOpen() ? &m_camTop : nullptr;
    m_settingsDlg.m_pCamSide = m_camSide.IsOpen() ? &m_camSide : nullptr;

    if (m_settingsDlg.DoModal() == IDOK)
    {
        m_topCamSerial = m_settingsDlg.m_selectedTopSerial;
        m_sideCamSerial = m_settingsDlg.m_selectedSideSerial;
        m_strServerIP = m_settingsDlg.m_strServerIP;
        m_nUploadPort = m_settingsDlg.m_nUploadPort;
        m_nRequestPort = m_settingsDlg.m_nRequestPort;

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
        bool bReopened = OpenAssignedCameras();

        if (bReopened) {
            // =================================================================
            // [수정] 요청에 따라 고급 설정 적용 코드 비활성화
            // =================================================================
            /*
            if (sAppliedRole == _T("TOP") && m_camTop.IsOpen()) {
                ApplyAdvancedCameraSettings(m_camTop, dFps, dExposure, dGain);
                m_dTopFps = dFps; m_dTopExposure = dExposure; m_dTopGain = dGain;
            }
            else if (sAppliedRole == _T("SIDE") && m_camSide.IsOpen()) {
                ApplyAdvancedCameraSettings(m_camSide, dFps, dExposure, dGain);
                m_dSideFps = dFps; m_dSideExposure = dExposure; m_dSideGain = dGain;
            }
            */
            AddLog(L"[INFO] 고급 카메라 설정 적용 비활성화됨 (사용자 요청).");
            // =================================================================
        }
        else {
            AfxMessageBox(L"카메라 재연결 실패. 고급 설정이 적용되지 않았을 수 있습니다.");
        }
        m_prevFrameTop.release();
        m_prevFrameSide.release();
    }
    else {
        AddLog(L"[INFO] 설정 변경 취소됨.");
    }

    // [수정] 설정 창이 닫힌 후 (OK든 Cancel이든) 100ms 간격으로 타이머를 다시 시작합니다.
    if (m_timerId == 0) { // 타이머가 꺼져있을 때만
        m_timerId = SetTimer(1, 100, nullptr);
        AddLog(L"[INFO] 설정 창 닫힘. (타이머 재시작)");
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
        if (bWasGrabbing) cam.StopGrabbing();

        CString camSerial(cam.GetDeviceInfo().GetSerialNumber().c_str());
        AddLog(L"[INFO] 고급 설정 적용 시작 (" + camSerial + L")...");

        bool bSuccess = true;
        try {
            CBooleanParameter(cam.GetNodeMap(), "AcquisitionFrameRateEnable").SetValue(true);
            if (!SetPylonFloatValue(cam, "AcquisitionFrameRate", fps)) bSuccess = false;
        }
        catch (const GenericException&) { AddLog(L"[WARNING] AcquisitionFrameRateEnable 설정 불가. FPS 적용 건너뜀."); }

        if (!SetPylonFloatValue(cam, "ExposureTime", exposure)) bSuccess = false;
        if (!SetPylonFloatValue(cam, "Gain", gain)) bSuccess = false;

        if (bWasGrabbing) cam.StartGrabbing(GrabStrategy_LatestImageOnly);
        AddLog(L"[INFO] 고급 설정 적용 완료 (" + camSerial + L"). 성공 여부: " + (bSuccess ? L"성공" : L"일부 실패"));
        return bSuccess;

    }
    catch (const GenericException& e) {
        AddLog(CString(L"[ERROR] ApplyAdvancedCameraSettings 실패: ") + CString(e.GetDescription()));
        if (bWasGrabbing && !cam.IsGrabbing()) {
            try { cam.StartGrabbing(GrabStrategy_LatestImageOnly); }
            catch (...) {}
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
// [수정] SetPylonFloatValue 함수 전체 덮어쓰기 (검은 화면 버그 해결)
bool CCanClientDlg::SetPylonFloatValue(CInstantCamera& cam, const char* paramName, double value)
{
    // [수정] value < 0 (기본값 -1.0)이면 아예 설정을 시도하지 않습니다. (FPS 0.00 버그 수정)
    if (!cam.IsOpen() || value < 0.0) return false;
    try {
        INodeMap& nodemap = cam.GetNodeMap();
        CFloatParameter param(nodemap, paramName);
        if (param.IsValid() && IsWritable(param)) {
            double minVal = param.GetMin();
            double maxVal = param.GetMax();
            double clampedValue = max(minVal, min(maxVal, value)); // Clamp value to valid range
            param.SetValue(clampedValue);
            CString msg; msg.Format(L"    - %hs 설정: %.2f (범위: %.2f-%.2f)", paramName, clampedValue, minVal, maxVal); AddLog(msg);
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
// [수정] OnDestroy 함수 전체 덮어쓰기 (네트워크 종료 코드 추가)
void CCanClientDlg::OnDestroy()
{
    AddLog(L"[INFO] 프로그램 종료 시작...");

    if (m_timerId) KillTimer(m_timerId);
    if (m_evtShutdown) SetEvent(m_evtShutdown);
    if (m_pCaptureThread != nullptr) {
        WaitForSingleObject(m_pCaptureThread->m_hThread, 500); // 캡처 스레드 대기
    }
    CloseAllCameras(); // <-- 이 줄이 이미 있어야 합니다.

    ShutdownGDIPlus();

    // [수정] 네트워크(WSA) 종료 코드를 추가합니다.
    if (m_wsaInitialized)
    {
        WSACleanup();
    }

    CDialogEx::OnDestroy();
}

// [수정] OnPostInit 함수 전체 덮어쓰기 (타이머 간격 수정)
LRESULT CCanClientDlg::OnPostInit(WPARAM wParam, LPARAM lParam)
{
    AddLog(L"[INFO] Post-Init 완료. 카메라 스레드를 시작합니다.");

    try {
        //PylonInitialize(); // CanClient.cpp에서 하므로 주석 처리 유지
        ScanPylonDevices();
        if (!OpenAssignedCameras()) { AddLog(L"[WARNING] 초기 카메라 열기 실패."); }
        else {
            // =================================================================
            // [수정] 요청에 따라 고급 설정 적용 코드 비활성화
            // =================================================================
            // if (m_camTop.IsOpen()) ApplyAdvancedCameraSettings(m_camTop, m_dTopFps, m_dTopExposure, m_dTopGain);
            // if (m_camSide.IsOpen()) ApplyAdvancedCameraSettings(m_camSide, m_dSideFps, m_dSideExposure, m_dSideGain);
            AddLog(L"[INFO] 초기 고급 카메라 설정 적용 비활성화됨 (사용자 요청).");
            // =================================================================
        }
        m_converter.OutputPixelFormat = PixelType_BGR8packed;
        m_converter.OutputBitAlignment = OutputBitAlignment_MsbAligned;

        // [수정] 타이머 간격을 33ms (초당 30회) -> 100ms (초당 10회)로 변경 (UI 먹통 현상 완화)
        m_timerId = SetTimer(1, 100, nullptr);

        if (m_timerId == 0)
        {
            AddLog(L"[ERROR] SetTimer(1, ...) 실패! 타이머가 시작되지 않았습니다.");
            AfxMessageBox(L"치명적 오류: SetTimer가 실패했습니다. 프로그램을 다시 시작해주세요.");
        }
        else
        {
            CString msg;
            // [수정] C6328 경고 해결 (%u -> %I64u) (UINT_PTR는 64비트)
            msg.Format(L"[INFO] SetTimer(1, ...) 성공. Timer ID = %I64u", m_timerId);
            AddLog(msg);
        }
    }
    catch (const GenericException& e) { CString msg(e.GetDescription()); AfxMessageBox(msg); AddLog(CString(L"[ERROR] Pylon 초기화 실패: ") + msg); }
    catch (...) { AddLog(L"[ERROR] Pylon 초기화 중 알 수 없는 오류."); AfxMessageBox(L"Pylon 초기화 중 알 수 없는 오류 발생."); }

    return 0;
}


// ========================================================================
// [NEW] 신규 기능: 불량 결과 텍스트 배경색 변경
// ========================================================================
HBRUSH CCanClientDlg::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
    HBRUSH hbr = CDialogEx::OnCtlColor(pDC, pWnd, nCtlColor);

    // [현재 검사 결과]의 '결함 유형' 컨트롤 ID
    if (pWnd->GetDlgCtrlID() == IDC_STATIC_DEFECT_TYPE)
    {
        CString sText;
        pWnd->GetWindowText(sText);

        // "정상" 또는 "-" 가 아닐 경우 (즉, 불량일 경우)
        if (sText != _T("정상") && sText != _T("-") && !sText.IsEmpty())
        {
            pDC->SetBkColor(RGB(255, 220, 220)); // 연한 빨강 배경
            pDC->SetTextColor(RGB(200, 0, 0));   // 진한 빨강 텍스트
            return (HBRUSH)m_brushRed.GetSafeHandle();
        }
    }

    // 그 외에는 기본값 반환
    return hbr;
}

// ========================================================================
// [NEW] 신규 기능: 히스토리 내보내기 (CSV)
// ========================================================================
void CCanClientDlg::OnBnClickedBtnExportHistory()
{
    // 1. 파일 저장 대화상자 띄우기
    CString strFilter = _T("CSV 파일 (*.csv)|*.csv|모든 파일 (*.*)|*.*||");
    CFileDialog dlg(FALSE, // FALSE = 저장
        _T("csv"),       // 기본 확장자
        _T("history_export.csv"), // 기본 파일명
        OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT, // 속성
        strFilter,       // 필터
        this);           // 부모 윈도우

    if (dlg.DoModal() != IDOK)
    {
        AddLog(L"[INFO] 히스토리 내보내기 취소됨.");
        return;
    }

    CString filePath = dlg.GetPathName();
    CStdioFile file;

    // [수정] 유니코드(UTF-16 LE)로 저장 (BOM 포함)
    if (!file.Open(filePath, CFile::modeCreate | CFile::modeWrite | CFile::typeText))
    {
        AddLog(L"[ERROR] 히스토리 내보내기 파일 열기 실패: " + filePath);
        AfxMessageBox(L"파일을 저장할 수 없습니다.");
        return;
    }

    // [수정] UTF-16 BOM (Byte Order Mark) 추가 (Excel 호환성)
    file.Write(L"\xFEFF", 1);

    try
    {
        // 2. 헤더 쓰기
        file.WriteString(L"제품 ID,결함 유형,상세 내용,시간\n");

        // 3. 데이터 쓰기 (m_history 벡터 사용)
        for (const auto& rec : m_history)
        {
            // CSV 형식에 맞게 ,(콤마)가 포함될 수 있는 상세 내용은 ""로 감싸기
            CString detail = rec.defectDetail;
            detail.Replace(_T("\""), _T("\"\"")); // " -> ""
            detail.Format(_T("\"%s\""), (LPCTSTR)detail);

            CString line;
            line.Format(_T("%s,%s,%s,%s\n"),
                (LPCTSTR)rec.productId,
                (LPCTSTR)rec.defectType,
                (LPCTSTR)detail,
                (LPCTSTR)rec.timestamp);

            file.WriteString(line);
        }

        file.Close();
        AddLog(L"[SUCCESS] 히스토리 내보내기 완료: " + filePath);
        AfxMessageBox(L"히스토리 내보내기 완료: " + filePath);
    }
    catch (CFileException* e)
    {
        e->Delete();
        AddLog(L"[ERROR] 히스토리 내보내기 중 파일 쓰기 오류");
    }
}