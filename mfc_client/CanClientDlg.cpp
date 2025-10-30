#include "pch.h"
#include "framework.h"
#include "CanClient.h"
#include "CanClientDlg.h"
#include "afxdialogex.h"

#include <fstream>
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")


// ===== JSON 라이브러리 =====
#include <nlohmann/json.hpp>
using json = nlohmann::json;


// 3, 5. OpenCV Lib (모션 감지 및 인코딩)
#ifdef _DEBUG
  #pragma comment(lib, "opencv_world4120d.lib")
#else
  #pragma comment(lib, "opencv_world4120.lib")
#endif
// 5. 스레드 완료 메시지
#define WM_CAPTURE_COMPLETE (WM_USER + 100)


// UTF-8 std::string -> UTF-16 CString (기존 함수)
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

using namespace Pylon;
using namespace cv; // 3. OpenCV 네임스페이스

// ===================== 메시지 맵 =====================
BEGIN_MESSAGE_MAP(CCanClientDlg, CDialogEx)
    ON_BN_CLICKED(IDC_BTN_START, &CCanClientDlg::OnBnClickedBtnStart)
    ON_BN_CLICKED(IDC_BTN_SETTINGS, &CCanClientDlg::OnBnClickedBtnSettings) // 1, 2.
    ON_WM_DESTROY()
    ON_WM_TIMER()
    ON_MESSAGE(WM_CAPTURE_COMPLETE, &CCanClientDlg::OnCaptureComplete) // 5.
END_MESSAGE_MAP()

// ===================== 생성자 =====================
CCanClientDlg::CCanClientDlg(CWnd* pParent)
    : CDialogEx(IDD_CANCLIENT_DIALOG, pParent)
    , m_bMotionDetect(FALSE) // 3.
    , m_bCaptureInProgress(false) // 3.
{
    m_hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);
}

// ===================== MFC 연결 =====================
void CCanClientDlg::DoDataExchange(CDataExchange* pDX)
{
    CDialogEx::DoDataExchange(pDX);
    DDX_Control(pDX, IDC_LIST_HISTORY, m_historyList); // m_historyList 연결
    DDX_Control(pDX, IDC_CHECK_MOTION, m_checkMotionDetect); // 3.
    DDX_Check(pDX, IDC_CHECK_MOTION, m_bMotionDetect); // 3.
}

// ===================== 초기화 =====================
BOOL CCanClientDlg::OnInitDialog()
{
    CDialogEx::OnInitDialog();
    SetIcon(m_hIcon, TRUE);
    SetIcon(m_hIcon, FALSE);

    // ... 기존 히스토리 파일 초기화 ... (기존 코드)
    {
        CString folder = _T("C:\\CanClient");
        CreateDirectory(folder, NULL);
        CString filePath = folder + _T("\\history.txt");
        CFile file;
        if (file.Open(filePath, CFile::modeCreate | CFile::modeWrite)) {
            file.Close();
        }
        m_history.clear();
        if (m_historyList.GetSafeHwnd())
            m_historyList.DeleteAllItems();
    }

    // ===== WSA 초기화 ===== (기존 코드)
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) == 0) {
        m_wsaInitialized = true;
        OutputDebugString(L"[INFO] WSA 초기화 완료\n");
    }

    // ===== 히스토리 리스트 초기화 =====
    InitHistoryList(); // (기존 코드, m_historyList Subclassing 포함)

    // ===== 초기 UI 상태 =====
    ClearCurrentResult(); // (기존 코드)

    try {
        PylonInitialize();

        // 1, 2. Pylon 장치 스캔
        ScanPylonDevices();

        // 1, 2. 기본값 할당 (첫 번째, 두 번째 카메라)
        if (m_availableDevices.size() > 0)
        {
            m_topCamSerial = CString(m_availableDevices[0].GetSerialNumber().c_str());
        }
        if (m_availableDevices.size() > 1)
        {
            m_sideCamSerial = CString(m_availableDevices[1].GetSerialNumber().c_str());
        }

        // 1, 2. 할당된 카메라 열기
        if (!OpenAssignedCameras())
        {
            AfxMessageBox(L"카메라 열기에 실패했습니다. '설정'에서 카메라를 확인하세요.");
        }

        // 변환기 기본 설정 (기존 코드)
        m_converter.OutputPixelFormat = PixelType_BGR8packed;
        m_converter.OutputBitAlignment = OutputBitAlignment_MsbAligned;

        // 미리보기 타이머 (기존 코드)
        m_timerId = SetTimer(1, 33, nullptr); // ~30fps
    }
    catch (const GenericException& e) {
        CString msg(e.GetDescription());
        AfxMessageBox(msg);
    }

    return TRUE;
}

// ===================== 1, 2. Pylon 장치 스캔 =====================
void CCanClientDlg::ScanPylonDevices()
{
    try
    {
        CTlFactory& factory = CTlFactory::GetInstance();
        factory.EnumerateDevices(m_availableDevices);
        CString msg;
        msg.Format(L"[INFO] Pylon 카메라 %d대 발견", m_availableDevices.size());
        AddLog(msg);
    }
    catch (const GenericException& e)
    {
        AddLog(CString(L"[ERROR] Pylon 스캔 실패: ") + CString(e.GetDescription()));
    }
}

