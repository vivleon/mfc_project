#include "pch.h"
#include "CanClient.h"
#include "afxdialogex.h"
#include "CameraSettingsDlg.h"
#include "resource.h"

// Pylon GenApi for parameter access
#include <pylon/ParameterIncludes.h>

using namespace Pylon;
using namespace GenApi;

IMPLEMENT_DYNAMIC(CCameraSettingsDlg, CDialogEx)

CCameraSettingsDlg::CCameraSettingsDlg(CWnd* pParent /*=nullptr*/)
    : CDialogEx(IDD_CAMERA_SETTINGS, pParent)
    , m_selectedTopSerial(_T(""))
    , m_selectedSideSerial(_T(""))
    , m_pCamTop(nullptr)
    , m_pCamSide(nullptr)
    , m_dFps(0.0)
    , m_dExposure(0.0)
    , m_dGain(0.0)
    , m_sSelectedCamRole(_T(""))
{
}

CCameraSettingsDlg::~CCameraSettingsDlg()
{
}

void CCameraSettingsDlg::DoDataExchange(CDataExchange* pDX)
{
    CDialogEx::DoDataExchange(pDX);
    // Basic Controls
    DDX_Control(pDX, IDC_COMBO_TOP, m_comboTop);
    DDX_Control(pDX, IDC_COMBO_SIDE, m_comboSide);
    DDX_Control(pDX, IDC_EDIT_SERVER_IP, m_editServerIP);
    DDX_Control(pDX, IDC_EDIT_UPLOAD_PORT, m_editUploadPort);
    DDX_Control(pDX, IDC_EDIT_REQUEST_PORT, m_editRequestPort);
    // Advanced Controls
    DDX_Control(pDX, IDC_TAB_SETTINGS, m_tabSettings);
    DDX_Control(pDX, IDC_STATIC_GROUP_ADV, m_groupAdvSettings);
    DDX_Control(pDX, IDC_SLIDER_FPS, m_sliderFps);
    DDX_Control(pDX, IDC_EDIT_FPS, m_editFps);
    DDX_Control(pDX, IDC_SLIDER_EXPOSURE, m_sliderExposure);
    DDX_Control(pDX, IDC_EDIT_EXPOSURE, m_editExposure);
    DDX_Control(pDX, IDC_SLIDER_GAIN, m_sliderGain);
    DDX_Control(pDX, IDC_EDIT_GAIN, m_editGain);
    DDX_Control(pDX, IDC_STATIC_TARGET_CAM, m_staticTargetCam);
}

BEGIN_MESSAGE_MAP(CCameraSettingsDlg, CDialogEx)
    ON_BN_CLICKED(IDOK, &CCameraSettingsDlg::OnBnClickedOk)
    ON_NOTIFY(TCN_SELCHANGE, IDC_TAB_SETTINGS, &CCameraSettingsDlg::OnTcnSelchangeTabSettings)
    ON_WM_HSCROLL()
END_MESSAGE_MAP()

BOOL CCameraSettingsDlg::OnInitDialog()
{
    CDialogEx::OnInitDialog();

    // --- Initialize Basic Tab ---
    PopulateComboBoxes();
    m_editServerIP.SetWindowText(m_strServerIP);
    CString portStr;
    portStr.Format(_T("%d"), m_nUploadPort); m_editUploadPort.SetWindowText(portStr);
    portStr.Format(_T("%d"), m_nRequestPort); m_editRequestPort.SetWindowText(portStr);

    // --- Initialize Tab Control ---
    m_tabSettings.InsertItem(0, _T("기본 설정"));
    m_tabSettings.InsertItem(1, _T("고급 설정"));

    // --- Initialize Advanced Controls (Initially hidden) ---
    InitAdvancedControls();
    ShowTabControls(0); // Show Basic tab controls first

    // Load initial advanced parameters if a camera is selected
    LoadCameraParameters();

    return TRUE;
}

