#pragma once
#include <pylon/PylonIncludes.h>
#include <vector>
#include <string>
#include <map>
#include "CameraSettingsDlg.h" // 1, 2. 설정 대화상자

// 3, 5. OpenCV 헤더 (모션 감지 및 메모리 내 인코딩용)
#include <opencv2/opencv.hpp> 

using namespace Pylon;

// ===== 검사 결과 구조체 =====
struct InspectionResult
{
    CString productId;      // 제품번호
    CString defectType;     // 판정결과 ("정상" / "불량" / "에러")
    CString defectDetail;   // 불량종류
    CString timestamp;      // 시간
};

class CCanClientDlg;

// 5. 스레드 전달용 구조체
struct CaptureThreadParams
{
    CCanClientDlg* pDlg;
    bool bUseTop;
    bool bUseSide;
};

class CCanClientDlg : public CDialogEx
{
public:
    CCanClientDlg(CWnd* pParent = nullptr);

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
    afx_msg LRESULT OnCaptureComplete(WPARAM wParam, LPARAM lParam); // [FIX] Removed duplicate declaration
    afx_msg void OnDblclkListHistory(NMHDR* pNMHDR, LRESULT* pResult);
    DECLARE_MESSAGE_MAP()

private:
    HICON m_hIcon;

    // Camera Members (Unchanged)
    Pylon::DeviceInfoList_t m_availableDevices;
    CString m_topCamSerial;
    CString m_sideCamSerial;
    CCameraSettingsDlg m_settingsDlg;
    CInstantCamera        m_camTop;
    CInstantCamera        m_camSide;
    CImageFormatConverter m_converter;
    CPylonImage           m_pylonImage;
    UINT_PTR              m_timerId = 0;

    // Motion Detection Members (Unchanged)
    CButton m_checkMotionDetect;
    BOOL    m_bMotionDetect;
    cv::Mat m_prevFrameTop;
    cv::Mat m_prevFrameSide;
    bool    m_bCaptureInProgress;
    bool DetectMotion(cv::Mat& currentFrame, CString role);
    void ConvertPylonBufferToMat(CPylonImage& pylonImg, CGrabResultPtr& grabResult, cv::Mat& outMat);

    // Thread, Network, UI, Data Members
    static UINT CaptureWorkThread(LPVOID pParam);
    void TriggerCapture(bool bUseTop, bool bUseSide);
    void ProcessCapture(bool bUseTop, bool bUseSide);
    bool m_wsaInitialized = false;
    CListCtrl m_historyList;
    std::vector<InspectionResult> m_history;
    int m_productCounter = 1012;

    // [NEW] Server IP and Port Members
    CString m_strServerIP;
    int m_nUploadPort;
    int m_nRequestPort;

    // Helper Functions
    void DrawImageBufferToCtrl(const uint8_t* data, int width, int height, CWnd* pWnd);
    void ClearPictureControl(CWnd* pWnd);
    bool SendImageToServer(const std::vector<unsigned char>& imgBuffer, CString role, std::string& response);
    bool RequestImageFromServer(CString productID, CString role, std::vector<unsigned char>& imgBuffer);
    void InitHistoryList();
    void UpdateCurrentResult(const InspectionResult& result);
    void AddToHistory(const InspectionResult& result);
    void ClearCurrentResult();
    void UpdateStatistics();
    void LoadHistoryFromFile();
    void SaveHistoryToFile();
    CString GenerateProductId();
    CString GetCurrentTimestamp();
    bool ParseJsonResponse(const std::string& json, InspectionResult& result);
    void ScanPylonDevices();
    bool OpenAssignedCameras();
    void CloseAllCameras();
    void AddLog(const CString& msg);

    // [NEW] Settings Load/Save Function Declarations
    void LoadAppSettings();
    void SaveAppSettings();
};
