// CanClientDlg.h

#pragma once
#include <vector>
#include <string>
#include <map>
#include <afxmt.h> // For CEvent

// [FIX] Include Winsock headers before Pylon to avoid conflicts
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib") // Ensure linker dependency

#include <pylon/PylonIncludes.h>
#include <opencv2/opencv.hpp>
#include "CameraSettingsDlg.h"
#include "PreviewDlg.h"       // For Preview Dialog
#include "CanClient.h" // [FIX] CanClient.h 추가

// [NEW] GDI+ for modern UI and image preview
#include <gdiplus.h>
#pragma comment (lib,"gdiplus.lib")


#define WM_APP_POSTINIT (WM_APP + 1)

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
    // afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor); // [UI] 제거
    afx_msg LRESULT OnPostInit(WPARAM wParam, LPARAM lParam);
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
    CString GenerateProductId();
    CString GetCurrentTimestamp();
    bool ParseJsonResponse(const std::string& jsonStr, InspectionResult& result);
    void ScanPylonDevices();
    bool OpenAssignedCameras();
    void CloseAllCameras();
    void AddLog(const CString& msg);

    // [FIX] 설정 변수들을 App 클래스에서 접근할 수 있도록 Public으로 이동
public:
    CString m_strServerIP;
    int     m_nUploadPort;   // [FIX] 특수 문자(0xa0) 제거
    int     m_nRequestPort;  // [FIX] 특수 문자(0xa0) 제거
    CString m_topCamSerial;  // [FIX] 중복 선언 제거 (여기에만 둠)
    CString m_sideCamSerial; // [FIX] 중복 선언 제거

    // --- Advanced Camera Settings Storage ---
    double m_dTopFps, m_dTopExposure, m_dTopGain;    // [FIX] 중복 선언 제거
    double m_dSideFps, m_dSideExposure, m_dSideGain; // [FIX] 중복 선언 제거

    bool ApplyAdvancedCameraSettings(CInstantCamera& cam, double fps, double exposure, double gain);
    bool SetPylonFloatValue(CInstantCamera& cam, const char* paramName, double value); // Helper
};