void CCameraSettingsDlg::PopulateComboBoxes()
{
    m_comboTop.ResetContent();
    m_comboSide.ResetContent();
    CString strNone = _T("None (사용 안 함)");
    int idxNoneTop = m_comboTop.AddString(strNone);
    int idxNoneSide = m_comboSide.AddString(strNone);
    // Store empty string as item data for "None"
    m_comboTop.SetItemDataPtr(idxNoneTop, new CString(_T("")));
    m_comboSide.SetItemDataPtr(idxNoneSide, new CString(_T("")));

    int currentTopSelection = idxNoneTop;
    int currentSideSelection = idxNoneSide;

    for (size_t i = 0; i < m_availableDevices.size(); ++i) {
        CString strCam = GetDeviceString(m_availableDevices[i]);
        CString serial = CString(m_availableDevices[i].GetSerialNumber().c_str());
        CString* pSerialTop = new CString(serial); // Allocate memory for item data
        CString* pSerialSide = new CString(serial);

        int listIndexTop = m_comboTop.AddString(strCam);
        m_comboTop.SetItemDataPtr(listIndexTop, pSerialTop);
        int listIndexSide = m_comboSide.AddString(strCam);
        m_comboSide.SetItemDataPtr(listIndexSide, pSerialSide);

        if (m_currentTopSerial == serial) currentTopSelection = listIndexTop;
        if (m_currentSideSerial == serial) currentSideSelection = listIndexSide;
    }
    m_comboTop.SetCurSel(currentTopSelection);
    m_comboSide.SetCurSel(currentSideSelection);
}

CString CCameraSettingsDlg::GetDeviceString(const Pylon::CDeviceInfo& dev)
{
    CString str;
    str.Format(_T("%s (%s)"), CString(dev.GetFriendlyName().c_str()), CString(dev.GetSerialNumber().c_str()));
    return str;
}

void CCameraSettingsDlg::InitAdvancedControls()
{
    // Set slider ranges (e.g., 0-1000 for integer representation)
    // Actual min/max will be loaded from camera
    m_sliderFps.SetRange(0, 1000);
    m_sliderExposure.SetRange(0, 10000); // Larger range for exposure
    m_sliderGain.SetRange(0, 500); // Smaller range for gain

    // Disable initially until a camera is loaded
    m_groupAdvSettings.EnableWindow(FALSE);
    m_sliderFps.EnableWindow(FALSE); m_editFps.EnableWindow(FALSE);
    m_sliderExposure.EnableWindow(FALSE); m_editExposure.EnableWindow(FALSE);
    m_sliderGain.EnableWindow(FALSE); m_editGain.EnableWindow(FALSE);
    m_staticTargetCam.SetWindowText(_T("카메라 선택 필요"));
}

