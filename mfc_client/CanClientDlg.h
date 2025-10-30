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
    afx_msg void OnBnClickedBtnSettings(); // 1, 2. 설정 버튼 핸들러
    DECLARE_MESSAGE_MAP()

private:
    HICON m_hIcon;

    // ===== 1, 2. 카메라 (Pylon) =====
    Pylon::DeviceInfoList_t m_availableDevices; // 사용 가능한 모든 장치
    CString m_topCamSerial;    // "TOP" 역할에 할당된 시리얼
    CString m_sideCamSerial;   // "SIDE" 역할에 할당된 시리얼
    CCameraSettingsDlg m_settingsDlg; // 설정 대화상자 인스턴스

    CInstantCamera        m_camTop;
    CInstantCamera        m_camSide; // m_camFront -> m_camSide (명칭 변경)
    CImageFormatConverter m_converter;   // BGR8 변환용
    CPylonImage           m_pylonImage;  // 미리보기/캡처 공용 버퍼
    UINT_PTR              m_timerId = 0;

    // ===== 3. 모션 감지 =====
    CButton m_checkMotionDetect;
    BOOL    m_bMotionDetect;
    cv::Mat m_prevFrameTop;  // 모션 감지용 이전 프레임
    cv::Mat m_prevFrameSide;
    bool    m_bCaptureInProgress; // 캡처 스레드 중복 실행 방지 플래그
    bool DetectMotion(cv::Mat& currentFrame, CString role); // 모션 감지 함수
    void ConvertPylonBufferToMat(CPylonImage& pylonImg, CGrabResultPtr& grabResult, cv::Mat& outMat);

    // ===== 5. 비동기 캡처 =====
    static UINT CaptureWorkThread(LPVOID pParam); // 5. 캡처/전송 스레드
    void TriggerCapture(bool bUseTop, bool bUseSide); // 스레드 시작 함수
    void ProcessCapture(bool bUseTop, bool bUseSide); // 스레드 실제 작업 함수

    // ===== 네트워크 =====
    bool m_wsaInitialized = false;

    // ===== UI 컨트롤 =====
    CListCtrl m_historyList;

    // ===== 데이터 =====
    std::vector<InspectionResult> m_history;
    int m_productCounter = 1012; // CK1012부터 시작

    // ===== 헬퍼 함수 =====
    void DrawImageBufferToCtrl(const uint8_t* data, int width, int height, CWnd* pWnd);
    void ClearPictureControl(CWnd* pWnd);

    // 5. 네트워크 (메모리 버퍼 전송)
    bool SendImageToServer(const std::vector<unsigned char>& imgBuffer, CString role, std::string& response);

    // UI 업데이트
    void InitHistoryList();
    void UpdateCurrentResult(const InspectionResult& result);
    void AddToHistory(const InspectionResult& result);
    void ClearCurrentResult();
    void UpdateStatistics();

    // 히스토리 관리
    void LoadHistoryFromFile();
    void SaveHistoryToFile();

    // 유틸리티
    CString GenerateProductId();
    CString GetCurrentTimestamp();

    // JSON 파싱 (기존 함수 재사용)
    bool ParseJsonResponse(const std::string& json, InspectionResult& result);

    // 1, 2. 카메라 스캔/연결
    void ScanPylonDevices();
    bool OpenAssignedCameras();
    void CloseAllCameras();

    // 5. 스레드 안전한 UI 업데이트
    void AddLog(const CString& msg); // 디버그/상태 로깅용
    afx_msg LRESULT OnCaptureComplete(WPARAM wParam, LPARAM lParam); // 캡처 완료 메시지
};