// ===================== 1, 2. 할당된 카메라 열기 =====================
bool CCanClientDlg::OpenAssignedCameras()
{
    CloseAllCameras(); // 일단 모두 닫기
    CTlFactory& factory = CTlFactory::GetInstance();
    bool bSuccessTop = true;
    bool bSuccessSide = true;

    try
    {
        // TOP 카메라 열기
        if (!m_topCamSerial.IsEmpty())
        {
            m_camTop.Attach(factory.CreateDevice(Pylon::String_t(CT2A(m_topCamSerial))));
            m_camTop.Open();
            m_camTop.StartGrabbing(GrabStrategy_LatestImageOnly);
            AddLog(L"[INFO] TOP 카메라 (" + m_topCamSerial + L") 열기 성공.");
        }
        else
        {
            bSuccessTop = false;
            AddLog(L"[INFO] TOP 카메라가 할당되지 않았습니다.");
        }

        // SIDE 카메라 열기
        if (!m_sideCamSerial.IsEmpty() && m_sideCamSerial != m_topCamSerial)
        {
            m_camSide.Attach(factory.CreateDevice(Pylon::String_t(CT2A(m_sideCamSerial))));
            m_camSide.Open();
            m_camSide.StartGrabbing(GrabStrategy_LatestImageOnly);
            AddLog(L"[INFO] SIDE 카메라 (" + m_sideCamSerial + L") 열기 성공.");
        }
        else if (m_sideCamSerial == m_topCamSerial && !m_topCamSerial.IsEmpty())
        {
            bSuccessSide = false;
            AddLog(L"[WARNING] SIDE 카메라가 TOP과 동일하여 열지 않습니다.");
        }
        else
        {
            bSuccessSide = false;
            AddLog(L"[INFO] SIDE 카메라가 할당되지 않았습니다.");
        }
    }
    catch (const GenericException& e)
    {
        AddLog(CString(L"[ERROR] 카메라 열기 실패: ") + CString(e.GetDescription()));
        return false;
    }

    // 하나라도 열렸으면 성공
    return m_camTop.IsOpen() || m_camSide.IsOpen();
}

// ===================== 1, 2. 모든 카메라 닫기 =====================
void CCanClientDlg::CloseAllCameras()
{
    try {
        if (m_camTop.IsGrabbing())   m_camTop.StopGrabbing();
        if (m_camTop.IsOpen())       m_camTop.Close();
        //if (m_camTop.IsAttached())   m_camTop.DetachDevice();

        if (m_camSide.IsGrabbing())  m_camSide.StopGrabbing();
        if (m_camSide.IsOpen())      m_camSide.Close();
        //if (m_camSide.IsAttached())  m_camSide.DetachDevice();
    }
    catch (...) {}
    AddLog(L"[INFO] 모든 카메라 닫힘.");
}


// ===================== 히스토리 리스트 초기화 (기존 코드) =====================
void CCanClientDlg::InitHistoryList()
{
    // CCanClientDlg::DoDataExchange에서 m_historyList가 연결된 후 호출되어야 함
    // m_historyList.SubclassDlgItem(IDC_LIST_HISTORY, this); // DoDataExchange가 처리

    // ===== 컬럼 추가 =====
    m_historyList.InsertColumn(0, _T("제품번호"), LVCFMT_CENTER, 150);
    m_historyList.InsertColumn(1, _T("분석결과"), LVCFMT_CENTER, 120);
    m_historyList.InsertColumn(2, _T("불량종류"), LVCFMT_CENTER, 180);
    m_historyList.InsertColumn(3, _T("시간"), LVCFMT_CENTER, 170);

    // ===== 확장 스타일 =====
    m_historyList.SetExtendedStyle(
        LVS_EX_FULLROWSELECT |  // 전체 행 선택
        LVS_EX_GRIDLINES        // 눈금선
    );

    // ===== 파일에서 히스토리 로드 =====
    LoadHistoryFromFile();
}