void CCameraSettingsDlg::ShowTabControls(int nTab)
{
    // Show/Hide Basic Controls
    BOOL bShowBasic = (nTab == 0);
    GetDlgItem(IDC_STATIC_TOP_LABEL)->ShowWindow(bShowBasic ? SW_SHOW : SW_HIDE);
    GetDlgItem(IDC_COMBO_TOP)->ShowWindow(bShowBasic ? SW_SHOW : SW_HIDE);
    GetDlgItem(IDC_STATIC_SIDE_LABEL)->ShowWindow(bShowBasic ? SW_SHOW : SW_HIDE); // Assuming you add this label
    GetDlgItem(IDC_COMBO_SIDE)->ShowWindow(bShowBasic ? SW_SHOW : SW_HIDE);
    GetDlgItem(IDC_STATIC_SERVER_IP)->ShowWindow(bShowBasic ? SW_SHOW : SW_HIDE);
    GetDlgItem(IDC_EDIT_SERVER_IP)->ShowWindow(bShowBasic ? SW_SHOW : SW_HIDE);
    GetDlgItem(IDC_STATIC_UPLOAD_PORT)->ShowWindow(bShowBasic ? SW_SHOW : SW_HIDE);
    GetDlgItem(IDC_EDIT_UPLOAD_PORT)->ShowWindow(bShowBasic ? SW_SHOW : SW_HIDE);
    GetDlgItem(IDC_STATIC_REQUEST_PORT)->ShowWindow(bShowBasic ? SW_SHOW : SW_HIDE);
    GetDlgItem(IDC_EDIT_REQUEST_PORT)->ShowWindow(bShowBasic ? SW_SHOW : SW_HIDE);

    // Show/Hide Advanced Controls
    BOOL bShowAdv = (nTab == 1);
    m_groupAdvSettings.ShowWindow(bShowAdv ? SW_SHOW : SW_HIDE);
    GetDlgItem(IDC_STATIC_FPS)->ShowWindow(bShowAdv ? SW_SHOW : SW_HIDE);
    m_sliderFps.ShowWindow(bShowAdv ? SW_SHOW : SW_HIDE);
    m_editFps.ShowWindow(bShowAdv ? SW_SHOW : SW_HIDE);
    GetDlgItem(IDC_STATIC_EXPOSURE)->ShowWindow(bShowAdv ? SW_SHOW : SW_HIDE);
    m_sliderExposure.ShowWindow(bShowAdv ? SW_SHOW : SW_HIDE);
    m_editExposure.ShowWindow(bShowAdv ? SW_SHOW : SW_HIDE);
    GetDlgItem(IDC_STATIC_GAIN)->ShowWindow(bShowAdv ? SW_SHOW : SW_HIDE);
    m_sliderGain.ShowWindow(bShowAdv ? SW_SHOW : SW_HIDE);
    m_editGain.ShowWindow(bShowAdv ? SW_SHOW : SW_HIDE);
    m_staticTargetCam.ShowWindow(bShowAdv ? SW_SHOW : SW_HIDE);
}

void CCameraSettingsDlg::OnTcnSelchangeTabSettings(NMHDR* pNMHDR, LRESULT* pResult)
{
    int nSel = m_tabSettings.GetCurSel();
    ShowTabControls(nSel);

    if (nSel == 1) // If Advanced tab is selected
    {
        LoadCameraParameters(); // Reload parameters for the currently selected camera
    }
    *pResult = 0;
}

void CCameraSettingsDlg::OnHScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar)
{
    CSliderCtrl* pSlider = reinterpret_cast<CSliderCtrl*>(pScrollBar);
    double minVal = 0, maxVal = 0;

    // Determine which slider moved and update corresponding edit box
    if (pSlider == &m_sliderFps) {
        UpdateSliderRange(GetSelectedCameraForAdvancedSettings(m_sSelectedCamRole), "AcquisitionFrameRate", m_sliderFps, m_editFps); // Ensure range is correct
        GetPylonValue(GetSelectedCameraForAdvancedSettings(m_sSelectedCamRole), "AcquisitionFrameRate", minVal); // Get actual min
        GetPylonValue(GetSelectedCameraForAdvancedSettings(m_sSelectedCamRole), "AcquisitionFrameRate", maxVal, true); // Get actual max
        UpdateEditFromSlider(m_sliderFps, m_editFps, minVal, maxVal, _T("%.1f"));
    }
    else if (pSlider == &m_sliderExposure) {
        UpdateSliderRange(GetSelectedCameraForAdvancedSettings(m_sSelectedCamRole), "ExposureTime", m_sliderExposure, m_editExposure);
        GetPylonValue(GetSelectedCameraForAdvancedSettings(m_sSelectedCamRole), "ExposureTime", minVal);
        GetPylonValue(GetSelectedCameraForAdvancedSettings(m_sSelectedCamRole), "ExposureTime", maxVal, true);
        UpdateEditFromSlider(m_sliderExposure, m_editExposure, minVal, maxVal, _T("%.0f")); // Exposure often integer microseconds
    }
    else if (pSlider == &m_sliderGain) {
        UpdateSliderRange(GetSelectedCameraForAdvancedSettings(m_sSelectedCamRole), "Gain", m_sliderGain, m_editGain);
        GetPylonValue(GetSelectedCameraForAdvancedSettings(m_sSelectedCamRole), "Gain", minVal);
        GetPylonValue(GetSelectedCameraForAdvancedSettings(m_sSelectedCamRole), "Gain", maxVal, true);
        UpdateEditFromSlider(m_sliderGain, m_editGain, minVal, maxVal, _T("%.1f"));
    }

    CDialogEx::OnHScroll(nSBCode, nPos, pScrollBar);
}

