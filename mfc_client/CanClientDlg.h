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

// [NEW] GDI+ for modern UI and image preview
#include <gdiplus.h>
#pragma comment (lib,"gdiplus.lib")

using namespace Pylon;

// --- Forward Declarations ---
class CCanClientDlg;

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
    virtual ~CCanClientDlg(); // [NEW] Added destructor for cleanup

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
    afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor); // Dark Mode UI
    DECLARE_MESSAGE_MAP()

private:
    HICON m_hIcon;

    // --- GDI+ ---
    ULONG_PTR m_gdiplusToken;
    void InitGDIPlus();
    void ShutdownGDIPlus();

    // --- Dark Mode UI ---
    CBrush m_brBkg;     // Dialog background
    CBrush m_brList;    // List/Edit background
    CBrush m_brStatic;  // Static text/groupbox background

    // --- Pylon Cameras ---
    Pylon::DeviceInfoList_t m_availableDevices;
    CInstantCamera        m_camTop;
    CInstantCamera        m_camSide; // [RENAMED] m_camFront -> m_camSide
    CImageFormatConverter m_converter;
    CPylonImage           m_pylonImage;
    UINT_PTR              m_timerId;

    // --- Motion Detection ---
    CButton m_checkMotionDetect;
    BOOL    m_bMotionDetect;
    cv::Mat m_prevFrameTop;
    cv::Mat m_prevFrameSide;
    bool DetectMotion(cv::Mat& currentFrame, CString role); // Implementation added
    void ConvertPylonBufferToMat(CPylonImage& pylonImg, CGrabResultPtr& grabResult, cv::Mat& outMat);

    // --- Capture Thread ---
    bool     m_bCaptureInProgress;
    HANDLE   m_evtShutdown; // Event for safe thread shutdown
    CWinThread* m_pCaptureThread; // [NEW] Pointer to manage the thread
    static UINT CaptureWorkThread(LPVOID pParam);
    void TriggerCapture(bool bUseTop, bool bUseSide);
    void ProcessCapture(bool bUseTop, bool bUseSide);

    // --- Network & Settings ---
    bool m_wsaInitialized;
    CString m_strServerIP;
    int     m_nUploadPort;
    int     m_nRequestPort;
    void LoadAppSettings();
    void SaveAppSettings();

    // --- Dialog Controls & Data ---
    CCameraSettingsDlg m_settingsDlg;
    CListCtrl m_historyList;
    std::vector<InspectionResult> m_history;
    LONG m_productCounter; // Changed to LONG for InterlockedIncrement

    // --- Camera Settings ---
    CString m_topCamSerial;
    CString m_sideCamSerial;

    // --- [NEW] Advanced Camera Settings Storage ---
    double m_dTopFps, m_dTopExposure, m_dTopGain;
    double m_dSideFps, m_dSideExposure, m_dSideGain;
    bool ApplyAdvancedCameraSettings(CInstantCamera& cam, double fps, double exposure, double gain);
    bool SetPylonFloatValue(CInstantCamera& cam, const char* paramName, double value); // Helper


    // --- Helper Functions ---
    void DrawImageBufferToCtrl(const uint8_t* data, int width, int height, CWnd* pWnd);
    void ClearPictureControl(CWnd* pWnd);
    bool SendImageToServer(const std::vector<unsigned char>& imgBuffer, CString role, std::string& response);
    // [REMOVED] RequestImageFromServer (replaced by local file logic)
    void InitHistoryList();
    void UpdateCurrentResult(const InspectionResult& result);
    void AddToHistory(const InspectionResult& result);
    void ClearCurrentResult();
    void UpdateStatistics();
    void LoadHistoryFromFile();
    void SaveHistoryToFile();
    CString GenerateProductId();
    CString GetCurrentTimestamp();
    bool ParseJsonResponse(const std::string& jsonStr, InspectionResult& result);
    void ScanPylonDevices();
    bool OpenAssignedCameras();
    void CloseAllCameras();
    void AddLog(const CString& msg);
};

