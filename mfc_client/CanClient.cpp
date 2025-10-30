// CanClient.cpp

#include "pch.h"
#include "framework.h"
#include "CanClient.h"
#include "CanClientDlg.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif


// CCanClientApp

BEGIN_MESSAGE_MAP(CCanClientApp, CWinApp)
    ON_COMMAND(ID_HELP, &CWinApp::OnHelp)
END_MESSAGE_MAP()


// CCanClientApp 생성

CCanClientApp::CCanClientApp()
{
    m_dwRestartManagerSupportFlags = AFX_RESTART_MANAGER_SUPPORT_RESTART;
}

// 유일한 CCanClientApp 개체입니다.
CCanClientApp theApp;


// CCanClientApp 초기화

BOOL CCanClientApp::InitInstance()
{
    INITCOMMONCONTROLSEX InitCtrls;
    InitCtrls.dwSize = sizeof(InitCtrls);
    InitCtrls.dwICC = ICC_WIN95_CLASSES;
    InitCommonControlsEx(&InitCtrls);

    CWinApp::InitInstance();
    AfxEnableControlContainer();
    CShellManager* pShellManager = new CShellManager;
    CMFCVisualManager::SetDefaultManager(RUNTIME_CLASS(CMFCVisualManagerWindows));

    SetRegistryKey(_T("로컬 애플리케이션 마법사에서 생성된 애플리케이션"));

    CCanClientDlg dlg;
    m_pMainWnd = &dlg;

    // [FIX] LoadAppSettings를 DoModal *전에* 호출합니다.
    // 이 시점에는 'this' (CWinApp)가 유효하므로 Assertion Failed가 발생하지 않습니다.
    LoadAppSettings(&dlg);

    INT_PTR nResponse = dlg.DoModal();
    if (nResponse == IDOK)
    {
    }
    else if (nResponse == IDCANCEL)
    {
    }
    else if (nResponse == -1)
    {
        TRACE(traceAppMsg, 0, "경고: 대화 상자를 만들지 못했으므로 애플리케이션이 예기치 않게 종료됩니다.\n");
        TRACE(traceAppMsg, 0, "경고: 대화 상자에서 MFC 컨트롤을 사용하는 경우 \"#define _AFX_NO_MFC_CONTROLS_IN_DIALOGS\"를 수행할 수 없습니다.\n");
    }

    // [FIX] DoModal이 끝난 후(프로그램 종료 시) 설정을 저장합니다.
    SaveAppSettings(&dlg);

    if (pShellManager != nullptr)
    {
        delete pShellManager;
    }

#if !defined(_AFXDLL) && !defined(_AFX_NO_MFC_CONTROLS_IN_DIALOGS)
    ControlBarCleanUp();
#endif
    return FALSE;
}


