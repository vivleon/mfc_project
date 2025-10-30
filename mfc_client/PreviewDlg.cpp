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

// [NEW] Load images from file paths
void CPreviewDlg::LoadImageFromFile(CString sPath, Gdiplus::Image** ppImage)
{
    if (sPath.IsEmpty()) return;

    // Free existing image if any
    if (*ppImage)
    {
        delete* ppImage;
        *ppImage = nullptr;
    }

    // Load new image
    *ppImage = Gdiplus::Image::FromFile(sPath);
    if ((*ppImage)->GetLastStatus() != Gdiplus::Ok)
    {
        AfxMessageBox(L"이미지 로드 실패: " + sPath);
        delete* ppImage;
        *ppImage = nullptr;
    }
}

void CPreviewDlg::DoDataExchange(CDataExchange* pDX)
{
    CDialogEx::DoDataExchange(pDX);
}

BOOL CPreviewDlg::OnInitDialog()
{
    CDialogEx::OnInitDialog();

    // Load images
    LoadImageFromFile(m_strPathTop, &m_imgTop);
    LoadImageFromFile(m_strPathSide, &m_imgSide);

    // [NEW] Set dark background for preview
    // We will do this in OnPaint to prevent flicker
    this->ModifyStyle(0, WS_CLIPCHILDREN);

    return TRUE;
}

BEGIN_MESSAGE_MAP(CPreviewDlg, CDialogEx)
    ON_WM_PAINT()
END_MESSAGE_MAP()

// [NEW] Draw images on paint
void CPreviewDlg::OnPaint()
{
    CPaintDC dc(this); // device context for painting

    // Fill background
    CRect rcClient;
    GetClientRect(&rcClient);
    CBrush brBkg;
    brBkg.CreateSolidBrush(RGB(30, 30, 30)); // Dark background
    dc.FillRect(&rcClient, &brBkg);
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
    dc.FillSolidRect(rc, RGB(0, 0, 0)); // Black background for image boxes

    if (!pImage) return; // No image to draw

    Graphics graphics(dc.GetSafeHdc());
    graphics.SetInterpolationMode(InterpolationModeHighQuality);

    // Calculate aspect ratio
    RectF rcDraw;
    REAL srcAR = (REAL)pImage->GetWidth() / pImage->GetHeight();
    REAL dstAR = (REAL)rc.Width() / rc.Height();

    if (srcAR > dstAR) {
        rcDraw.Width = (REAL)rc.Width();
        rcDraw.Height = rc.Width() / srcAR;
        rcDraw.X = 0;
        rcDraw.Y = (rc.Height() - rcDraw.Height) / 2;
    }
    else {
        rcDraw.Height = (REAL)rc.Height();
        rcDraw.Width = rc.Height() * srcAR;
        rcDraw.X = (rc.Width() - rcDraw.Width) / 2;
        rcDraw.Y = 0;
    }

    graphics.DrawImage(pImage, rcDraw, 0, 0, (REAL)pImage->GetWidth(), (REAL)pImage->GetHeight(), UnitPixel);
}