// ===================== 1, 2, 3. 타이머 (미리보기 + 모션 감지) =====================
void CCanClientDlg::OnTimer(UINT_PTR nIDEvent)
{
    if (nIDEvent != 1) {
        CDialogEx::OnTimer(nIDEvent);
        return;
    }

    UpdateData(TRUE); // 3. 체크박스 상태 동기화

    // 3. 캡처가 진행 중이면 타이머(미리보기/모션감지)는 아무 작업도 하지 않음
    if (m_bCaptureInProgress)
    {
        return;
    }

    try {
        CGrabResultPtr grabTop, grabSide;
        bool bMotionTop = false;
        bool bMotionSide = false;

        // 1, 2. TOP 카메라 처리 (할당된 경우)
        if (m_camTop.IsGrabbing() &&
            m_camTop.RetrieveResult(50, grabTop, TimeoutHandling_Return) &&
            grabTop->GrabSucceeded())
        {
            // 3. 모션 감지 모드
            if (m_bMotionDetect)
            {
                cv::Mat currentMat;
                ConvertPylonBufferToMat(m_pylonImage, grabTop, currentMat);
                bMotionTop = DetectMotion(currentMat, _T("TOP"));
                // 모션 감지 모드에서는 미리보기 그리지 않음 (옵션)
                // DrawImageBufferToCtrl(...); 
            }
            // 일반 미리보기 모드
            else
            {
                m_converter.Convert(m_pylonImage, grabTop);
                const uint8_t* buf = reinterpret_cast<const uint8_t*>(m_pylonImage.GetBuffer());
                int w = static_cast<int>(grabTop->GetWidth());
                int h = static_cast<int>(grabTop->GetHeight());
                DrawImageBufferToCtrl(buf, w, h, GetDlgItem(IDC_CAM_TOP));
            }
        }
        else if (!m_bMotionDetect)
        {
            // 미리보기 모드일 때만 Picture Control 클리어
            ClearPictureControl(GetDlgItem(IDC_CAM_TOP));
        }

        // 1, 2. SIDE 카메라 처리 (할당된 경우)
        if (m_camSide.IsGrabbing() &&
            m_camSide.RetrieveResult(50, grabSide, TimeoutHandling_Return) &&
            grabSide->GrabSucceeded())
        {
            // 3. 모션 감지 모드
            if (m_bMotionDetect)
            {
                cv::Mat currentMat;
                ConvertPylonBufferToMat(m_pylonImage, grabSide, currentMat);
                bMotionSide = DetectMotion(currentMat, _T("SIDE"));
            }
            // 일반 미리보기 모드
            else
            {
                m_converter.Convert(m_pylonImage, grabSide);
                const uint8_t* buf = reinterpret_cast<const uint8_t*>(m_pylonImage.GetBuffer());
                int w = static_cast<int>(grabSide->GetWidth());
                int h = static_cast<int>(grabSide->GetHeight());
                DrawImageBufferToCtrl(buf, w, h, GetDlgItem(IDC_CAM_FRONT)); // IDC_CAM_FRONT 사용
            }
        }
        else if (!m_bMotionDetect)
        {
            ClearPictureControl(GetDlgItem(IDC_CAM_FRONT));
        }

        // 3. 모션이 감지되면 캡처 스레드 실행
        if (m_bMotionDetect && (bMotionTop || bMotionSide))
        {
            AddLog(L"[Motion] 모션 감지됨. 캡처 시작...");
            // 어떤 카메라에 모션이 감지되었든, 할당된 모든 카메라를 촬영
            TriggerCapture(m_camTop.IsGrabbing(), m_camSide.IsGrabbing());
        }
    }
    catch (...) {
        // OutputDebugString(L"[Basler] RetrieveResult error\n");
    }

    CDialogEx::OnTimer(nIDEvent);
}


// ===================== 3. Pylon -> cv::Mat 변환 (모션 감지용) =====================
void CCanClientDlg::ConvertPylonBufferToMat(CPylonImage& pylonImg, CGrabResultPtr& grabResult, cv::Mat& outMat)
{
    // BGR8로 변환 (기존 미리보기와 동일)
    m_converter.Convert(pylonImg, grabResult);
    const uint8_t* buf = reinterpret_cast<const uint8_t*>(pylonImg.GetBuffer());
    int w = static_cast<int>(grabResult->GetWidth());
    int h = static_cast<int>(grabResult->GetHeight());

    // cv::Mat으로 래핑 (데이터 복사 없음)
    cv::Mat bgrFrame(h, w, CV_8UC3, (void*)buf);

    // 모션 감지를 위해 그레이스케일 + 블러 처리
    cv::cvtColor(bgrFrame, outMat, cv::COLOR_BGR2GRAY);
    cv::GaussianBlur(outMat, outMat, cv::Size(21, 21), 0);
}