// [FIX] LoadAppSettings 함수 구현 (CanClientDlg.cpp에서 이동됨)
void CCanClientApp::LoadAppSettings(CCanClientDlg* pDlg)
{
    if (!pDlg) return;

    // Basic Settings
    pDlg->m_strServerIP = GetProfileString(_T("Network"), _T("ServerIP"), _T("127.0.0.1"));
    pDlg->m_nUploadPort = GetProfileInt(_T("Network"), _T("UploadPort"), 8080);
    pDlg->m_nRequestPort = GetProfileInt(_T("Network"), _T("RequestPort"), 8081);
    pDlg->m_topCamSerial = GetProfileString(_T("Camera"), _T("TopSerial"), _T(""));
    pDlg->m_sideCamSerial = GetProfileString(_T("Camera"), _T("SideSerial"), _T(""));

    // Advanced Settings (유효성 검사 포함)
    const double DEFAULT_PARAM = -1.0;
    pDlg->m_dTopFps = _ttof(GetProfileString(_T("CameraParams"), _T("TopFps"), _T("-1.0")));
    pDlg->m_dTopExposure = _ttof(GetProfileString(_T("CameraParams"), _T("TopExposure"), _T("-1.0")));
    pDlg->m_dTopGain = _ttof(GetProfileString(_T("CameraParams"), _T("TopGain"), _T("-1.0")));
    pDlg->m_dSideFps = _ttof(GetProfileString(_T("CameraParams"), _T("SideFps"), _T("-1.0")));
    pDlg->m_dSideExposure = _ttof(GetProfileString(_T("CameraParams"), _T("SideExposure"), _T("-1.0")));
    pDlg->m_dSideGain = _ttof(GetProfileString(_T("CameraParams"), _T("SideGain"), _T("-1.0")));

    // 비정상적인 값(너무 크거나 0)을 불러왔을 경우 -1.0(기본값)으로 리셋
    const double MAX_EXPOSURE = 1000000.0; // 1초
    const double MAX_FPS = 500.0;
    const double MAX_GAIN = 100.0;

    if (pDlg->m_dTopExposure > MAX_EXPOSURE || pDlg->m_dTopExposure == 0) pDlg->m_dTopExposure = DEFAULT_PARAM;
    if (pDlg->m_dTopFps > MAX_FPS || pDlg->m_dTopFps == 0) pDlg->m_dTopFps = DEFAULT_PARAM;
    if (pDlg->m_dTopGain > MAX_GAIN || pDlg->m_dTopGain == 0) pDlg->m_dTopGain = DEFAULT_PARAM;

    if (pDlg->m_dSideExposure > MAX_EXPOSURE || pDlg->m_dSideExposure == 0) pDlg->m_dSideExposure = DEFAULT_PARAM;
    if (pDlg->m_dSideFps > MAX_FPS || pDlg->m_dSideFps == 0) pDlg->m_dSideFps = DEFAULT_PARAM;
    if (pDlg->m_dSideGain > MAX_GAIN || pDlg->m_dSideGain == 0) pDlg->m_dSideGain = DEFAULT_PARAM;
}

// [FIX] SaveAppSettings 함수 구현 (CanClientDlg.cpp에서 이동됨)
void CCanClientApp::SaveAppSettings(CCanClientDlg* pDlg)
{
    if (!pDlg) return;

    // Basic Settings
    WriteProfileString(_T("Network"), _T("ServerIP"), pDlg->m_strServerIP);
    WriteProfileInt(_T("Network"), _T("UploadPort"), pDlg->m_nUploadPort);
    WriteProfileInt(_T("Network"), _T("RequestPort"), pDlg->m_nRequestPort);
    WriteProfileString(_T("Camera"), _T("TopSerial"), pDlg->m_topCamSerial);
    WriteProfileString(_T("Camera"), _T("SideSerial"), pDlg->m_sideCamSerial);

    // Advanced Settings (WriteProfileString 사용)
    CString strVal;
    const double DEFAULT_PARAM = -1.0;
    if (pDlg->m_dTopFps != DEFAULT_PARAM) { strVal.Format(_T("%.1f"), pDlg->m_dTopFps); WriteProfileString(_T("CameraParams"), _T("TopFps"), strVal); }
    if (pDlg->m_dTopExposure != DEFAULT_PARAM) { strVal.Format(_T("%.0f"), pDlg->m_dTopExposure); WriteProfileString(_T("CameraParams"), _T("TopExposure"), strVal); }
    if (pDlg->m_dTopGain != DEFAULT_PARAM) { strVal.Format(_T("%.1f"), pDlg->m_dTopGain); WriteProfileString(_T("CameraParams"), _T("TopGain"), strVal); }

    if (pDlg->m_dSideFps != DEFAULT_PARAM) { strVal.Format(_T("%.1f"), pDlg->m_dSideFps); WriteProfileString(_T("CameraParams"), _T("SideFps"), strVal); }
    if (pDlg->m_dSideExposure != DEFAULT_PARAM) { strVal.Format(_T("%.0f"), pDlg->m_dSideExposure); WriteProfileString(_T("CameraParams"), _T("SideExposure"), strVal); }
    if (pDlg->m_dSideGain != DEFAULT_PARAM) { strVal.Format(_T("%.1f"), pDlg->m_dSideGain); WriteProfileString(_T("CameraParams"), _T("SideGain"), strVal); }
}