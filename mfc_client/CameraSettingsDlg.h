#pragma once
#include "afxdialogex.h"
#include <vector>
#include <map>
#include <pylon/PylonIncludes.h> // Pylon DeviceInfo

// CCameraSettingsDlg dialog

class CCameraSettingsDlg : public CDialogEx
{
	DECLARE_DYNAMIC(CCameraSettingsDlg)

public:
	CCameraSettingsDlg(CWnd* pParent = nullptr);   // standard constructor
	virtual ~CCameraSettingsDlg();

	// Dialog Data
#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_CAMERA_SETTINGS };
#endif

protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support
	virtual BOOL OnInitDialog();

	// [FIX] 'OnBnClickedOk' 함수의 선언이 누락되어 추가했습니다.
	afx_msg void OnBnClickedOk();
	DECLARE_MESSAGE_MAP()

public:
	// 부모(CanClientDlg)가 설정해줄 데이터
	Pylon::DeviceInfoList_t m_availableDevices; // Pylon 장치 정보 리스트
	CString m_currentTopSerial;  // 현재 설정된 TOP 카메라 시리얼
	CString m_currentSideSerial; // 현재 설정된 SIDE 카메라 시리얼

	// 대화상자에서 선택한 결과를 부모에게 다시 전달할 데이터
	CString m_selectedTopSerial;
	CString m_selectedSideSerial;

	// [NEW] Server Settings (Passed from/to Parent)
	CString m_strServerIP;
	int m_nUploadPort;
	int m_nRequestPort;

private:
	CComboBox m_comboTop;
	CComboBox m_comboSide;
	void PopulateComboBoxes();
	CString GetDeviceString(const Pylon::CDeviceInfo& dev);

	// [NEW] Server Controls
	CEdit m_editServerIP;
	CEdit m_editUploadPort;
	CEdit m_editRequestPort;

public:
	afx_msg void OnCbnSelchangeCombo1();
	afx_msg void OnEnChangeEditServerIp();
};