// Helper to get selected camera for advanced settings
Pylon::CInstantCamera* CCameraSettingsDlg::GetSelectedCameraForAdvancedSettings(CString& role)
{
    // Prioritize TOP camera if selected and open
    int selTop = m_comboTop.GetCurSel();
    if (selTop > 0 && m_pCamTop && m_pCamTop->IsOpen()) { // Check index > 0 (not "None") and camera pointer valid
        CString* pSerial = (CString*)m_comboTop.GetItemDataPtr(selTop);
        if (pSerial && *pSerial == CString(m_pCamTop->GetDeviceInfo().GetSerialNumber().c_str())) {
            role = _T("TOP");
            return m_pCamTop;
        }
    }
    // Otherwise, check SIDE camera
    int selSide = m_comboSide.GetCurSel();
    if (selSide > 0 && m_pCamSide && m_pCamSide->IsOpen()) {
        CString* pSerial = (CString*)m_comboSide.GetItemDataPtr(selSide);
        if (pSerial && *pSerial == CString(m_pCamSide->GetDeviceInfo().GetSerialNumber().c_str())) {
            role = _T("SIDE");
            return m_pCamSide;
        }
    }
    role = _T("");
    return nullptr; // No valid camera selected or open
}


void CCameraSettingsDlg::LoadCameraParameters()
{
    CString role;
    CInstantCamera* pCam = GetSelectedCameraForAdvancedSettings(role);

    if (pCam && pCam->IsOpen())
    {
        m_sSelectedCamRole = role; // Store which camera's params are loaded
        m_staticTargetCam.SetWindowText(role + _T(" 카메라 고급 설정"));
        m_groupAdvSettings.EnableWindow(TRUE);
        m_sliderFps.EnableWindow(TRUE); m_editFps.EnableWindow(TRUE);
        m_sliderExposure.EnableWindow(TRUE); m_editExposure.EnableWindow(TRUE);
        m_sliderGain.EnableWindow(TRUE); m_editGain.EnableWindow(TRUE);

        // --- Load FPS ---
        UpdateSliderRange(pCam, "AcquisitionFrameRate", m_sliderFps, m_editFps);
        double currentFps = 0.0;
        if (GetPylonValue(pCam, "AcquisitionFrameRate", currentFps)) {
            UpdateSliderFromEdit(m_editFps, m_sliderFps, CFloatParameter(pCam->GetNodeMap(), "AcquisitionFrameRate").GetMin(), CFloatParameter(pCam->GetNodeMap(), "AcquisitionFrameRate").GetMax());
        }

        // --- Load Exposure ---
        UpdateSliderRange(pCam, "ExposureTime", m_sliderExposure, m_editExposure);
        double currentExposure = 0.0;
        if (GetPylonValue(pCam, "ExposureTime", currentExposure)) {
            UpdateSliderFromEdit(m_editExposure, m_sliderExposure, CFloatParameter(pCam->GetNodeMap(), "ExposureTime").GetMin(), CFloatParameter(pCam->GetNodeMap(), "ExposureTime").GetMax());
        }

        // --- Load Gain ---
        UpdateSliderRange(pCam, "Gain", m_sliderGain, m_editGain);
        double currentGain = 0.0;
        if (GetPylonValue(pCam, "Gain", currentGain)) {
            UpdateSliderFromEdit(m_editGain, m_sliderGain, CFloatParameter(pCam->GetNodeMap(), "Gain").GetMin(), CFloatParameter(pCam->GetNodeMap(), "Gain").GetMax());
        }
    }
    else
    {
        // No camera selected or open, disable controls
        m_sSelectedCamRole = _T("");
        m_staticTargetCam.SetWindowText(_T("연결된 카메라 선택 필요"));
        m_groupAdvSettings.EnableWindow(FALSE);
        m_sliderFps.EnableWindow(FALSE); m_editFps.EnableWindow(FALSE); m_editFps.SetWindowText(_T(""));
        m_sliderExposure.EnableWindow(FALSE); m_editExposure.EnableWindow(FALSE); m_editExposure.SetWindowText(_T(""));
        m_sliderGain.EnableWindow(FALSE); m_editGain.EnableWindow(FALSE); m_editGain.SetWindowText(_T(""));
    }
}

