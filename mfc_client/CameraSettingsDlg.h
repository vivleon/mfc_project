#pragma once
#include "afxdialogex.h"
#include <vector>
#include <map>
#include <pylon/PylonIncludes.h> // Pylon DeviceInfo & Camera
#include <afxcmn.h> // For CSliderCtrl, CTabCtrl

class CCameraSettingsDlg : public CDialogEx
{
	DECLARE_DYNAMIC(CCameraSettingsDlg)

public:
	CCameraSettingsDlg(CWnd* pParent = nullptr);
	virtual ~CCameraSettingsDlg();

#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_CAMERA_SETTINGS };
#endif

protected:
	virtual void DoDataExchange(CDataExchange* pDX);
	virtual BOOL OnInitDialog();
	afx_msg void OnBnClickedOk();
	afx_msg void OnTcnSelchangeTabSettings(NMHDR* pNMHDR, LRESULT* pResult); // Tab change handler
	afx_msg void OnHScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar); // Slider handler
	DECLARE_MESSAGE_MAP()

public:
	// --- Input Data from Parent ---
	Pylon::DeviceInfoList_t m_availableDevices;
	CString m_currentTopSerial;
	CString m_currentSideSerial;
	CString m_strServerIP;
	int     m_nUploadPort;
	int     m_nRequestPort;
	// [NEW] Pointers to actual camera objects to read parameters
	Pylon::CInstantCamera* m_pCamTop;
	Pylon::CInstantCamera* m_pCamSide;

	// --- Output Data to Parent ---
	CString m_selectedTopSerial;
	CString m_selectedSideSerial;
	// [NEW] Selected advanced parameters
	double m_dFps;
	double m_dExposure;
	double m_dGain;
	CString m_sSelectedCamRole; // "TOP" or "SIDE" for which params were loaded/saved


private:
	// --- Basic Controls ---
	CComboBox m_comboTop;
	CComboBox m_comboSide;
	CEdit m_editServerIP;
	CEdit m_editUploadPort;
	CEdit m_editRequestPort;

	// --- Advanced Controls ---
	CTabCtrl m_tabSettings;
	CStatic m_groupAdvSettings; // Groupbox for advanced
	CSliderCtrl m_sliderFps;
	CEdit m_editFps;
	CSliderCtrl m_sliderExposure;
	CEdit m_editExposure;
	CSliderCtrl m_sliderGain;
	CEdit m_editGain;
	CStatic m_staticTargetCam; // Label showing target camera

	// --- Internal Helpers ---
	void PopulateComboBoxes();
	CString GetDeviceString(const Pylon::CDeviceInfo& dev);
	void ShowTabControls(int nTab);
	void InitAdvancedControls();
	void LoadCameraParameters(); // Load parameters from selected camera
	void SaveCameraParameters(); // Save parameters from UI controls to members
	bool UpdateEditFromSlider(CSliderCtrl& slider, CEdit& edit, double minVal, double maxVal, CString format = _T("%.1f"));
	bool UpdateSliderFromEdit(CEdit& edit, CSliderCtrl& slider, double minVal, double maxVal);

	// Pylon parameter helpers
	template<typename TParam>
	bool GetPylonValue(Pylon::CInstantCamera* pCam, const char* paramName, TParam& value);
	template<typename TParam>
	void UpdateSliderRange(Pylon::CInstantCamera* pCam, const char* paramName, CSliderCtrl& slider, CEdit& edit);

	// Helper to get the currently selected active camera for advanced settings
	Pylon::CInstantCamera* GetSelectedCameraForAdvancedSettings(CString& role);

};

