#include "pch.h"
#include "CanClient.h"
#include "afxdialogex.h"
#include "PreviewDlg.h"

// GDI+
#include <gdiplus.h>
using namespace Gdiplus;
// GDI+ Token is now managed by CCanClientDlg

// CPreviewDlg Implementation
IMPLEMENT_DYNAMIC(CPreviewDlg, CDialogEx)

CPreviewDlg::CPreviewDlg(CWnd* pParent /*=nullptr*/)
    : CDialogEx(IDD_PREVIEW_DLG, pParent)
    , m_imgTop(nullptr)
    , m_imgSide(nullptr)
{
}

CPreviewDlg::~CPreviewDlg()
{
    // Clean up GDI+ images
    if (m_imgTop) delete m_imgTop;
    if (m_imgSide) delete m_imgSide;
}

// [NEW] Set paths
void CPreviewDlg::SetImagePaths(CString strPathTop, CString strPathSide)
{
    m_strPathTop = strPathTop;
    m_strPathSide = strPathSide;
}

// ========================================================================
// [MODIFIED] LoadImageFromFile (멈춤 현상 수정)
// ========================================================================
void CPreviewDlg::LoadImageFromFile(CString sPath, Gdiplus::Image** ppImage)
{
    if (sPath.IsEmpty()) return;

    // Free existing image if any
    if (*ppImage)
    {
        delete* ppImage;
        *ppImage = nullptr;
    }

    // [MODIFIED] CT2A(sPath)는 Unicode 빌드에서 CString(wchar_t*)을 
    // char*로 잘못 변환합니다. GDI+ FromFile은 wchar_t*를
    // 직접 받으므로 CString을 그대로 전달해야 합니다.
    *ppImage = Gdiplus::Image::FromFile(sPath); // <-- CT2A 래퍼 제거

    // Check for failure
    if (!*ppImage || (*ppImage)->GetLastStatus() != Gdiplus::Ok)
    {
        AfxTrace(L"GDI+ 이미지 로드 실패: %s", sPath);
        if (*ppImage) delete* ppImage;
        *ppImage = nullptr;
    }
}


void CPreviewDlg::DoDataExchange(CDataExchange* pDX)
{
    CDialogEx::DoDataExchange(pDX);
}

// [NEW] Load images on initialization
BOOL CPreviewDlg::OnInitDialog()
{
    CDialogEx::OnInitDialog();

    // [NEW] Load images
    LoadImageFromFile(m_strPathTop, &m_imgTop);
    LoadImageFromFile(m_strPathSide, &m_imgSide);

    return TRUE;  // return TRUE  unless you set the focus to a control
}

BEGIN_MESSAGE_MAP(CPreviewDlg, CDialogEx)
    ON_WM_PAINT()
END_MESSAGE_MAP()


// CPreviewDlg message handlers

// [NEW] OnPaint for drawing
void CPreviewDlg::OnPaint()
{
    CPaintDC dc(this); // device context for painting

    // Fill background
    CRect rcClient;
    GetClientRect(&rcClient);
    CBrush brBkg;
    //brBkg.CreateSolidBrush(RGB(30, 30, 30)); // Dark background
    dc.FillRect(rcClient, CBrush::FromHandle(GetSysColorBrush(COLOR_BTNFACE)));
    brBkg.DeleteObject();

    // Draw images
    DrawImageToCtrl(m_imgTop, IDC_IMG_LEFT);
    DrawImageToCtrl(m_imgSide, IDC_IMG_RIGHT);
}

void CPreviewDlg::DrawImageToCtrl(Gdiplus::Image* pImage, UINT nCtrlID)
{
    CWnd* pWnd = GetDlgItem(nCtrlID);
    if (!pWnd) return;

    CClientDC dc(pWnd);
    CRect rc;
    pWnd->GetClientRect(&rc);

    // Fill background first
    dc.FillRect(rc, CBrush::FromHandle(GetSysColorBrush(COLOR_BTNFACE)));

    if (!pImage) return; // No image to draw

    Graphics graphics(dc.GetSafeHdc());
    graphics.SetInterpolationMode(InterpolationModeHighQuality);

    // Calculate aspect ratio
    RectF rcDraw;
    REAL srcAR = (REAL)pImage->GetWidth() / pImage->GetHeight();
    REAL dstAR = (REAL)rc.Width() / rc.Height();

    if (srcAR > dstAR) {
        // Source is wider than destination
        rcDraw.Width = (REAL)rc.Width();
        rcDraw.Height = rc.Width() / srcAR;
        rcDraw.X = 0;
        rcDraw.Y = (rc.Height() - rcDraw.Height) / 2;
    }
    else {
        // Source is taller than destination
        rcDraw.Height = (REAL)rc.Height();
        rcDraw.Width = rc.Height() * srcAR;
        rcDraw.Y = 0;
        rcDraw.X = (rc.Width() - rcDraw.Width) / 2;
    }

    // Draw the image
    graphics.DrawImage(pImage, rcDraw, 0, 0, (REAL)pImage->GetWidth(), (REAL)pImage->GetHeight(), UnitPixel);
}