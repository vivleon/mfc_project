// CameraSettingsDlg.cpp : implementation file
//

#include "pch.h"
#include "CanClient.h" // Main App
#include "afxdialogex.h"
#include "CameraSettingsDlg.h"
#include "resource.h" // IDD_CAMERA_SETTINGS

IMPLEMENT_DYNAMIC(CCameraSettingsDlg, CDialogEx)

CCameraSettingsDlg::CCameraSettingsDlg(CWnd* pParent /*=nullptr*/)
	: CDialogEx(IDD_CAMERA_SETTINGS, pParent)
	, m_selectedTopSerial(_T(""))
	, m_selectedSideSerial(_T(""))
{

}

CCameraSettingsDlg::~CCameraSettingsDlg()
{
}

void CCameraSettingsDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialogEx::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_COMBO_TOP, m_comboTop);
	DDX_Control(pDX, IDC_COMBO_SIDE, m_comboSide);
}


BEGIN_MESSAGE_MAP(CCameraSettingsDlg, CDialogEx)
	ON_BN_CLICKED(IDOK, &CCameraSettingsDlg::OnBnClickedOk)
END_MESSAGE_MAP()


// CCameraSettingsDlg message handlers

BOOL CCameraSettingsDlg::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	PopulateComboBoxes();

	return TRUE;
}

// Pylon 장치 정보로 콤보박스 채우기
CString CCameraSettingsDlg::GetDeviceString(const Pylon::CDeviceInfo& dev)
{
	CString str;
	str.Format(_T("%s (%s)"),
		CString(dev.GetFriendlyName().c_str()),
		CString(dev.GetSerialNumber().c_str()));
	return str;
}


void CCameraSettingsDlg::PopulateComboBoxes()
{
	m_comboTop.ResetContent();
	m_comboSide.ResetContent();

	CString strNone = _T("None (사용 안 함)");
	m_comboTop.AddString(strNone);
	m_comboSide.AddString(strNone);

	// "None"에 대한 데이터로 빈 시리얼 번호("")를 설정
	m_comboTop.SetItemData(0, (DWORD_PTR)(new CString(_T(""))));
	m_comboSide.SetItemData(0, (DWORD_PTR)(new CString(_T(""))));

	int currentTopSelection = 0; // "None"이 기본값
	int currentSideSelection = 0; // "None"이 기본값

	for (size_t i = 0; i < m_availableDevices.size(); ++i)
	{
		CString strCam = GetDeviceString(m_availableDevices[i]);
		CString serial = CString(m_availableDevices[i].GetSerialNumber().c_str());

		// 콤보박스 아이템 데이터로 CString 객체 포인터를 저장 (나중에 OK에서 읽고 delete)
		int listIndexTop = m_comboTop.AddString(strCam);
		m_comboTop.SetItemData(listIndexTop, (DWORD_PTR)(new CString(serial)));

		int listIndexSide = m_comboSide.AddString(strCam);
		m_comboSide.SetItemData(listIndexSide, (DWORD_PTR)(new CString(serial)));

		// 현재 설정된 역할을 콤보박스에 표시
		if (m_currentTopSerial == serial)
		{
			currentTopSelection = listIndexTop;
		}
		if (m_currentSideSerial == serial)
		{
			currentSideSelection = listIndexSide;
		}
	}

	m_comboTop.SetCurSel(currentTopSelection);
	m_comboSide.SetCurSel(currentSideSelection);
}


void CCameraSettingsDlg::OnBnClickedOk()
{
	// "OK" 버튼 클릭 시 선택된 시리얼 번호를 멤버 변수에 저장
	int selTop = m_comboTop.GetCurSel();
	if (selTop != CB_ERR)
	{
		CString* pSerial = (CString*)m_comboTop.GetItemData(selTop);
		m_selectedTopSerial = *pSerial;
	}

	int selSide = m_comboSide.GetCurSel();
	if (selSide != CB_ERR)
	{
		CString* pSerial = (CString*)m_comboSide.GetItemData(selSide);
		m_selectedSideSerial = *pSerial;
	}

	// 콤보박스 아이템 데이터로 할당된 CString 포인터들 삭제
	for (int i = 0; i < m_comboTop.GetCount(); ++i)
	{
		delete (CString*)m_comboTop.GetItemData(i);
	}
	for (int i = 0; i < m_comboSide.GetCount(); ++i)
	{
		delete (CString*)m_comboSide.GetItemData(i);
	}

	CDialogEx::OnOK(); // 대화상자를 닫습니다.
}