// ===================== 3. 모션 감지 함수 (OpenCV) =====================
bool CCanClientDlg::DetectMotion(cv::Mat& processedFrame, CString role)
{
    cv::Mat frameDelta, thresh;
    cv::Mat* pPrevFrame = (role == _T("TOP")) ? &m_prevFrameTop : &m_prevFrameSide;

    // 이전 프레임이 없으면 (첫 프레임이면)
    if (pPrevFrame->empty())
    {
        *pPrevFrame = processedFrame.clone();
        return false; // 첫 프레임은 모션으로 간주 안 함
    }

    // 이전 프레임과 현재 프레임의 차이 계산
    cv::absdiff(*pPrevFrame, processedFrame, frameDelta);
    cv::threshold(frameDelta, thresh, 25, 255, cv::THRESH_BINARY);

    // 딜레이트
    cv::dilate(thresh, thresh, cv::Mat(), cv::Point(-1, -1), 2);

    // 컨투어(윤곽선) 찾기
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(thresh, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    bool motionDetected = false;
    for (const auto& contour : contours)
    {
        if (cv::contourArea(contour) > 1000) // (임계값 1000, 환경에 맞게 조절)
        {
            motionDetected = true;
            break;
        }
    }

    // 현재 프레임을 다음 비교를 위해 이전 프레임으로 저장
    *pPrevFrame = processedFrame.clone();

    return motionDetected;
}


// ===================== BGR8 버퍼 출력 (기존 함수) =====================
void CCanClientDlg::DrawImageBufferToCtrl(const uint8_t* data, int width, int height, CWnd* pWnd)
{
    // (기존 코드와 동일)
    if (!pWnd || !data || width <= 0 || height <= 0) return;
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
    CBrush brush(RGB(0, 0, 0)); dc.FillRect(rc, &brush);
    int oldMode = SetStretchBltMode(dc.GetSafeHdc(), HALFTONE);
    SetBrushOrgEx(dc.GetSafeHdc(), 0, 0, nullptr);
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width; bmi.bmiHeader.biHeight = -height;
    bmi.bmiHeader.biPlanes = 1; bmi.bmiHeader.biBitCount = 24; bmi.bmiHeader.biCompression = BI_RGB;
    StretchDIBits(dc.GetSafeHdc(), drawX, drawY, drawW, drawH, 0, 0, width, height, data, &bmi, DIB_RGB_COLORS, SRCCOPY);
    SetStretchBltMode(dc.GetSafeHdc(), oldMode);
}

// ===================== 1, 2. Picture Control 클리어 =====================
void CCanClientDlg::ClearPictureControl(CWnd* pWnd)
{
    if (pWnd && pWnd->GetSafeHwnd())
    {
        CClientDC dc(pWnd);
        CRect rc; pWnd->GetClientRect(&rc);
        CBrush brush(RGB(0, 0, 0));
        dc.FillRect(rc, &brush);
    }
}

// ===================== 3, 5. 촬영 버튼 (스레드 시작) =====================
void CCanClientDlg::OnBnClickedBtnStart()
{
    // 3. 모션 감지 모드일 때는 수동 버튼이 작동하지 않음
    UpdateData(TRUE);
    if (m_bMotionDetect)
    {
        AddLog(L"[INFO] 모션 감지 모드 활성화 중... 수동 촬영이 비활성화됩니다.");
        return;
    }

    // 5. 캡처가 이미 진행 중이면 중복 실행 방지
    if (m_bCaptureInProgress)
    {
        AddLog(L"[WARNING] 이미 캡처가 진행 중입니다.");
        return;
    }

    AddLog(L"[Manual] 수동 캡처 시작...");
    // 1, 2. 할당된 카메라만 캡처
    TriggerCapture(m_camTop.IsOpen(), m_camSide.IsOpen());
}

// ===================== 5. 캡처 스레드 시작 함수 =====================
void CCanClientDlg::TriggerCapture(bool bUseTop, bool bUseSide)
{
    // 1. 하나도 할당되지 않았으면 시작 안 함
    if (!bUseTop && !bUseSide)
    {
        AddLog(L"[ERROR] 캡처할 카메라가 없습니다. '설정'을 확인하세요.");
        return;
    }

    // 3, 5. 중복 실행 방지 플래그 설정
    m_bCaptureInProgress = true;
    GetDlgItem(IDC_BTN_START)->EnableWindow(FALSE);
    m_checkMotionDetect.EnableWindow(FALSE); // 3.
    GetDlgItem(IDC_BTN_SETTINGS)->EnableWindow(FALSE); // 1, 2.

    // 5. 스레드에 전달할 파라미터 설정
    CaptureThreadParams* pParams = new CaptureThreadParams;
    pParams->pDlg = this;
    pParams->bUseTop = bUseTop;
    pParams->bUseSide = bUseSide;

    // 5. UI 멈춤 방지를 위해 워커 스레드에서 캡처/전송 실행
    AfxBeginThread(CaptureWorkThread, pParams);
}

// ===================== 5. 캡처 완료 메시지 핸들러 =====================
LRESULT CCanClientDlg::OnCaptureComplete(WPARAM wParam, LPARAM lParam)
{
    // 3, 5. 캡처 완료. 플래그 리셋 및 UI 활성화
    m_bCaptureInProgress = false;

    // 모션 감지 모드가 아닐 때만 버튼 활성화 (모션 감지 모드는 타이머가 계속 돔)
    UpdateData(TRUE);
    if (!m_bMotionDetect)
    {
        GetDlgItem(IDC_BTN_START)->EnableWindow(TRUE);
    }
    m_checkMotionDetect.EnableWindow(TRUE); // 3.
    GetDlgItem(IDC_BTN_SETTINGS)->EnableWindow(TRUE); // 1, 2.

    AddLog(L"[INFO] 캡처/전송 작업 완료.");

    // (lParam으로 InspectionResult 포인터를 받아 UI 업데이트)
    InspectionResult* pResult = (InspectionResult*)lParam;
    if (pResult)
    {
        if (pResult->defectType == _T("CAPTURE_FAIL"))
        {
            AddLog(L"[ERROR] 카메라 캡처에 실패했습니다.");
        }
        else
        {
            // UI 업데이트 및 히스토리 추가
            UpdateCurrentResult(*pResult);
            AddToHistory(*pResult);
            AddLog(L"[SUCCESS] 검사 완료 및 결과 표시");
        }
        delete pResult; // 스레드에서 new로 할당한 메모리 해제
    }
    else
    {
        // 캡처는 했으나 응답/파싱 실패
        InspectionResult result;
        result.productId = GenerateProductId();
        result.timestamp = GetCurrentTimestamp();
        result.defectType = _T("에러");
        result.defectDetail = (wParam == 0) ? _T("서버 응답 없음") : Utf8ToCStr((char*)wParam);

        UpdateCurrentResult(result);
        AddToHistory(result);

        if (wParam != 0)
        {
            // 원본 응답 문자열 (char*) 메모리 해제
            delete[](char*)wParam;
        }
        AddLog(L"[ERROR] 서버 응답 파싱 실패.");
    }

    return 0;
}


// ===================== 5. 캡처 워커 스레드 =====================
UINT CCanClientDlg::CaptureWorkThread(LPVOID pParam)
{
    CaptureThreadParams* pParams = (CaptureThreadParams*)pParam;
    if (!pParams || !pParams->pDlg)
    {
        if (pParams) delete pParams;
        return 1;
    }

    // 실제 작업 함수 호출
    pParams->pDlg->ProcessCapture(pParams->bUseTop, pParams->bUseSide);

    delete pParams; // 파라미터 객체 해제
    return 0;
}

// ===================== 5. 실제 캡처/전송 로직 (스레드에서 실행됨) =====================
void CCanClientDlg::ProcessCapture(bool bUseTop, bool bUseSide)
{
    // 타이머 일시 중지 (미리보기와 캡처 충돌 방지)
    // CCanClientDlg 클래스 멤버에 접근하므로 this 포인터 필요
    if (m_timerId) {
        KillTimer(m_timerId);
    }

    try {
        // (카메라는 OnInit/설정에서 이미 열려있다고 가정)
        // (안정화 Sleep은 옵션)
        // Sleep(120); 

        CGrabResultPtr grabTop, grabSide;
        std::vector<unsigned char> bufTop, bufSide;  // 5. 메모리 버퍼
        std::string topResponse, sideResponse;

        // ===== 1) TOP 캡처 & 메모리 인코딩 =====
        if (bUseTop && m_camTop.IsGrabbing() &&
            m_camTop.RetrieveResult(800, grabTop, TimeoutHandling_Return) &&
            grabTop->GrabSucceeded())
        {
            CPylonImage imgTop;
            m_converter.Convert(imgTop, grabTop); // BGR8

            // 5. Pylon BGR8 버퍼 -> cv::Mat 래핑
            cv::Mat frameTop(
                static_cast<int>(grabTop->GetHeight()),
                static_cast<int>(grabTop->GetWidth()),
                CV_8UC3,
                (void*)imgTop.GetBuffer()
            );

            // 5. 메모리로 JPG 인코딩 (90% 품질)
            std::vector<int> params;
            params.push_back(cv::IMWRITE_JPEG_QUALITY);
            params.push_back(90);
            cv::imencode(".jpg", frameTop, bufTop, params);

            // 5. 메모리 버퍼 전송
            SendImageToServer(bufTop, _T("TOP"), topResponse);
            // OutputDebugStringA(("[TOP 응답] " + topResponse + "\n").c_str());
        }

        // Sleep(200); // 서버 처리 대기 (필요시)

        // ===== 2) SIDE 캡처 & 메모리 인코딩 & 전송 =====
        if (bUseSide && m_camSide.IsGrabbing() &&
            m_camSide.RetrieveResult(800, grabSide, TimeoutHandling_Return) &&
            grabSide->GrabSucceeded())
        {
            CPylonImage imgSide;
            m_converter.Convert(imgSide, grabSide); // BGR8

            cv::Mat frameSide(
                static_cast<int>(grabSide->GetHeight()),
                static_cast<int>(grabSide->GetWidth()),
                CV_8UC3,
                (void*)imgSide.GetBuffer()
            );

            std::vector<int> params;
            params.push_back(cv::IMWRITE_JPEG_QUALITY);
            params.push_back(90);
            cv::imencode(".jpg", frameSide, bufSide, params);

            SendImageToServer(bufSide, _T("SIDE"), sideResponse);
            // OutputDebugStringA(("[SIDE 응답] " + sideResponse + "\n").c_str());

            // ===== 3) 검사 결과 처리 =====
            // 1, 2. (요청) SIDE(FRONT) 카메라가 기준이 되도록 기존 로직 유지
            InspectionResult* pResult = new InspectionResult(); // 5. 힙에 할당
            pResult->productId = GenerateProductId();
            pResult->timestamp = GetCurrentTimestamp();

            if (ParseJsonResponse(sideResponse, *pResult)) {
                // 성공
                PostMessage(WM_CAPTURE_COMPLETE, (WPARAM)1, (LPARAM)pResult);
            }
            else {
                // JSON 파싱 실패
                // 5. 원본 응답 문자열을 복사해서 전달 (메모리 해제 필요)
                char* pResponseStr = new char[sideResponse.length() + 1];
                strcpy_s(pResponseStr, sideResponse.length() + 1, sideResponse.c_str());
                PostMessage(WM_CAPTURE_COMPLETE, (WPARAM)pResponseStr, (LPARAM)pResult); // pResult는 메모리 해제용
            }
        }
        else if (bUseTop && !bUseSide)
        {
            // TOP만 있고 SIDE가 없는 경우 (TOP 응답 기준)
            InspectionResult* pResult = new InspectionResult();
            pResult->productId = GenerateProductId();
            pResult->timestamp = GetCurrentTimestamp();

            if (ParseJsonResponse(topResponse, *pResult)) {
                PostMessage(WM_CAPTURE_COMPLETE, (WPARAM)1, (LPARAM)pResult);
            }
            else {
                char* pResponseStr = new char[topResponse.length() + 1];
                strcpy_s(pResponseStr, topResponse.length() + 1, topResponse.c_str());
                PostMessage(WM_CAPTURE_COMPLETE, (WPARAM)pResponseStr, (LPARAM)pResult);
            }
        }
        else
        {
            // 캡처 실패 (SIDE 카메라가 할당되었으나 캡처 실패)
            InspectionResult* pResult = new InspectionResult();
            pResult->defectType = _T("CAPTURE_FAIL"); // 실패 플래그
            PostMessage(WM_CAPTURE_COMPLETE, (WPARAM)0, (LPARAM)pResult);
        }
    }
    catch (const GenericException& e)
    {
        CString msg(e.GetDescription());
        // AfxMessageBox는 스레드에서 사용하면 안 됨
        OutputDebugString(L"[ERROR] 스레드 카메라 에러: " + msg + L"\n");
        InspectionResult* pResult = new InspectionResult();
        pResult->defectType = _T("CAPTURE_FAIL");
        PostMessage(WM_CAPTURE_COMPLETE, (WPARAM)0, (LPARAM)pResult);
    }

    // 타이머 재시작
    m_timerId = SetTimer(1, 33, nullptr);
}


// ===================== 5. TCP 전송 (메모리 버퍼) =====================
bool CCanClientDlg::SendImageToServer(const std::vector<unsigned char>& imgBuffer, CString role, std::string& response)
{
    OutputDebugString(L"[DEBUG] SendImageToServer (Memory) 시작\n");

    if (imgBuffer.empty())
    {
        OutputDebugString(L"[ERROR] 이미지 버퍼가 비어있습니다.\n");
        return false;
    }

    // ===== 1. JSON 생성 (C# 서버 호환) =====
    // C# 서버가 JSON + Image 방식이 아닌,
    // (기존 코드 분석) 4byte (길이) + Image 방식이므로 JSON 불필요.
    // (참고: 만약 C# 서버가 JSON 헤더를 받는다면 여기서 만들어야 함)
    /*
    json j;
    j["role"] = std::string(CT2A(role));
    j["image_size"] = imgBuffer.size();
    std::string json_str = j.dump();
    int json_len = json_str.length();
    */

    // ===== 소켓 생성 =====
    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) {
        OutputDebugString(L"[ERROR] 소켓 생성 실패\n");
        return false;
    }

    // ===== 서버 연결 ===== (기존 코드)
    sockaddr_in serverAddr = {};
    serverAddr.sin_family = AF_INET;
    //serverAddr.sin_port = htons(9000); // (포트 확인)
    //inet_pton(AF_INET, "127.0.0.1", &serverAddr.sin_addr);
    serverAddr.sin_port = htons(8080); // (C# 서버 포트 8080으로 가정)
    inet_pton(AF_INET, "127.0.0.1", &serverAddr.sin_addr); // (IP 확인)

    if (connect(sock, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        int err = WSAGetLastError();
        CString errMsg; errMsg.Format(L"[ERROR] 서버 연결 실패 (WSA: %d)\n", err);
        OutputDebugString(errMsg);
        closesocket(sock);
        return false;
    }

    // ===== 크기 전송 (Big Endian) ===== (기존 코드)
    int fileSize = static_cast<int>(imgBuffer.size());
    int netSize = htonl(fileSize);

    if (send(sock, (char*)&netSize, sizeof(netSize), 0) != sizeof(netSize)) {
        OutputDebugString(L"[ERROR] 길이 전송 실패\n");
        closesocket(sock);
        return false;
    }

    // ===== 데이터 전송 (sendall) ===== (기존 코드, 버퍼 소스만 변경)
    int totalSent = 0;
    while (totalSent < fileSize) {
        int chunk = min(64 * 1024, fileSize - totalSent); // 64KB 청크
        int sent = send(sock, (const char*)imgBuffer.data() + totalSent, chunk, 0);
        if (sent <= 0) {
            OutputDebugString(L"[ERROR] 데이터 전송 실패\n");
            closesocket(sock);
            return false;
        }
        totalSent += sent;
    }
    OutputDebugString(L"[INFO] 이미지 전송 완료 (Memory)\n");

    // ===== 응답 수신 ===== (기존 코드)
    char recvBuf[4096] = { 0 };
    int recvLen = recv(sock, recvBuf, sizeof(recvBuf) - 1, 0);

    if (recvLen > 0) {
        recvBuf[recvLen] = '\0';
        response = std::string(recvBuf);
        OutputDebugStringA(("[응답 수신] " + response + "\n").c_str());
    }
    else {
        OutputDebugString(L"[WARNING] 응답 없음\n");
        response.clear();
    }

    closesocket(sock);
    return true;
}

// ===================== JSON 파싱 (기존 코드) =====================
bool CCanClientDlg::ParseJsonResponse(const std::string& jsonStr, InspectionResult& result)
{
    // (기존 코드와 동일)
    if (jsonStr.empty()) { OutputDebugString(L"[ERROR] JSON 문자열이 비어있음\n"); return false; }
    try {
        auto j = json::parse(jsonStr);
        if (j.contains("result")) {
            std::string resultStr = j["result"].get<std::string>();
            result.defectType = Utf8ToCStr(resultStr);
        }
        else { return false; }
        if (j.contains("reason")) {
            std::string reasonStr = j["reason"].get<std::string>();
            result.defectDetail = Utf8ToCStr(reasonStr);
        }
        else { result.defectDetail = _T(""); }
        return true;
    }
    catch (json::parse_error& e) { CStringA errMsg; errMsg.Format("[ERROR] JSON 파싱 실패: %s\n", e.what()); OutputDebugStringA(errMsg); return false; }
    catch (json::type_error& e) { CStringA errMsg; errMsg.Format("[ERROR] JSON 타입 에러: %s\n", e.what()); OutputDebugStringA(errMsg); return false; }
    catch (std::exception& e) { CStringA errMsg; errMsg.Format("[ERROR] JSON 예외: %s\n", e.what()); OutputDebugStringA(errMsg); return false; }
}

// ===================== UI 업데이트 (기존 코드) =====================
void CCanClientDlg::UpdateCurrentResult(const InspectionResult& result)
{
    // (기존 코드와 동일)
    SetDlgItemText(IDC_STATIC_PRODUCT_ID, result.productId);
    SetDlgItemText(IDC_STATIC_DEFECT_TYPE, result.defectType);
    if (result.defectDetail.IsEmpty() || result.defectType == _T("정상")) {
        SetDlgItemText(IDC_STATIC_DEFECT_DETAIL, _T("-"));
    }
    else {
        SetDlgItemText(IDC_STATIC_DEFECT_DETAIL, result.defectDetail);
    }
}

// ===================== 현재 결과 초기화 (기존 코드) =====================
void CCanClientDlg::ClearCurrentResult()
{
    // (기존 코드와 동일)
    SetDlgItemText(IDC_STATIC_PRODUCT_ID, _T("-"));
    SetDlgItemText(IDC_STATIC_DEFECT_TYPE, _T("-"));
    SetDlgItemText(IDC_STATIC_DEFECT_DETAIL, _T("-"));
}

void CCanClientDlg::AddToHistory(const InspectionResult& result)
{
    // (기존 코드와 동일, 스레드에서 호출되지 않고 메인 스레드(OnCaptureComplete)에서 호출됨)
    m_history.push_back(result);
    int idx = m_historyList.GetItemCount();
    m_historyList.InsertItem(idx, result.productId);
    m_historyList.SetItemText(idx, 1, result.defectType);
    m_historyList.SetItemText(idx, 2, result.defectDetail.IsEmpty() ? _T("-") : result.defectDetail);
    m_historyList.SetItemText(idx, 3, result.timestamp);
    SaveHistoryToFile();
    m_historyList.EnsureVisible(idx, FALSE);
    UpdateStatistics();
}

// ===================== 통계 갱신 (기존 코드) =====================
void CCanClientDlg::UpdateStatistics()
{
    // (기존 코드와 동일)
    int total = 0, normal = 0, defect = 0;
    CString today = CTime::GetCurrentTime().Format(_T("%Y-%m-%d"));
    for (const auto& rec : m_history) {
        if (rec.timestamp.Left(10) == today) {
            total++;
            if (rec.defectType == _T("정상")) normal++;
            else defect++;
        }
    }
    double ratio = (total > 0) ? (normal * 100.0 / total) : 0.0;
    CString strToday, strOkNg, strRate;
    strToday.Format(_T("오늘 검사량 : %d개"), total);
    strOkNg.Format(_T("정상 : %d개 / 불량 : %d개"), normal, defect);
    strRate.Format(_T("정상 비율 : %.0f%%"), ratio);
    SetDlgItemText(IDC_STATIC_TODAY_CNT, strToday);
    SetDlgItemText(IDC_STATIC_OK_NG, strOkNg);
    SetDlgItemText(IDC_STATIC_RATE, strRate);
}

// ===================== 유틸리티 함수 (기존 코드) =====================
CString CCanClientDlg::GenerateProductId()
{
    // (기존 코드와 동일, 스레드에서 호출되지만 m_productCounter 접근은?)
    // 5. 스레드 안전성을 위해 InterlockedIncrement 사용
    LONG newId = InterlockedIncrement((LONG*)&m_productCounter); // 원자적 증가
    CString productId;
    productId.Format(_T("CK%04d"), newId);
    return productId;
}

CString CCanClientDlg::GetCurrentTimestamp()
{
    // (기존 코드와 동일)
    CTime now = CTime::GetCurrentTime();
    return now.Format(_T("%Y-%m-%d %H:%M:%S"));
}

// ===================== 히스토리 저장/로드 (기존 코드) =====================
void CCanClientDlg::SaveHistoryToFile()
{
    // (기존 코드와 동일)
    CString folder = _T("C:\\CanClient"); CreateDirectory(folder, NULL);
    CString filePath = folder + _T("\\history.txt");
    CStdioFile file;
    if (!file.Open(filePath, CFile::modeCreate | CFile::modeWrite | CFile::typeText)) { return; }
    for (const auto& rec : m_history) {
        CString line;
        line.Format(_T("%s|%s|%s|%s\n"),
            rec.productId.GetString(), rec.defectType.GetString(),
            (rec.defectDetail.IsEmpty() ? _T("-") : rec.defectDetail.GetString()),
            rec.timestamp.GetString());
        file.WriteString(line);
    }
    file.Close();
}

void CCanClientDlg::LoadHistoryFromFile()
{
    // (기존 코드와 동일, m_productCounter 설정 부분 포함)
    CString filePath = _T("C:\\CanClient\\history.txt");
    CStdioFile file;
    if (!file.Open(filePath, CFile::modeRead | CFile::typeText)) { return; }
    CString line;
    int maxId = 1011;
    while (file.ReadString(line)) {
        line.Trim(); if (line.IsEmpty()) continue;
        std::vector<CString> tokens;
        int cur = 0;
        while (true) {
            int next = line.Find(_T("|"), cur);
            if (next == -1) { tokens.push_back(line.Mid(cur)); break; }
            tokens.push_back(line.Mid(cur, next - cur)); cur = next + 1;
        }
        if (tokens.size() < 4) continue;
        InspectionResult rec;
        rec.productId = tokens[0].Trim(); rec.defectType = tokens[1].Trim();
        rec.defectDetail = tokens[2].Trim(); rec.timestamp = tokens[3].Trim();
        if (rec.defectDetail == _T("-")) rec.defectDetail.Empty();
        if (!rec.productId.IsEmpty()) {
            m_history.push_back(rec);
            int idx = m_historyList.GetItemCount();
            m_historyList.InsertItem(idx, rec.productId);
            m_historyList.SetItemText(idx, 1, rec.defectType);
            m_historyList.SetItemText(idx, 2, rec.defectDetail.IsEmpty() ? _T("-") : rec.defectDetail);
            m_historyList.SetItemText(idx, 3, rec.timestamp);
            CString numStr = rec.productId.Mid(2);
            int num = _ttoi(numStr);
            if (num > maxId) maxId = num;
        }
    }
    m_productCounter = maxId + 1; // 5. 스레드 안전성을 위해 m_productCounter는 LONG 타입이어야 함. (헤더에서 int -> LONG 변경 필요)
    file.Close();
    UpdateStatistics(); // 5. 로드 후 통계 갱신
}

// ===================== 1, 2. 설정 버튼 핸들러 =====================
void CCanClientDlg::OnBnClickedBtnSettings()
{
    AddLog(L"[INFO] 설정 창 열기...");

    // 설정창 열기 전 Pylon 장치 목록 다시 스캔
    ScanPylonDevices();

    // 설정 대화상자에 현재 값 전달
    m_settingsDlg.m_availableDevices = m_availableDevices;
    m_settingsDlg.m_currentTopSerial = m_topCamSerial;
    m_settingsDlg.m_currentSideSerial = m_sideCamSerial;

    if (m_settingsDlg.DoModal() == IDOK)
    {
        // 설정 대화상자에서 "OK"를 눌렀을 때
        m_topCamSerial = m_settingsDlg.m_selectedTopSerial;
        m_sideCamSerial = m_settingsDlg.m_selectedSideSerial;

        AddLog(L"[INFO] 카메라 설정 변경됨. 카메라 다시 여는 중...");

        // 1, 2. 변경된 설정으로 카메라 다시 열기
        if (!OpenAssignedCameras())
        {
            AfxMessageBox(L"선택한 카메라 열기에 실패했습니다.");
        }

        // 3. 모션 감지용 이전 프레임 리셋
        m_prevFrameTop.release();
        m_prevFrameSide.release();
    }
}

// ===================== 5. 스레드 안전한 로그 (예시) =====================
void CCanClientDlg::AddLog(const CString& msg)
{
    // (실제 구현에서는 CListBox 대신 파일/디버그 출력 사용)
    OutputDebugString(msg + L"\n");

    // (만약 CListBox에 로그를 남긴다면, PostMessage 등으로 메인 스레드에서 처리해야 함)
}


// ===================== 종료 =====================
void CCanClientDlg::OnDestroy()
{
    CDialogEx::OnDestroy();

    if (m_timerId) {
        KillTimer(m_timerId);
        m_timerId = 0;
    }

    // 5. 캡처 스레드가 진행 중일 수 있으므로, 종료 대기
    m_bCaptureInProgress = true; // (새 스레드 방지)
    Sleep(1000); // (간단한 1초 대기. 실제로는 Event/Mutex 등으로 종료 대기 필요)

    try {
        CloseAllCameras(); // 1, 2.
        PylonTerminate();
    }
    catch (...) {}

    if (m_wsaInitialized) {
        WSACleanup();
        m_wsaInitialized = false;
        OutputDebugString(L"[INFO] WSA 종료\n");
    }
}