// Saves parameters from UI to member variables
void CCameraSettingsDlg::SaveCameraParameters()
{
    CString sVal;
    m_editFps.GetWindowText(sVal); m_dFps = _ttof(sVal);
    m_editExposure.GetWindowText(sVal); m_dExposure = _ttof(sVal);
    m_editGain.GetWindowText(sVal); m_dGain = _ttof(sVal);
    // m_sSelectedCamRole should already be set when LoadCameraParameters was called
}

void CCameraSettingsDlg::OnBnClickedOk()
{
    // --- Save Basic Settings ---
    int selTop = m_comboTop.GetCurSel();
    if (selTop != CB_ERR) {
        CString* pSerial = (CString*)m_comboTop.GetItemDataPtr(selTop);
        if (pSerial) m_selectedTopSerial = *pSerial;
    }
    int selSide = m_comboSide.GetCurSel();
    if (selSide != CB_ERR) {
        CString* pSerial = (CString*)m_comboSide.GetItemDataPtr(selSide);
        if (pSerial) m_selectedSideSerial = *pSerial;
    }

    // Clean up allocated CString data for combo boxes
    for (int i = 0; i < m_comboTop.GetCount(); ++i) delete (CString*)m_comboTop.GetItemDataPtr(i);
    for (int i = 0; i < m_comboSide.GetCount(); ++i) delete (CString*)m_comboSide.GetItemDataPtr(i);


    m_editServerIP.GetWindowText(m_strServerIP);
    CString portStr;
    m_editUploadPort.GetWindowText(portStr); m_nUploadPort = _ttoi(portStr);
    m_editRequestPort.GetWindowText(portStr); m_nRequestPort = _ttoi(portStr);

    // --- Save Advanced Settings (from UI to member variables) ---
    SaveCameraParameters();

    CDialogEx::OnOK(); // Close the dialog
}

// --- Pylon Parameter Helper Functions ---

// Template function to get parameter value safely
template<typename TParam> // TParam can be double, int64_t, bool, String_t etc.
bool CCameraSettingsDlg::GetPylonValue(Pylon::CInstantCamera* pCam, const char* paramName, TParam& value, bool getMax)
{
    if (!pCam || !pCam->IsOpen()) return false;
    try {
        INodeMap& nodemap = pCam->GetNodeMap();
        CParameter param(nodemap, paramName);
        if (!param.IsValid() || !IsReadable(param)) return false;

        // Use appropriate Pylon class based on TParam type hint
        if constexpr (std::is_same_v<TParam, double>) {
            CFloatParameter p(nodemap, paramName);
            value = getMax ? p.GetMax() : p.GetValue();
            return true;
        }
        else if constexpr (std::is_same_v<TParam, int64_t>) {
            CIntegerParameter p(nodemap, paramName);
            value = getMax ? p.GetMax() : p.GetValue();
            return true;
        }
        else if constexpr (std::is_same_v<TParam, bool>) {
            CBooleanParameter p(nodemap, paramName);
            if (getMax) return false; // Max doesn't apply to bool
            value = p.GetValue();
            return true;
        }
        // Add more types as needed (e.g., CEnumerationParameter)

    }
    catch (const GenericException& e) {
        // Log error e.GetDescription()
        return false;
    }
    return false;
}

