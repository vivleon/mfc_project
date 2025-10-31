#pragma once
#include <vector>
#include <string>
#include <map>
#include <afxmt.h> // For CEvent

// [FIX] Include Winsock headers before Pylon to avoid conflicts
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib") // Ensure linker dependency
#pragma comment(lib, "shlwapi.lib")

#include <pylon/PylonIncludes.h>
#include <opencv2/opencv.hpp>
#include "CameraSettingsDlg.h"
#include "PreviewDlg.h"      // For Preview Dialog
#include "CanClient.h" // [FIX] CanClient.h 추가

// [NEW] GDI+ for modern UI and image preview
#include <gdiplus.h>
#pragma comment (lib,"gdiplus.lib")


#define WM_APP_POSTINIT (WM_APP + 1)
// [FIX] 워커 스레드 -> UI 스레드로 보낼 사용자 정의 메시지 (ID 충돌 수정)
#define WM_APP_CAPTURE_COMPLETE (WM_APP + 2) // <-- (WM_APP + 1)에서 (WM_APP + 2)로 수정됨

using namespace Pylon;

// --- Forward Declarations ---
// class CCanClientDlg; // CanClient.h로 이동

// --- Structs ---
struct InspectionResult
{
    CString productId;
    CString defectType;
    CString defectDetail;
    CString timestamp;
};

struct CaptureThreadParams
{
    CCanClientDlg* pDlg;
    bool bUseTop;
    bool bUseSide;
};

// [FIX] 사용되지 않는 'CaptureResult' 구조체 제거
// (프로젝트가 'InspectionResult'를 사용하므로 혼동을 막기 위해 삭제)


// --- Main Dialog Class ---
class CCanClientDlg : public CDialogEx
{
public:
    CCanClientDlg(CWnd* pParent = nullptr);
    virtual ~CCanClientDlg() noexcept;

#ifdef AFX_DESIGN_TIME
    enum { IDD = IDD_CANCLIENT_DIALOG };
#endif

protected:
    virtual void DoDataExchange(CDataExchange* pDX);
    virtual BOOL OnInitDialog();
    afx_msg void OnDestroy();
    afx_msg void OnBnClickedBtnStart();
    afx_msg void OnTimer(UINT_PTR nIDEvent);
    afx_msg void OnBnClickedBtnSettings();
    afx_msg LRESULT OnCaptureComplete(WPARAM wParam, LPARAM lParam);
    afx_msg void OnDblclkListHistory(NMHDR* pNMHDR, LRESULT* pResult); // List double-click
    afx_msg LRESULT OnPostInit(WPARAM wParam, LPARAM lParam);

    // [NEW] 신규 기능 핸들러
    afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor); // 불량 결과 빨간색 표시
    afx_msg void OnBnClickedBtnExportHistory(); // 이력 내보내기
    afx_msg void OnBnClickedCheckMotion(); // [FIX] 모션 캡처 무한 루프 방지용

    DECLARE_MESSAGE_MAP()

private:
    HICON m_hIcon;

    // --- GDI+ ---
    ULONG_PTR m_gdiplusToken;
    void InitGDIPlus();
    void ShutdownGDIPlus();

    // --- Pylon Cameras ---
    Pylon::DeviceInfoList_t m_availableDevices;
    CInstantCamera        m_camTop;
    CInstantCamera        m_camSide;
    CImageFormatConverter m_converter;
    CPylonImage           m_pylonImage;
    UINT_PTR              m_timerId;

    // --- Motion Detection ---
    CButton m_checkMotionDetect;
    BOOL    m_bMotionDetect;
    cv::Mat m_prevFrameTop;
    cv::Mat m_prevFrameSide;
    bool DetectMotion(cv::Mat& currentFrame, CString role);
    void ConvertPylonBufferToMat(CPylonImage& pylonImg, CGrabResultPtr& grabResult, cv::Mat& outMat);

    // --- Capture Thread ---
    bool    m_bCaptureInProgress;
    HANDLE  m_evtShutdown;
    CWinThread* m_pCaptureThread; // [FIX] 중복 선언 제거 (여기 하나만 둠)
    static UINT CaptureWorkThread(LPVOID pParam);
    void TriggerCapture(bool bUseTop, bool bUseSide);
    void ProcessCapture(bool bUseTop, bool bUseSide);

    // [FIX] 타이머 중복 실행 방지 플래그
    bool m_bTimerBusy;

    // --- Network & Settings ---
    bool m_wsaInitialized;
    // [FIX] 설정 변수들은 App 클래스에서 관리하도록 public으로 이동

    // --- Dialog Controls & Data ---
    CCameraSettingsDlg m_settingsDlg;
    CListCtrl m_historyList;
    std::vector<InspectionResult> m_history;
    LONG m_productCounter;
    CBrush m_brushRed; // [NEW] 불량 결과 표시용 브러시

    // --- Helper Functions ---
    void DrawImageBufferToCtrl(const uint8_t* data, int width, int height, CWnd* pWnd);
    void ClearPictureControl(CWnd* pWnd);
    bool SendImageToServer(const std::vector<unsigned char>& imgBuffer, CString role, std::string& response);
    void InitHistoryList();
    void UpdateCurrentResult(const InspectionResult& result);
    void AddToHistory(const InspectionResult& result);
    void ClearCurrentResult();
    void UpdateStatistics();
    void LoadHistoryFromFile(); // [FIX] 주석 해제
    void SaveHistoryToFile(); // [FIX] 주석 해제

    // [FIX] CSV 파싱 헬퍼 선언 추가 (컴파일 오류 수정)
    void ProcessHistoryLine(CString line, long& maxId);

    // [NEW] 히스토리 파일 경로 헬퍼 선언 추가
    CString GetHistoryFilePath();

    CString GenerateProductId();
    CString GetCurrentTimestamp();
    bool ParseJsonResponse(const std::string& jsonStr, InspectionResult& result);
    void ScanPylonDevices();
    bool OpenAssignedCameras();
    void CloseAllCameras();
    void AddLog(const CString& msg);

    // [NEW] 로컬 이미지 저장을 위한 헬퍼
    void SaveImageLocally(const cv::Mat& frame, CString role, CString productId);

    // [FIX] 설정 변수들을 App 클래스에서 접근할 수 있도록 Public으로 이동
public:
    CString m_strServerIP;
    int     m_nUploadPort;    // [FIX] 특수 문자(0xa0) 제거
    int     m_nRequestPort;   // [FIX] 특수 문자(0xa0) 제거
    CString m_topCamSerial;   // [FIX] 중복 선언 제거 (여기에만 둠)
    CString m_sideCamSerial; // [FIX] 중복 선언 제거

    // --- Advanced Camera Settings Storage ---
    double m_dTopFps, m_dTopExposure, m_dTopGain;    // [FIX] 중복 선언 제거
    double m_dSideFps, m_dSideExposure, m_dSideGain; // [FIX] 중복 선언 제거

    bool ApplyAdvancedCameraSettings(CInstantCamera& cam, double fps, double exposure, double gain);
    bool SetPylonFloatValue(CInstantCamera& cam, const char* paramName, double value); // Helper
};