// Template function to update slider range and edit box based on Pylon parameter
template<typename TParam> // TParam typically double or int64_t for sliders
void CCameraSettingsDlg::UpdateSliderRange(Pylon::CInstantCamera* pCam, const char* paramName, CSliderCtrl& slider, CEdit& edit)
{
    if (!pCam || !pCam->IsOpen()) return;
    try {
        INodeMap& nodemap = pCam->GetNodeMap();
        if constexpr (std::is_same_v<TParam, double> || std::is_same_v<TParam, float>) {
            CFloatParameter param(nodemap, paramName);
            if (param.IsValid() && IsReadable(param)) {
                double minVal = param.GetMin();
                double maxVal = param.GetMax();
                double curVal = param.GetValue();
                // Map double range to slider int range (e.g., 0-1000)
                int sliderMin = 0;
                int sliderMax = slider.GetRangeMax(); // Use existing max unless it's 0
                if (sliderMax == 0) sliderMax = 1000;
                slider.SetRange(sliderMin, sliderMax);
                int sliderPos = static_cast<int>(((curVal - minVal) / (maxVal - minVal)) * sliderMax);
                slider.SetPos(sliderPos);

                CString sCurVal;
                // Determine format based on parameter name
                if (strcmp(paramName, "ExposureTime") == 0) sCurVal.Format(_T("%.0f"), curVal);
                else sCurVal.Format(_T("%.1f"), curVal);
                edit.SetWindowText(sCurVal);
            }
        }
        else if constexpr (std::is_same_v<TParam, int64_t> || std::is_same_v<TParam, int>) {
            CIntegerParameter param(nodemap, paramName);
            if (param.IsValid() && IsReadable(param)) {
                int64_t minVal = param.GetMin();
                int64_t maxVal = param.GetMax();
                int64_t curVal = param.GetValue();
                // Assuming slider range matches integer range if reasonable
                if (maxVal < 32768 && minVal > -32768) { // Check if fits in slider's default 16-bit range
                    slider.SetRange(static_cast<int>(minVal), static_cast<int>(maxVal));
                    slider.SetPos(static_cast<int>(curVal));
                }
                else { // Need scaling like float
                    int sliderMin = 0;
                    int sliderMax = slider.GetRangeMax();
                    if (sliderMax == 0) sliderMax = 1000;
                    slider.SetRange(sliderMin, sliderMax);
                    if (maxVal > minVal) { // Avoid division by zero
                        int sliderPos = static_cast<int>(((double)(curVal - minVal) / (double)(maxVal - minVal)) * sliderMax);
                        slider.SetPos(sliderPos);
                    }
                    else { slider.SetPos(sliderMin); } // Set to min if range is zero
                }
                CString sCurVal; sCurVal.Format(_T("%lld"), curVal);
                edit.SetWindowText(sCurVal);
            }
        }
    }
    catch (const GenericException& e) {
        // Parameter might not exist or be of the wrong type
        edit.SetWindowText(_T("N/A"));
        slider.EnableWindow(FALSE);
    }
    catch (...) {
        edit.SetWindowText(_T("Error"));
        slider.EnableWindow(FALSE);
    }
}


// --- Slider/Edit Update Helpers ---
bool CCameraSettingsDlg::UpdateEditFromSlider(CSliderCtrl& slider, CEdit& edit, double minVal, double maxVal, CString format)
{
    if (maxVal <= minVal) return false; // Avoid division by zero
    int pos = slider.GetPos();
    int sMin = slider.GetRangeMin();
    int sMax = slider.GetRangeMax();
    double value = minVal + ((double)(pos - sMin) / (double)(sMax - sMin)) * (maxVal - minVal);
    CString sVal;
    sVal.Format(format, value);
    edit.SetWindowText(sVal);
    return true;
}

bool CCameraSettingsDlg::UpdateSliderFromEdit(CEdit& edit, CSliderCtrl& slider, double minVal, double maxVal)
{
    if (maxVal <= minVal) return false;
    CString sVal;
    edit.GetWindowText(sVal);
    double value = _ttof(sVal);
    // Clamp value to min/max
    value = max(minVal, min(maxVal, value));

    int sMin = slider.GetRangeMin();
    int sMax = slider.GetRangeMax();
    int pos = sMin + static_cast<int>(((value - minVal) / (maxVal - minVal)) * (sMax - sMin));
    slider.SetPos(pos);
    return true;
}

